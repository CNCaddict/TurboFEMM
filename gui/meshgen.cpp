// FEMM Qt 6 GUI — Mesh generation implementation
#include "meshgen.h"
#include "document.h"
#include "femm_types.h"
#include "dialogs/motiondialog.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <cmath>
#include <cstdlib>
#include <set>
#include <algorithm>
#include <cstring>

// Triangle library API (compiled with TRILIBRARY)
extern "C" {
#include "triangle.h"
void trifree(VOID *memptr);
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
constexpr int kSlidingBandInnerPointMarker = -100001;
constexpr int kSlidingBandOuterPointMarker = -100002;
}

MeshGenerator::MeshGenerator(QObject *parent)
    : QObject(parent)
{
}

MeshGenerator::~MeshGenerator() = default;

bool MeshGenerator::generateMesh(FemmeDocument *doc)
{
    if (!doc) {
        m_lastError = "No document";
        return false;
    }

    QString femPath = doc->filePath();
    if (femPath.isEmpty()) {
        m_lastError = "Document must be saved before meshing";
        return false;
    }

    // Strip .fem extension to get base path
    QString basePath = femPath;
    if (basePath.endsWith(".fem", Qt::CaseInsensitive))
        basePath.chop(4);

    emit progress("Writing .poly file...");

    QString polyPath = basePath + ".poly";
    if (!writePoly(doc, polyPath))
        return false;

    emit progress("Running triangle mesh generator...");

    if (!runTriangle(basePath))
        return false;

    // Write .pbc file (periodic boundary conditions)
    // The solver requires this file to exist even if there are no PBCs.
    // Written AFTER triangle runs to ensure it doesn't get overwritten.
    emit progress("Writing .pbc file...");
    {
        QFile pbcFile(basePath + ".pbc");
        if (pbcFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream pbcOut(&pbcFile);
            pbcOut << "0\n0\n";
            pbcFile.close();
        }
    }

    emit progress("Loading mesh...");

    if (!loadMesh(doc, basePath))
        return false;

    doc->hasMesh = true;
    emit progress("Mesh complete.");
    return true;
}

// ---------------------------------------------------------------
// Write in-memory mesh to disk files (.node, .ele, .pbc)
// ---------------------------------------------------------------

bool MeshGenerator::writeMeshFiles(FemmeDocument *doc)
{
    if (!doc || doc->filePath().isEmpty()) {
        m_lastError = "No document or file path";
        return false;
    }
    if (doc->meshNodes.empty() || doc->meshElements.empty()) {
        m_lastError = "No mesh data to write";
        return false;
    }

    QByteArray base = doc->filePath().toLocal8Bit();
    if (doc->filePath().endsWith(".fem", Qt::CaseInsensitive))
        base.chop(4);

    int numNodes = (int)doc->meshNodes.size();
    int numElements = (int)doc->meshElements.size();

    // Use fprintf (C locale) — matches fkn's fscanf readers exactly.

    // Write .node file  (format: numNodes 2 0 1 / index x y boundary_marker)
    {
        FILE *fp = fopen(QByteArray(base + ".node").constData(), "w");
        if (!fp) {
            m_lastError = "Cannot write .node file";
            return false;
        }
        fprintf(fp, "%d 2 0 1\n", numNodes);
        for (int i = 0; i < numNodes; i++) {
            const auto &nd = doc->meshNodes[i];
            fprintf(fp, "%d\t%.17g\t%.17g\t%d\n",
                    i, nd.x, nd.y, nd.boundaryMarker);
        }
        fclose(fp);
    }

    // Write .ele file  (format: numElements 3 1 / index p0 p1 p2 label)
    {
        FILE *fp = fopen(QByteArray(base + ".ele").constData(), "w");
        if (!fp) {
            m_lastError = "Cannot write .ele file";
            return false;
        }
        fprintf(fp, "%d 3 1\n", numElements);
        for (int i = 0; i < numElements; i++) {
            const auto &el = doc->meshElements[i];
            fprintf(fp, "%d\t%d\t%d\t%d\t%d\n",
                    i, el.p[0], el.p[1], el.p[2], el.label);
        }
        fclose(fp);
    }

    // Write .pbc file  (format: numPBCs / [data] / numAGEs / [data])
    // Our Qt6 port has no periodic BCs or air gap elements.
    {
        FILE *fp = fopen(QByteArray(base + ".pbc").constData(), "w");
        if (!fp) {
            m_lastError = "Cannot write .pbc file";
            return false;
        }
        fprintf(fp, "0\n0\n");
        fclose(fp);
    }

    // Write .edge file  (format: numEdges 1 / index n0 n1 marker)
    // fkn requires this file for boundary condition mapping on edges.
    {
        FILE *fp = fopen(QByteArray(base + ".edge").constData(), "w");
        if (!fp) {
            m_lastError = "Cannot write .edge file";
            return false;
        }
        int numEdges = (int)doc->meshEdges.size();
        fprintf(fp, "%d\t1\n", numEdges);
        for (int i = 0; i < numEdges; i++) {
            const auto &e = doc->meshEdges[i];
            fprintf(fp, "%d\t%d\t%d\t%d\n", i, e.n0, e.n1, e.marker);
        }
        fclose(fp);
    }

    return true;
}

// ---------------------------------------------------------------
// Arc segment discretization
// ---------------------------------------------------------------

void MeshGenerator::discretizeArcs(FemmeDocument *doc,
                                     std::vector<TempNode> &extraNodes,
                                     std::vector<TempSegment> &extraSegments,
                                     int baseNodeCount)
{
    for (int i = 0; i < (int)doc->arcSegments.size(); i++) {
        const FArcSegment &arc = doc->arcSegments[i];
        if (arc.n0 < 0 || arc.n0 >= (int)doc->nodes.size()) continue;
        if (arc.n1 < 0 || arc.n1 >= (int)doc->nodes.size()) continue;

        double x0 = doc->nodes[arc.n0].x;
        double y0 = doc->nodes[arc.n0].y;
        double x1 = doc->nodes[arc.n1].x;
        double y1 = doc->nodes[arc.n1].y;

        double arcAngle = arc.arcLength * M_PI / 180.0;
        int k = std::max(1, (int)std::ceil(arc.arcLength / arc.maxSideLength));

        // Find arc center
        double mx = (x0 + x1) / 2.0;
        double my = (y0 + y1) / 2.0;
        double dx = x1 - x0, dy = y1 - y0;
        double halfChord = std::sqrt(dx*dx + dy*dy) / 2.0;
        if (halfChord < 1e-12) continue;

        double R = halfChord / std::sin(arcAngle / 2.0);
        double d = R * std::cos(arcAngle / 2.0);

        // Normal to chord
        double nx = -dy / (2.0 * halfChord);
        double ny = dx / (2.0 * halfChord);

        double cx = mx + d * nx;
        double cy = my + d * ny;

        // Boundary marker for arc segments
        int bm = 0;
        if (arc.boundaryMarker != "<None>") {
            int idx = doc->findBoundaryPropIndex(arc.boundaryMarker);
            if (idx >= 0) bm = -(idx + 2);
        }

        // Generate intermediate points by rotating CCW around center
        double startAngle = std::atan2(y0 - cy, x0 - cx);
        double dAngle = arcAngle / k;

        int prevNode = arc.n0;

        for (int j = 1; j < k; j++) {
            double angle = startAngle + dAngle * j;
            TempNode tn;
            tn.x = cx + std::fabs(R) * std::cos(angle);
            tn.y = cy + std::fabs(R) * std::sin(angle);
            tn.boundaryMarker = 0;

            int newIdx = baseNodeCount + (int)extraNodes.size();
            extraNodes.push_back(tn);

            TempSegment ts;
            ts.n0 = prevNode;
            ts.n1 = newIdx;
            ts.boundaryMarker = bm;
            extraSegments.push_back(ts);

            prevNode = newIdx;
        }

        // Final segment to end node
        TempSegment ts;
        ts.n0 = prevNode;
        ts.n1 = arc.n1;
        ts.boundaryMarker = bm;
        extraSegments.push_back(ts);
    }
}

// ---------------------------------------------------------------
// Write .poly file
// ---------------------------------------------------------------

bool MeshGenerator::writePoly(FemmeDocument *doc, const QString &polyPath)
{
    // Discretize arcs first
    std::vector<TempNode> extraNodes;
    std::vector<TempSegment> extraSegments;
    discretizeArcs(doc, extraNodes, extraSegments, (int)doc->nodes.size());

    QFile file(polyPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastError = "Cannot write " + polyPath;
        return false;
    }

    QTextStream out(&file);
    out.setRealNumberPrecision(17);

    int totalNodes = (int)doc->nodes.size() + (int)extraNodes.size();
    int totalSegments = (int)doc->segments.size() + (int)extraSegments.size();

    // ---- Node section ----
    // format: <# nodes> <dimension=2> <# attributes=0> <boundary markers=1>
    out << totalNodes << "\t2\t0\t1\n";

    // Write original nodes
    for (int i = 0; i < (int)doc->nodes.size(); i++) {
        const FNode &nd = doc->nodes[i];
        int bm = 0;
        if (nd.boundaryMarker != "<None>") {
            int idx = doc->findPointPropIndex(nd.boundaryMarker);
            if (idx >= 0) bm = idx + 2;
        }
        out << i << "\t" << nd.x << "\t" << nd.y << "\t" << bm << "\n";
    }

    // Write extra nodes from arc discretization
    for (int i = 0; i < (int)extraNodes.size(); i++) {
        int idx = (int)doc->nodes.size() + i;
        out << idx << "\t" << extraNodes[i].x << "\t" << extraNodes[i].y
            << "\t" << extraNodes[i].boundaryMarker << "\n";
    }

    // ---- Segment section ----
    // format: <# segments> <boundary markers=1>
    out << totalSegments << "\t1\n";

    // Write original segments
    for (int i = 0; i < (int)doc->segments.size(); i++) {
        const FSegment &seg = doc->segments[i];
        int bm = 0;
        if (seg.boundaryMarker != "<None>") {
            int idx = doc->findBoundaryPropIndex(seg.boundaryMarker);
            if (idx >= 0) bm = -(idx + 2);
        }
        out << i << "\t" << seg.n0 << "\t" << seg.n1 << "\t" << bm << "\n";
    }

    // Write extra segments from arc discretization
    for (int i = 0; i < (int)extraSegments.size(); i++) {
        int idx = (int)doc->segments.size() + i;
        out << idx << "\t" << extraSegments[i].n0 << "\t" << extraSegments[i].n1
            << "\t" << extraSegments[i].boundaryMarker << "\n";
    }

    // ---- Holes section ----
    int numHoles = 0;
    for (const auto &blk : doc->blockLabels)
        if (blk.blockType == "<No Mesh>") numHoles++;

    out << numHoles << "\n";
    int holeIdx = 0;
    for (const auto &blk : doc->blockLabels) {
        if (blk.blockType == "<No Mesh>") {
            out << holeIdx << "\t" << blk.x << "\t" << blk.y << "\n";
            holeIdx++;
        }
    }

    // ---- Region attributes section ----
    // Compute default mesh size from bounding box
    double xmin, ymin, xmax, ymax;
    double defaultMeshSize = 1.0;
    if (doc->getBoundingBox(xmin, ymin, xmax, ymax)) {
        double diag = std::sqrt((xmax-xmin)*(xmax-xmin) + (ymax-ymin)*(ymax-ymin));
        defaultMeshSize = std::pow(diag / 10.0, 2.0);
    }

    int numRegions = (int)doc->blockLabels.size() - numHoles;
    out << numRegions << "\n";
    int regIdx = 0;
    for (const auto &blk : doc->blockLabels) {
        if (blk.blockType == "<No Mesh>") continue;

        out << regIdx << "\t" << blk.x << "\t" << blk.y << "\t" << (regIdx + 1) << "\t";

        if (blk.maxArea > 0 && blk.maxArea < defaultMeshSize)
            out << blk.maxArea;
        else
            out << defaultMeshSize;

        out << "\n";
        regIdx++;
    }

    file.close();
    return true;
}

// ---------------------------------------------------------------
// Run triangle
// ---------------------------------------------------------------

bool MeshGenerator::runTriangle(const QString &basePath)
{
    // Find triangle executable
    QString trianglePath = m_trianglePath;
    if (trianglePath.isEmpty()) {
        // App is at: build/gui/femm-gui.app/Contents/MacOS/femm-gui
        // Triangle is at: build/triangle/triangle
        QDir appDir(QCoreApplication::applicationDirPath());
        QString buildDir = QDir::cleanPath(appDir.absolutePath() + "/../../../..");

        // Check common locations
        QStringList candidates = {
            appDir.filePath("triangle"),                      // same dir
            buildDir + "/triangle/triangle",                  // build/triangle/triangle
            QDir::cleanPath(appDir.absolutePath() + "/../../../../triangle/triangle"),
            QDir::cleanPath(appDir.absolutePath() + "/../triangle"),
        };

        for (const auto &c : candidates) {
            if (QFileInfo::exists(c)) {
                trianglePath = QFileInfo(c).absoluteFilePath();
                break;
            }
        }

        if (trianglePath.isEmpty()) {
            m_lastError = QString("Cannot find triangle executable. Searched:\n%1")
                .arg(candidates.join("\n"));
            return false;
        }
    }

    // Run: triangle -p -P -q<minAngle> -e -A -a -z -Q -I <basename>
    QStringList args;
    args << "-p" << "-P" << "-q30" << "-e" << "-A" << "-a" << "-z" << "-Q" << "-I"
         << basePath;

    QProcess process;
    process.start(trianglePath, args);

    if (!process.waitForStarted(5000)) {
        m_lastError = "Failed to start triangle: " + process.errorString();
        return false;
    }

    if (!process.waitForFinished(60000)) {
        m_lastError = "Triangle timed out";
        process.kill();
        return false;
    }

    if (process.exitCode() != 0) {
        m_lastError = "Triangle failed: " + QString::fromUtf8(process.readAllStandardError());
        return false;
    }

    return true;
}

// ---------------------------------------------------------------
// Load mesh results
// ---------------------------------------------------------------

bool MeshGenerator::loadMesh(FemmeDocument *doc, const QString &basePath)
{
    doc->meshNodes.clear();
    doc->meshElements.clear();

    // Read .node file
    {
        QFile nodeFile(basePath + ".node");
        if (!nodeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_lastError = "Cannot read .node file";
            return false;
        }
        QTextStream in(&nodeFile);

        // First line: <# nodes> <dim> <# attr> <# boundary markers>
        QString header = in.readLine().trimmed();
        int numNodes = header.split(QRegularExpression("\\s+")).first().toInt();
        doc->meshNodes.reserve(numNodes);

        for (int i = 0; i < numNodes && !in.atEnd(); i++) {
            QString line = in.readLine().trimmed();
            QStringList parts = line.split(QRegularExpression("\\s+"));
            if (parts.size() < 3) continue;

            FMeshNode mn;
            mn.x = parts[1].toDouble();
            mn.y = parts[2].toDouble();
            if (parts.size() >= 4)
                mn.boundaryMarker = parts[3].toInt();
            doc->meshNodes.push_back(mn);
        }
        nodeFile.close();
    }

    // Read .ele file
    {
        QFile eleFile(basePath + ".ele");
        if (!eleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_lastError = "Cannot read .ele file";
            return false;
        }
        QTextStream in(&eleFile);

        // First line: <# triangles> <nodes per tri> <# attributes>
        QString header = in.readLine().trimmed();
        int numEls = header.split(QRegularExpression("\\s+")).first().toInt();
        doc->meshElements.reserve(numEls);

        for (int i = 0; i < numEls && !in.atEnd(); i++) {
            QString line = in.readLine().trimmed();
            QStringList parts = line.split(QRegularExpression("\\s+"));
            if (parts.size() < 4) continue;

            FMeshElement me;
            me.p[0] = parts[1].toInt();
            me.p[1] = parts[2].toInt();
            me.p[2] = parts[3].toInt();
            if (parts.size() >= 5)
                me.label = parts[4].toInt();
            doc->meshElements.push_back(me);
        }
        eleFile.close();
    }

    // Read .edge file (if it exists — created by Triangle, needed by solver)
    {
        QFile edgeFile(basePath + ".edge");
        if (edgeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&edgeFile);
            QString header = in.readLine().trimmed();
            int numEdges = header.split(QRegularExpression("\\s+")).first().toInt();
            doc->meshEdges.reserve(numEdges);
            for (int i = 0; i < numEdges && !in.atEnd(); i++) {
                QString line = in.readLine().trimmed();
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() < 4) continue;
                MeshEdge me;
                me.n0 = parts[1].toInt();
                me.n1 = parts[2].toInt();
                me.marker = parts[3].toInt();
                doc->meshEdges.push_back(me);
            }
            edgeFile.close();
        }
    }

    return true;
}

// ---------------------------------------------------------------
// In-process mesh generation using Triangle library
// ---------------------------------------------------------------

bool MeshGenerator::generateMeshInProcess(FemmeDocument *doc, std::vector<MeshEdge> &edgesOut,
                                           SlidingBand *bandSetup)
{
    if (!doc) {
        m_lastError = "No document";
        return false;
    }

    emit progress("Generating mesh in-process...");

    // Discretize arcs into line segments
    std::vector<TempNode> extraNodes;
    std::vector<TempSegment> extraSegments;
    discretizeArcs(doc, extraNodes, extraSegments, (int)doc->nodes.size());

    // --- Sliding band: inject constrained interface circles ---
    // The interface rings must be explicit PSLG segments so Triangle keeps
    // the sliding band confined to the airgap instead of allowing triangles
    // to span across the intended remesh strip.
    if (bandSetup && bandSetup->active && bandSetup->isRotation
        && bandSetup->numInterfaceNodes > 0)
    {
        const int baseNodeCount = (int)doc->nodes.size();
        const int N = bandSetup->numInterfaceNodes;
        const double cx = bandSetup->cx;
        const double cy = bandSetup->cy;
        const double rInner = bandSetup->innerRadius;
        const double rOuter = bandSetup->outerRadius;

        const int innerStart = baseNodeCount + (int)extraNodes.size();
        for (int i = 0; i < N; i++) {
            double angle = 2.0 * M_PI * i / N;
            TempNode tn;
            tn.x = cx + rInner * std::cos(angle);
            tn.y = cy + rInner * std::sin(angle);
            tn.boundaryMarker = kSlidingBandInnerPointMarker;
            extraNodes.push_back(tn);
        }
        for (int i = 0; i < N; i++) {
            TempSegment ts;
            ts.n0 = innerStart + i;
            ts.n1 = innerStart + ((i + 1) % N);
            ts.boundaryMarker = 0;
            extraSegments.push_back(ts);
        }

        const int outerStart = baseNodeCount + (int)extraNodes.size();
        for (int i = 0; i < N; i++) {
            double angle = 2.0 * M_PI * i / N;
            TempNode tn;
            tn.x = cx + rOuter * std::cos(angle);
            tn.y = cy + rOuter * std::sin(angle);
            tn.boundaryMarker = kSlidingBandOuterPointMarker;
            extraNodes.push_back(tn);
        }
        for (int i = 0; i < N; i++) {
            TempSegment ts;
            ts.n0 = outerStart + i;
            ts.n1 = outerStart + ((i + 1) % N);
            ts.boundaryMarker = 0;
            extraSegments.push_back(ts);
        }

        emit progress(QString("  Sliding band: %1 nodes/circle with constrained segments, innerR=%2, outerR=%3")
                       .arg(N).arg(rInner, 0, 'f', 4).arg(rOuter, 0, 'f', 4));
    }

    int totalNodes = (int)doc->nodes.size() + (int)extraNodes.size();
    int totalSegments = (int)doc->segments.size() + (int)extraSegments.size();

    // Count holes and regions
    int numHoles = 0;
    for (const auto &blk : doc->blockLabels)
        if (blk.blockType == "<No Mesh>") numHoles++;
    int numRegions = (int)doc->blockLabels.size() - numHoles;

    // Compute default mesh size from bounding box
    double xmin, ymin, xmax, ymax;
    double defaultMeshSize = 1.0;
    if (doc->getBoundingBox(xmin, ymin, xmax, ymax)) {
        double diag = std::sqrt((xmax-xmin)*(xmax-xmin) + (ymax-ymin)*(ymax-ymin));
        defaultMeshSize = std::pow(diag / 10.0, 2.0);
    }

    // Populate Triangle input struct
    struct triangulateio in, out;
    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));

    // Points
    in.numberofpoints = totalNodes;
    in.numberofpointattributes = 0;
    in.pointlist = (REAL *)malloc(totalNodes * 2 * sizeof(REAL));
    in.pointmarkerlist = (int *)malloc(totalNodes * sizeof(int));

    for (int i = 0; i < (int)doc->nodes.size(); i++) {
        const FNode &nd = doc->nodes[i];
        in.pointlist[i*2]     = nd.x;
        in.pointlist[i*2 + 1] = nd.y;
        int bm = 0;
        if (nd.boundaryMarker != "<None>") {
            int idx = doc->findPointPropIndex(nd.boundaryMarker);
            if (idx >= 0) bm = idx + 2;
        }
        in.pointmarkerlist[i] = bm;
    }
    for (int i = 0; i < (int)extraNodes.size(); i++) {
        int idx = (int)doc->nodes.size() + i;
        in.pointlist[idx*2]     = extraNodes[i].x;
        in.pointlist[idx*2 + 1] = extraNodes[i].y;
        in.pointmarkerlist[idx] = extraNodes[i].boundaryMarker;
    }

    // Segments
    in.numberofsegments = totalSegments;
    in.segmentlist = (int *)malloc(totalSegments * 2 * sizeof(int));
    in.segmentmarkerlist = (int *)malloc(totalSegments * sizeof(int));

    for (int i = 0; i < (int)doc->segments.size(); i++) {
        const FSegment &seg = doc->segments[i];
        in.segmentlist[i*2]     = seg.n0;
        in.segmentlist[i*2 + 1] = seg.n1;
        int bm = 0;
        if (seg.boundaryMarker != "<None>") {
            int idx = doc->findBoundaryPropIndex(seg.boundaryMarker);
            if (idx >= 0) bm = -(idx + 2);
        }
        in.segmentmarkerlist[i] = bm;
    }
    for (int i = 0; i < (int)extraSegments.size(); i++) {
        int idx = (int)doc->segments.size() + i;
        in.segmentlist[idx*2]     = extraSegments[i].n0;
        in.segmentlist[idx*2 + 1] = extraSegments[i].n1;
        in.segmentmarkerlist[idx] = extraSegments[i].boundaryMarker;
    }

    // Holes
    in.numberofholes = numHoles;
    if (numHoles > 0) {
        in.holelist = (REAL *)malloc(numHoles * 2 * sizeof(REAL));
        int hi = 0;
        for (const auto &blk : doc->blockLabels) {
            if (blk.blockType == "<No Mesh>") {
                in.holelist[hi*2]     = blk.x;
                in.holelist[hi*2 + 1] = blk.y;
                hi++;
            }
        }
    }

    // Regions (x, y, attribute, max area) — 4 REALs per region
    in.numberofregions = numRegions;
    if (numRegions > 0) {
        in.regionlist = (REAL *)malloc(numRegions * 4 * sizeof(REAL));
        int ri = 0;
        for (const auto &blk : doc->blockLabels) {
            if (blk.blockType == "<No Mesh>") continue;
            in.regionlist[ri*4]     = blk.x;
            in.regionlist[ri*4 + 1] = blk.y;
            in.regionlist[ri*4 + 2] = (REAL)(ri + 1);  // 1-based label
            double effectiveArea = (blk.maxArea > 0 && blk.maxArea < defaultMeshSize)
                                   ? blk.maxArea : defaultMeshSize;
            in.regionlist[ri*4 + 3] = effectiveArea;
            emit progress(QString("  Region %1 '%2': maxArea=%3 (effective=%4, default=%5)")
                          .arg(ri).arg(blk.blockType)
                          .arg(blk.maxArea, 0, 'g', 6)
                          .arg(effectiveArea, 0, 'g', 6)
                          .arg(defaultMeshSize, 0, 'g', 6));
            ri++;
        }
    }

    // Call Triangle — same switches as subprocess: pPq30eAazQ
    // (p=PSLG, P=no output .poly, q30=quality 30deg, e=edges, A=attributes,
    //  a=area constraints, z=zero-indexed, Q=quiet). With a sliding band,
    // also suppress Steiner insertion on constrained segments so the
    // interface rings stay stable between steps.
    char switches[] = "pPq30eAazQ";
    char bandSwitches[] = "pPq30eAazQY";
    char *triangleSwitches = (bandSetup && bandSetup->active) ? bandSwitches : switches;
    triangulate(triangleSwitches, &in, &out, NULL);

    // Copy results to document
    doc->meshNodes.clear();
    doc->meshNodes.reserve(out.numberofpoints);
    for (int i = 0; i < out.numberofpoints; i++) {
        FMeshNode mn;
        mn.x = out.pointlist[i*2];
        mn.y = out.pointlist[i*2 + 1];
        mn.boundaryMarker = out.pointmarkerlist ? out.pointmarkerlist[i] : 0;
        doc->meshNodes.push_back(mn);
    }

    doc->meshElements.clear();
    doc->meshElements.reserve(out.numberoftriangles);
    for (int i = 0; i < out.numberoftriangles; i++) {
        FMeshElement me;
        me.p[0] = out.trianglelist[i * out.numberofcorners];
        me.p[1] = out.trianglelist[i * out.numberofcorners + 1];
        me.p[2] = out.trianglelist[i * out.numberofcorners + 2];
        if (out.numberoftriangleattributes > 0)
            me.label = (int)out.triangleattributelist[i * out.numberoftriangleattributes];
        doc->meshElements.push_back(me);
    }

    // Copy edges (needed by solver for boundary condition mapping)
    edgesOut.clear();
    edgesOut.reserve(out.numberofedges);
    for (int i = 0; i < out.numberofedges; i++) {
        MeshEdge me;
        me.n0 = out.edgelist[i*2];
        me.n1 = out.edgelist[i*2 + 1];
        me.marker = out.edgemarkerlist ? out.edgemarkerlist[i] : 0;
        edgesOut.push_back(me);
    }
    // Also store edges in the document so writeMeshFiles can write the .edge
    // file later (fkn requires it for boundary condition mapping).
    doc->meshEdges = edgesOut;

    // Free Triangle-allocated output arrays
    if (out.pointlist) trifree((VOID *)out.pointlist);
    if (out.pointattributelist) trifree((VOID *)out.pointattributelist);
    if (out.pointmarkerlist) trifree((VOID *)out.pointmarkerlist);
    if (out.trianglelist) trifree((VOID *)out.trianglelist);
    if (out.triangleattributelist) trifree((VOID *)out.triangleattributelist);
    if (out.segmentlist) trifree((VOID *)out.segmentlist);
    if (out.segmentmarkerlist) trifree((VOID *)out.segmentmarkerlist);
    if (out.edgelist) trifree((VOID *)out.edgelist);
    if (out.edgemarkerlist) trifree((VOID *)out.edgemarkerlist);

    // Free input arrays (we allocated these)
    free(in.pointlist);
    free(in.pointmarkerlist);
    free(in.segmentlist);
    free(in.segmentmarkerlist);
    if (in.holelist) free(in.holelist);
    if (in.regionlist) free(in.regionlist);

    doc->hasMesh = true;
    emit progress(QString("Mesh complete: %1 nodes, %2 elements, %3 edges")
                  .arg(doc->meshNodes.size())
                  .arg(doc->meshElements.size())
                  .arg(edgesOut.size()));
    return true;
}

// ---------------------------------------------------------------
// Refine existing mesh using Triangle's -r switch
// ---------------------------------------------------------------
bool MeshGenerator::refineMeshInProcess(
    FemmeDocument *doc,
    const std::vector<MeshEdge> &segmentEdges,
    const std::vector<double> &triangleAreaList,
    std::vector<MeshEdge> &edgesOut)
{
    int numNodes = (int)doc->meshNodes.size();
    int numElems = (int)doc->meshElements.size();
    int numSegments = (int)segmentEdges.size();

    if (numElems != (int)triangleAreaList.size()) {
        m_lastError = "Area list size does not match element count";
        return false;
    }
    if (numElems == 0 || numNodes == 0) {
        m_lastError = "No mesh to refine";
        return false;
    }

    // Triangle library types
    struct triangulateio in, out;
    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));

    // --- Points from current mesh ---
    in.numberofpoints = numNodes;
    in.numberofpointattributes = 0;
    in.pointlist = (REAL *)malloc(numNodes * 2 * sizeof(REAL));
    in.pointmarkerlist = (int *)malloc(numNodes * sizeof(int));
    for (int i = 0; i < numNodes; i++) {
        in.pointlist[i*2]     = doc->meshNodes[i].x;
        in.pointlist[i*2 + 1] = doc->meshNodes[i].y;
        in.pointmarkerlist[i] = doc->meshNodes[i].boundaryMarker;
    }

    // --- Triangles from current mesh ---
    in.numberoftriangles = numElems;
    in.numberofcorners = 3;
    in.numberoftriangleattributes = 1;
    in.trianglelist = (int *)malloc(numElems * 3 * sizeof(int));
    in.triangleattributelist = (REAL *)malloc(numElems * sizeof(REAL));
    in.trianglearealist = (REAL *)malloc(numElems * sizeof(REAL));
    for (int i = 0; i < numElems; i++) {
        in.trianglelist[i*3]     = doc->meshElements[i].p[0];
        in.trianglelist[i*3 + 1] = doc->meshElements[i].p[1];
        in.trianglelist[i*3 + 2] = doc->meshElements[i].p[2];
        in.triangleattributelist[i] = (REAL)doc->meshElements[i].label;
        in.trianglearealist[i] = triangleAreaList[i];
    }

    // --- Constrained segments (boundary edges) ---
    in.numberofsegments = numSegments;
    if (numSegments > 0) {
        in.segmentlist = (int *)malloc(numSegments * 2 * sizeof(int));
        in.segmentmarkerlist = (int *)malloc(numSegments * sizeof(int));
        for (int i = 0; i < numSegments; i++) {
            in.segmentlist[i*2]     = segmentEdges[i].n0;
            in.segmentlist[i*2 + 1] = segmentEdges[i].n1;
            in.segmentmarkerlist[i] = segmentEdges[i].marker;
        }
    }

    // No holes or regions needed for refinement
    in.numberofholes = 0;
    in.numberofregions = 0;

    // Refine: "r" = refinement mode, q30 = quality, e = edges,
    // A = propagate attributes, a = variable area constraints,
    // z = zero-indexed, Q = quiet
    char switches[] = "rq30eAazQ";

    // Count how many elements have actual area constraints (for diagnostics)
    int numConstrained = 0;
    for (int i = 0; i < numElems; i++) {
        if (in.trianglearealist[i] > 0)
            numConstrained++;
    }

    triangulate(switches, &in, &out, NULL);

    if (out.numberoftriangles == 0) {
        m_lastError = "Triangle refinement produced no elements";
        // Free input
        free(in.pointlist);
        free(in.pointmarkerlist);
        free(in.trianglelist);
        free(in.triangleattributelist);
        free(in.trianglearealist);
        if (in.segmentlist) free(in.segmentlist);
        if (in.segmentmarkerlist) free(in.segmentmarkerlist);
        return false;
    }

    // --- Copy refined mesh back to document ---
    doc->meshNodes.clear();
    doc->meshNodes.reserve(out.numberofpoints);
    for (int i = 0; i < out.numberofpoints; i++) {
        FMeshNode mn;
        mn.x = out.pointlist[i*2];
        mn.y = out.pointlist[i*2 + 1];
        mn.boundaryMarker = out.pointmarkerlist ? out.pointmarkerlist[i] : 0;
        doc->meshNodes.push_back(mn);
    }

    doc->meshElements.clear();
    doc->meshElements.reserve(out.numberoftriangles);
    for (int i = 0; i < out.numberoftriangles; i++) {
        FMeshElement me;
        me.p[0] = out.trianglelist[i * out.numberofcorners];
        me.p[1] = out.trianglelist[i * out.numberofcorners + 1];
        me.p[2] = out.trianglelist[i * out.numberofcorners + 2];
        if (out.numberoftriangleattributes > 0)
            me.label = (int)out.triangleattributelist[i * out.numberoftriangleattributes];
        doc->meshElements.push_back(me);
    }

    edgesOut.clear();
    edgesOut.reserve(out.numberofedges);
    for (int i = 0; i < out.numberofedges; i++) {
        MeshEdge me;
        me.n0 = out.edgelist[i*2];
        me.n1 = out.edgelist[i*2 + 1];
        me.marker = out.edgemarkerlist ? out.edgemarkerlist[i] : 0;
        edgesOut.push_back(me);
    }

    // Free Triangle-allocated output arrays
    if (out.pointlist) trifree((VOID *)out.pointlist);
    if (out.pointattributelist) trifree((VOID *)out.pointattributelist);
    if (out.pointmarkerlist) trifree((VOID *)out.pointmarkerlist);
    if (out.trianglelist) trifree((VOID *)out.trianglelist);
    if (out.triangleattributelist) trifree((VOID *)out.triangleattributelist);
    if (out.segmentlist) trifree((VOID *)out.segmentlist);
    if (out.segmentmarkerlist) trifree((VOID *)out.segmentmarkerlist);
    if (out.edgelist) trifree((VOID *)out.edgelist);
    if (out.edgemarkerlist) trifree((VOID *)out.edgemarkerlist);

    // Free input arrays
    free(in.pointlist);
    free(in.pointmarkerlist);
    free(in.trianglelist);
    free(in.triangleattributelist);
    free(in.trianglearealist);
    if (in.segmentlist) free(in.segmentlist);
    if (in.segmentmarkerlist) free(in.segmentmarkerlist);

    doc->hasMesh = true;
    emit progress(QString("Refined mesh: %1 nodes, %2 elements")
                  .arg(doc->meshNodes.size())
                  .arg(doc->meshElements.size()));
    return true;
}

// ---------------------------------------------------------------
// Sliding band — manual setup from user-specified radius
// ---------------------------------------------------------------

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool MeshGenerator::setupSlidingBand(FemmeDocument *doc, const MotionConfig &config,
                                      double innerR, double outerR, SlidingBand &band)
{
    if (!doc || !config.isRotation) {
        m_lastError = "Sliding band currently only supports rotary motion";
        return false;
    }

    const double cx = config.cx;
    const double cy = config.cy;
    const int movingGroup = config.groupNumber;

    // Determine which side is rotor from block label positions
    double avgRotorR = 0, avgStatorR = 0;
    int nRotor = 0, nStator = 0;
    for (const auto &blk : doc->blockLabels) {
        if (blk.blockType == "<No Mesh>") continue;
        double r = std::sqrt((blk.x - cx) * (blk.x - cx) +
                             (blk.y - cy) * (blk.y - cy));
        if (blk.inGroup == movingGroup) {
            avgRotorR += r; nRotor++;
        } else {
            avgStatorR += r; nStator++;
        }
    }
    if (nRotor == 0 || nStator == 0) {
        m_lastError = "Cannot find rotor and stator block labels";
        return false;
    }
    avgRotorR /= nRotor;
    avgStatorR /= nStator;
    bool rotorInside = (avgRotorR < avgStatorR);

    // --- Detect actual airgap from GEOMETRY nodes ---
    // Geometry nodes on arcs/segments define the iron surfaces.
    // Classify each node by the group of its connected arcs/segments,
    // then find the radial gap between rotor and stator geometry.
    //
    // For each node, determine if it belongs to rotor or stator by checking
    // the inGroup of arcs and segments that reference it.
    std::vector<int> nodeGroup(doc->nodes.size(), -1);  // -1 = unknown
    for (const auto &arc : doc->arcSegments) {
        if (arc.inGroup == movingGroup) {
            nodeGroup[arc.n0] = movingGroup;
            nodeGroup[arc.n1] = movingGroup;
        } else if (arc.inGroup != 0 || nodeGroup[arc.n0] < 0) {
            // Only override if not already set to movingGroup
            if (nodeGroup[arc.n0] != movingGroup) nodeGroup[arc.n0] = arc.inGroup;
            if (nodeGroup[arc.n1] != movingGroup) nodeGroup[arc.n1] = arc.inGroup;
        }
    }
    for (const auto &seg : doc->segments) {
        if (seg.inGroup == movingGroup) {
            nodeGroup[seg.n0] = movingGroup;
            nodeGroup[seg.n1] = movingGroup;
        } else if (seg.inGroup != 0 || nodeGroup[seg.n0] < 0) {
            if (nodeGroup[seg.n0] != movingGroup) nodeGroup[seg.n0] = seg.inGroup;
            if (nodeGroup[seg.n1] != movingGroup) nodeGroup[seg.n1] = seg.inGroup;
        }
    }

    // Find the actual iron surface radii: the closest rotor geometry to
    // the stator, and the closest stator geometry to the rotor.
    double rotorSurfaceR = -1;   // the rotor surface facing the airgap
    double statorSurfaceR = -1;  // the stator surface facing the airgap
    for (int i = 0; i < (int)doc->nodes.size(); i++) {
        double r = std::sqrt((doc->nodes[i].x - cx) * (doc->nodes[i].x - cx) +
                             (doc->nodes[i].y - cy) * (doc->nodes[i].y - cy));
        if (r < 1e-9) continue;  // skip center node

        if (nodeGroup[i] == movingGroup) {
            // Rotor node: find the surface closest to the stator
            if (rotorInside) {
                // Inner rotor: rotor surface = max rotor radius
                if (r > rotorSurfaceR) rotorSurfaceR = r;
            } else {
                // Outer rotor: rotor surface = min rotor radius
                if (rotorSurfaceR < 0 || r < rotorSurfaceR) rotorSurfaceR = r;
            }
        } else if (nodeGroup[i] >= 0) {
            // Stator node: find the surface closest to the rotor
            if (rotorInside) {
                // Inner rotor: stator surface = min stator radius
                if (statorSurfaceR < 0 || r < statorSurfaceR) statorSurfaceR = r;
            } else {
                // Outer rotor: stator surface = max stator radius
                if (r > statorSurfaceR) statorSurfaceR = r;
            }
        }
    }

    // Determine the actual airgap bounds
    double gapInner, gapOuter;
    if (rotorInside) {
        gapInner = rotorSurfaceR;   // inner rotor outer surface
        gapOuter = statorSurfaceR;  // stator inner surface
    } else {
        gapInner = statorSurfaceR;  // stator outer surface
        gapOuter = rotorSurfaceR;   // outer rotor inner surface
    }

    if (gapInner <= 0 || gapOuter <= 0 || gapOuter <= gapInner) {
        // Geometry detection failed — fall back to block-label gap detection,
        // but still keep the user-requested band radii instead of silently
        // replacing them with an auto-generated band.
        emit progress("  Could not detect airgap from geometry — validating against block-label gap...");

        std::vector<double> rotorRadii, statorRadii;
        for (const auto &blk : doc->blockLabels) {
            if (blk.blockType == "<No Mesh>") continue;
            double r = std::sqrt((blk.x - cx) * (blk.x - cx) +
                                 (blk.y - cy) * (blk.y - cy));
            if (blk.inGroup == movingGroup)
                rotorRadii.push_back(r);
            else
                statorRadii.push_back(r);
        }

        struct LabelInfo { double r; bool isRotor; };
        std::vector<LabelInfo> allLabels;
        for (double r : rotorRadii) allLabels.push_back({r, true});
        for (double r : statorRadii) allLabels.push_back({r, false});
        std::sort(allLabels.begin(), allLabels.end(),
                  [](const LabelInfo &a, const LabelInfo &b) { return a.r < b.r; });

        double bestGapLow = 0.0, bestGapHigh = 0.0, bestGapWidth = 1e30;
        bool bestRotorInside = rotorInside;
        for (int i = 1; i < (int)allLabels.size(); i++) {
            if (allLabels[i].isRotor != allLabels[i - 1].isRotor) {
                double w = allLabels[i].r - allLabels[i - 1].r;
                if (w > 1e-9 && w < bestGapWidth) {
                    bestGapWidth = w;
                    bestGapLow = allLabels[i - 1].r;
                    bestGapHigh = allLabels[i].r;
                    bestRotorInside = allLabels[i - 1].isRotor;
                }
            }
        }

        if (bestGapWidth >= 1e30) {
            m_lastError = "No clear radial gap found between rotor and stator";
            return false;
        }

        gapInner = bestGapLow;
        gapOuter = bestGapHigh;
        rotorInside = bestRotorInside;
    }

    double actualGap = gapOuter - gapInner;
    emit progress(QString("  Airgap from geometry: %1 to %2 (width=%3, rotorInside=%4)")
                  .arg(gapInner, 0, 'f', 4).arg(gapOuter, 0, 'f', 4)
                  .arg(actualGap, 0, 'f', 4).arg(rotorInside));

    // Validate the user-specified band stays inside the detected airgap.
    if (innerR <= gapInner || outerR >= gapOuter || outerR <= innerR) {
        m_lastError = QString("Sliding band radii must satisfy %1 < inner < outer < %2")
                          .arg(gapInner, 0, 'f', 4)
                          .arg(gapOuter, 0, 'f', 4);
        return false;
    }

    double midR = (innerR + outerR) / 2.0;
    double circumference = 2.0 * M_PI * midR;
    double bandWidth = outerR - innerR;

    // Choose interface density from the physical airgap, not the narrow
    // remesh strip width, so manual thin bands do not explode node counts.
    int N = std::max(72, std::min(360, (int)std::round(circumference / (actualGap * 0.5))));
    N = ((N + 3) / 4) * 4;

    // Update the document so the UI circles match the validated values.
    doc->slidingBandInnerRadius = innerR;
    doc->slidingBandOuterRadius = outerR;

    emit progress(QString("  Band validated: %1 → %2 (width=%3, %4%% of airgap)")
                  .arg(innerR, 0, 'f', 4).arg(outerR, 0, 'f', 4)
                  .arg(bandWidth, 0, 'f', 4)
                  .arg(bandWidth / actualGap * 100.0, 0, 'f', 1));

    // Find air block label in the airgap
    int airLabel = -1;
    int ri = 0;
    for (int i = 0; i < (int)doc->blockLabels.size(); i++) {
        const auto &blk = doc->blockLabels[i];
        if (blk.blockType == "<No Mesh>") continue;
        double r = std::sqrt((blk.x - cx) * (blk.x - cx) +
                             (blk.y - cy) * (blk.y - cy));
        // Look for air label anywhere in the actual airgap (not just the band)
        if (r > gapInner && r < gapOuter) {
            airLabel = ri + 1;  // 1-based Triangle region attribute
            break;
        }
        ri++;
    }

    // Fallback: find first air-like material near the airgap
    if (airLabel < 0) {
        ri = 0;
        for (int i = 0; i < (int)doc->blockLabels.size(); i++) {
            const auto &blk = doc->blockLabels[i];
            if (blk.blockType == "<No Mesh>") continue;
            double r = std::sqrt((blk.x - cx) * (blk.x - cx) +
                                 (blk.y - cy) * (blk.y - cy));
            if (std::fabs(r - midR) < actualGap) {
                int matIdx = -1;
                for (int m = 0; m < (int)doc->materialProps.size(); m++) {
                    if (doc->materialProps[m].blockName == blk.blockType) {
                        matIdx = m; break;
                    }
                }
                if (matIdx >= 0) {
                    const auto &mat = doc->materialProps[matIdx];
                    bool isAir = (mat.mu_x == 1.0 && mat.mu_y == 1.0 &&
                                  mat.H_c == 0.0 && mat.bhPoints == 0 &&
                                  mat.Jsrc.abs() < 1e-12 && mat.Cduct < 1e-12);
                    if (isAir) { airLabel = ri + 1; break; }
                }
            }
            ri++;
        }
    }

    if (airLabel < 0) {
        m_lastError = "No air block label found in the airgap region";
        return false;
    }

    band.active = true;
    band.isRotation = true;
    band.cx = cx;
    band.cy = cy;
    band.innerRadius = innerR;
    band.outerRadius = outerR;
    band.rotorIsInside = rotorInside;
    band.airBlockLabel = airLabel;
    band.numInterfaceNodes = N;
    band.movingGroup = movingGroup;
    band.cumulativeAngle = 0.0;

    emit progress(QString("Sliding band: innerR=%1, outerR=%2, N=%3, airLabel=%4, rotorInside=%5")
                  .arg(innerR, 0, 'f', 4).arg(outerR, 0, 'f', 4).arg(N).arg(airLabel).arg(rotorInside));
    return true;
}

// ---------------------------------------------------------------
// Sliding band — airgap detection (automatic)
// ---------------------------------------------------------------

#undef M_PI
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool MeshGenerator::detectAirgap(FemmeDocument *doc, const MotionConfig &config,
                                  SlidingBand &band)
{
    if (!doc || !config.isRotation) {
        m_lastError = "Sliding band currently only supports rotary motion";
        return false;
    }

    const double cx = config.cx;
    const double cy = config.cy;
    const int movingGroup = config.groupNumber;

    // Collect radii of rotor and stator block labels
    std::vector<double> rotorRadii, statorRadii;
    for (const auto &blk : doc->blockLabels) {
        if (blk.blockType == "<No Mesh>") continue;
        double r = std::sqrt((blk.x - cx) * (blk.x - cx) +
                             (blk.y - cy) * (blk.y - cy));
        if (blk.inGroup == movingGroup)
            rotorRadii.push_back(r);
        else
            statorRadii.push_back(r);
    }
    if (rotorRadii.empty()) {
        m_lastError = "No rotor block labels found for the moving group";
        return false;
    }
    if (statorRadii.empty()) {
        m_lastError = "No stator block labels found";
        return false;
    }

    // Sort radii for analysis
    std::sort(rotorRadii.begin(), rotorRadii.end());
    std::sort(statorRadii.begin(), statorRadii.end());

    emit progress(QString("  Rotor radii (%1 labels): min=%2, max=%3")
        .arg(rotorRadii.size())
        .arg(rotorRadii.front(), 0, 'f', 2)
        .arg(rotorRadii.back(), 0, 'f', 2));
    emit progress(QString("  Stator radii (%1 labels): min=%2, max=%3")
        .arg(statorRadii.size())
        .arg(statorRadii.front(), 0, 'f', 2)
        .arg(statorRadii.back(), 0, 'f', 2));

    // Find the airgap by looking for the radial region where rotor and
    // stator labels are closest neighbours.  Sort ALL labels by radius
    // and find the closest rotor↔stator boundary.
    struct LabelInfo { double r; bool isRotor; };
    std::vector<LabelInfo> allLabels;
    for (double r : rotorRadii) allLabels.push_back({r, true});
    for (double r : statorRadii) allLabels.push_back({r, false});
    std::sort(allLabels.begin(), allLabels.end(),
              [](const LabelInfo &a, const LabelInfo &b) { return a.r < b.r; });

    // Walk through the sorted list and find transitions between rotor↔stator.
    // The airgap is the smallest such transition gap.
    double bestGapLow = 0, bestGapHigh = 0, bestGapWidth = 1e30;
    bool bestRotorIsInside = true;
    for (int i = 1; i < (int)allLabels.size(); i++) {
        if (allLabels[i].isRotor != allLabels[i-1].isRotor) {
            double w = allLabels[i].r - allLabels[i-1].r;
            if (w > 1e-9 && w < bestGapWidth) {
                bestGapWidth = w;
                bestGapLow = allLabels[i-1].r;
                bestGapHigh = allLabels[i].r;
                // If the inner label is rotor, rotor is inside
                bestRotorIsInside = allLabels[i-1].isRotor;
            }
        }
    }

    if (bestGapWidth >= 1e30) {
        m_lastError = "No clear radial gap found between rotor and stator";
        return false;
    }

    double gapLow = bestGapLow;
    double gapHigh = bestGapHigh;
    double gapWidth = bestGapWidth;

    emit progress(QString("  Best rotor↔stator gap: %1 to %2 (width=%3), rotorInside=%4")
        .arg(gapLow, 0, 'f', 3).arg(gapHigh, 0, 'f', 3)
        .arg(gapWidth, 0, 'f', 3).arg(bestRotorIsInside));

    if (gapWidth < 1e-9) {
        m_lastError = "Airgap too narrow for sliding band";
        return false;
    }

    // Find a block label in the gap region (should be air)
    int airLabel = -1;  // 1-based
    int ri = 0;  // filtered (non-hole) index
    for (int i = 0; i < (int)doc->blockLabels.size(); i++) {
        const auto &blk = doc->blockLabels[i];
        if (blk.blockType == "<No Mesh>") continue;
        double r = std::sqrt((blk.x - cx) * (blk.x - cx) +
                             (blk.y - cy) * (blk.y - cy));
        if (r > gapLow && r < gapHigh) {
            airLabel = ri + 1;  // 1-based Triangle region attribute
            break;
        }
        ri++;
    }

    // If no block label in the gap, use the first "Air" material label as fallback
    if (airLabel < 0) {
        ri = 0;
        for (int i = 0; i < (int)doc->blockLabels.size(); i++) {
            const auto &blk = doc->blockLabels[i];
            if (blk.blockType == "<No Mesh>") continue;
            // Check if this is an air-like material
            int matIdx = -1;
            for (int m = 0; m < (int)doc->materialProps.size(); m++) {
                if (doc->materialProps[m].blockName == blk.blockType) {
                    matIdx = m;
                    break;
                }
            }
            if (matIdx >= 0) {
                const auto &mat = doc->materialProps[matIdx];
                bool isAir = (mat.mu_x == 1.0 && mat.mu_y == 1.0 &&
                              mat.H_c == 0.0 && mat.bhPoints == 0 &&
                              mat.Jsrc.abs() < 1e-12 && mat.Cduct < 1e-12);
                if (isAir) {
                    airLabel = ri + 1;
                    break;
                }
            }
            ri++;
        }
    }

    if (airLabel < 0) {
        m_lastError = "No air block label found for the airgap region";
        return false;
    }

    // Place a thin band centered in the gap (conceptually a single cut)
    double midR = (gapLow + gapHigh) / 2.0;
    double circumference = 2.0 * M_PI * midR;

    // N = number of interface nodes per circle, capped at [72, 360]
    int N = std::max(72, std::min(360, (int)std::round(circumference / (gapWidth * 0.5))));
    N = ((N + 3) / 4) * 4;  // round to multiple of 4

    double arcSeg = circumference / N;
    // Band width: thin cut — enough for good triangles, no wider than 1/3 of gap
    double bandWidth = std::min(arcSeg * 0.5, gapWidth / 3.0);
    bandWidth = std::max(bandWidth, gapWidth * 0.05);  // minimum 5% of gap

    double innerR = midR - bandWidth / 2.0;
    double outerR = midR + bandWidth / 2.0;

    // Safety: stay inside the gap
    double safeMargin = gapWidth * 0.05;
    if (innerR < gapLow + safeMargin) innerR = gapLow + safeMargin;
    if (outerR > gapHigh - safeMargin) outerR = gapHigh - safeMargin;

    // Rotor inside/outside was determined during gap detection above
    bool rotorInside = bestRotorIsInside;

    band.active = true;
    band.isRotation = true;
    band.cx = cx;
    band.cy = cy;
    band.innerRadius = innerR;
    band.outerRadius = outerR;
    band.rotorIsInside = rotorInside;
    band.airBlockLabel = airLabel;
    band.numInterfaceNodes = N;
    band.movingGroup = movingGroup;
    band.cumulativeAngle = 0.0;

    emit progress(QString("Airgap detected: gapLow=%1, gapHigh=%2, gap=%3, innerR=%4, outerR=%5, N=%6, airLabel=%7")
                  .arg(gapLow, 0, 'f', 4).arg(gapHigh, 0, 'f', 4)
                  .arg(gapWidth, 0, 'f', 4)
                  .arg(innerR, 0, 'f', 4).arg(outerR, 0, 'f', 4).arg(N).arg(airLabel));
    return true;
}

// ---------------------------------------------------------------
// Sliding band — mesh classification
// ---------------------------------------------------------------

bool MeshGenerator::classifyMeshForSlidingBand(FemmeDocument *doc, SlidingBand &band)
{
    if (!doc || !band.active) return false;

    const double cx = band.cx;
    const double cy = band.cy;
    const double rInner = band.innerRadius;
    const double rOuter = band.outerRadius;
    // Tolerance for fallback radial matching when explicit interface point
    // markers are unavailable. Keep this tight so ordinary airgap nodes do
    // not get mistaken for interface nodes.
    const double tol = std::max((rOuter - rInner) * 0.002, 1e-6);

    int numNodes = (int)doc->meshNodes.size();
    int numElems = (int)doc->meshElements.size();

    // Classify each node: 0=rotor, 1=inner_circle, 2=band_interior, 3=outer_circle, 4=stator
    enum NodeZone { NZ_ROTOR = 0, NZ_INNER = 1, NZ_BAND = 2, NZ_OUTER = 3, NZ_STATOR = 4 };
    std::vector<int> nodeZone(numNodes);

    struct AngleIdx { double angle; int nodeIdx; };
    std::vector<AngleIdx> innerCandidates, outerCandidates;

    for (int i = 0; i < numNodes; i++) {
        double dx = doc->meshNodes[i].x - cx;
        double dy = doc->meshNodes[i].y - cy;
        double r = std::sqrt(dx * dx + dy * dy);

        if (doc->meshNodes[i].boundaryMarker == kSlidingBandInnerPointMarker) {
            nodeZone[i] = NZ_INNER;
            double angle = std::atan2(dy, dx);
            if (angle < 0) angle += 2.0 * M_PI;
            innerCandidates.push_back({angle, i});
        } else if (doc->meshNodes[i].boundaryMarker == kSlidingBandOuterPointMarker) {
            nodeZone[i] = NZ_OUTER;
            double angle = std::atan2(dy, dx);
            if (angle < 0) angle += 2.0 * M_PI;
            outerCandidates.push_back({angle, i});
        } else if (std::fabs(r - rInner) < tol) {
            nodeZone[i] = NZ_INNER;
            double angle = std::atan2(dy, dx);
            if (angle < 0) angle += 2.0 * M_PI;
            innerCandidates.push_back({angle, i});
        } else if (std::fabs(r - rOuter) < tol) {
            nodeZone[i] = NZ_OUTER;
            double angle = std::atan2(dy, dx);
            if (angle < 0) angle += 2.0 * M_PI;
            outerCandidates.push_back({angle, i});
        } else if (r < rInner) {
            nodeZone[i] = NZ_ROTOR;
        } else if (r > rOuter) {
            nodeZone[i] = NZ_STATOR;
        } else {
            nodeZone[i] = NZ_BAND;
        }
    }

    // Sort interface nodes by angle
    auto angleCmp = [](const AngleIdx &a, const AngleIdx &b) { return a.angle < b.angle; };
    std::sort(innerCandidates.begin(), innerCandidates.end(), angleCmp);
    std::sort(outerCandidates.begin(), outerCandidates.end(), angleCmp);

    // Store interface node indices and angles
    band.innerCircleNodeIndices.clear();
    band.innerBaseAngles.clear();
    for (const auto &ai : innerCandidates) {
        band.innerCircleNodeIndices.push_back(ai.nodeIdx);
        band.innerBaseAngles.push_back(ai.angle);
    }
    band.outerCircleNodeIndices.clear();
    band.outerBaseAngles.clear();
    for (const auto &ai : outerCandidates) {
        band.outerCircleNodeIndices.push_back(ai.nodeIdx);
        band.outerBaseAngles.push_back(ai.angle);
    }

    if ((int)band.innerCircleNodeIndices.size() != band.numInterfaceNodes ||
        (int)band.outerCircleNodeIndices.size() != band.numInterfaceNodes) {
        emit progress(QString("  Sliding band interface count mismatch: expected %1, got inner=%2 outer=%3")
                      .arg(band.numInterfaceNodes)
                      .arg(band.innerCircleNodeIndices.size())
                      .arg(band.outerCircleNodeIndices.size()));
    }

    // Validate: need at least 8 nodes on each circle for a usable band
    if ((int)band.innerCircleNodeIndices.size() < 8) {
        emit progress(QString("Classification failed: only %1 nodes found on inner circle "
                              "(expected ~%2, tol=%3)")
                      .arg(band.innerCircleNodeIndices.size())
                      .arg(band.numInterfaceNodes)
                      .arg(tol, 0, 'g', 4));
        return false;
    }
    if ((int)band.outerCircleNodeIndices.size() < 8) {
        emit progress(QString("Classification failed: only %1 nodes found on outer circle "
                              "(expected ~%2, tol=%3)")
                      .arg(band.outerCircleNodeIndices.size())
                      .arg(band.numInterfaceNodes)
                      .arg(tol, 0, 'g', 4));
        return false;
    }

    // Store rotor node indices (for rotation).
    // For inner rotor: nodes inside inner circle rotate.
    // For outer rotor: nodes outside outer circle rotate.
    band.rotorNodeIndices.clear();
    for (int i = 0; i < numNodes; i++) {
        if (band.rotorIsInside && nodeZone[i] == NZ_ROTOR)
            band.rotorNodeIndices.push_back(i);
        else if (!band.rotorIsInside && nodeZone[i] == NZ_STATOR)
            band.rotorNodeIndices.push_back(i);
    }

    // Classify elements: moving (rotor), fixed (stator), or band.
    // For inner rotor: inner side = moving, outer side = fixed.
    // For outer rotor: outer side = moving, inner side = fixed.
    band.rotorElementIndices.clear();
    band.statorElementIndices.clear();
    std::vector<int> bandElemIndices;

    for (int e = 0; e < numElems; e++) {
        const auto &el = doc->meshElements[e];
        int z0 = nodeZone[el.p[0]], z1 = nodeZone[el.p[1]], z2 = nodeZone[el.p[2]];

        bool allInnerSide = (z0 <= NZ_INNER) && (z1 <= NZ_INNER) && (z2 <= NZ_INNER);
        bool allOuterSide = (z0 >= NZ_OUTER) && (z1 >= NZ_OUTER) && (z2 >= NZ_OUTER);

        if (band.rotorIsInside) {
            if (allInnerSide)
                band.rotorElementIndices.push_back(e);
            else if (allOuterSide)
                band.statorElementIndices.push_back(e);
            else
                bandElemIndices.push_back(e);
        } else {
            // Outer rotor: outer side moves, inner side fixed
            if (allOuterSide)
                band.rotorElementIndices.push_back(e);
            else if (allInnerSide)
                band.statorElementIndices.push_back(e);
            else
                bandElemIndices.push_back(e);
        }
    }

    // Reorder meshElements so band elements are at the end (contiguous block)
    std::vector<FMeshElement> reordered;
    reordered.reserve(numElems);

    // Build old→new index mapping for elements
    // First: rotor elements, then stator elements, then band elements
    for (int idx : band.rotorElementIndices)
        reordered.push_back(doc->meshElements[idx]);
    for (int idx : band.statorElementIndices)
        reordered.push_back(doc->meshElements[idx]);

    band.bandElementStart = (int)reordered.size();
    for (int idx : bandElemIndices)
        reordered.push_back(doc->meshElements[idx]);
    band.bandElementCount = (int)bandElemIndices.size();

    doc->meshElements = std::move(reordered);

    // Update element indices in the band to reflect new ordering
    band.rotorElementIndices.clear();
    for (int i = 0; i < band.bandElementStart - (int)band.statorElementIndices.size(); i++)
        band.rotorElementIndices.push_back(i);
    int statorStart = (int)band.rotorElementIndices.size();
    band.statorElementIndices.clear();
    for (int i = statorStart; i < band.bandElementStart; i++)
        band.statorElementIndices.push_back(i);

    // Classify edges: fixed (rotor + stator) vs band
    band.fixedEdges.clear();
    std::vector<MeshEdge> bandEdgesTemp;
    for (const auto &edge : doc->meshEdges) {
        int z0 = nodeZone[edge.n0], z1 = nodeZone[edge.n1];
        bool bothRotorSide = (z0 <= NZ_INNER) && (z1 <= NZ_INNER);
        bool bothStatorSide = (z0 >= NZ_OUTER) && (z1 >= NZ_OUTER);
        if (bothRotorSide || bothStatorSide)
            band.fixedEdges.push_back(edge);
        // Band edges will be regenerated by remeshBand()
    }

    emit progress(QString("Sliding band classified: %1 rotor nodes, %2 inner circle, "
                          "%3 outer circle, %4 rotor elems, %5 stator elems, %6 band elems")
                  .arg(band.rotorNodeIndices.size())
                  .arg(band.innerCircleNodeIndices.size())
                  .arg(band.outerCircleNodeIndices.size())
                  .arg(band.rotorElementIndices.size())
                  .arg(band.statorElementIndices.size())
                  .arg(band.bandElementCount));

    // --- Compact mesh: remove band-interior nodes ---
    // Triangle can place additional nodes inside the airgap strip. When the
    // zipper replaces the original band elements, any nodes referenced only
    // by those original band elements become orphaned, creating zero diagonal
    // entries in the stiffness matrix. Remove them now.
    //
    // "Keep" nodes: any node referenced by rotor or stator elements,
    //               plus all circle nodes.

    int oldNumNodes = (int)doc->meshNodes.size();
    std::vector<bool> keepNode(oldNumNodes, false);

    // Mark circle nodes as kept
    for (int ni : band.innerCircleNodeIndices) keepNode[ni] = true;
    for (int ni : band.outerCircleNodeIndices) keepNode[ni] = true;

    // Mark nodes referenced by non-band elements (rotor + stator)
    for (int e = 0; e < band.bandElementStart; e++) {
        const auto &el = doc->meshElements[e];
        for (int k = 0; k < 3; k++)
            if (el.p[k] >= 0 && el.p[k] < oldNumNodes)
                keepNode[el.p[k]] = true;
    }

    // Build old→new index mapping
    std::vector<int> remap(oldNumNodes, -1);
    int newCount = 0;
    for (int i = 0; i < oldNumNodes; i++) {
        if (keepNode[i])
            remap[i] = newCount++;
    }
    int removed = oldNumNodes - newCount;

    if (removed > 0) {
        // Compact meshNodes
        std::vector<FMeshNode> compacted(newCount);
        for (int i = 0; i < oldNumNodes; i++)
            if (remap[i] >= 0) compacted[remap[i]] = doc->meshNodes[i];
        doc->meshNodes = std::move(compacted);

        // Remap element node indices
        for (auto &el : doc->meshElements)
            for (int k = 0; k < 3; k++)
                el.p[k] = remap[el.p[k]];

        // Remap band node index vectors
        for (auto &ni : band.rotorNodeIndices) ni = remap[ni];
        for (auto &ni : band.innerCircleNodeIndices) ni = remap[ni];
        for (auto &ni : band.outerCircleNodeIndices) ni = remap[ni];

        // Remap base angles (same order, just indices changed)
        // No change needed — angles are stored separately from indices

        // Remap fixed edges
        for (auto &e : band.fixedEdges) {
            e.n0 = remap[e.n0];
            e.n1 = remap[e.n1];
        }
        // Remove any edges that reference removed nodes
        band.fixedEdges.erase(
            std::remove_if(band.fixedEdges.begin(), band.fixedEdges.end(),
                           [](const MeshEdge &e) { return e.n0 < 0 || e.n1 < 0; }),
            band.fixedEdges.end());

        emit progress(QString("  Compacted mesh: removed %1 orphaned band-interior nodes (%2 → %3)")
                      .arg(removed).arg(oldNumNodes).arg(newCount));
    }

    return true;
}

// ---------------------------------------------------------------
// Sliding band — band re-triangulation (zipper algorithm)
// ---------------------------------------------------------------

void MeshGenerator::remeshBand(FemmeDocument *doc, SlidingBand &band,
                                std::vector<MeshEdge> &edgesOut)
{
    if (!doc || !band.active) return;

    const int nInner = (int)band.innerCircleNodeIndices.size();
    const int nOuter = (int)band.outerCircleNodeIndices.size();
    if (nInner == 0 || nOuter == 0) return;

    // Compute current angles of both circle node sets. For outer-rotor
    // machines the outer ring moves; for inner-rotor machines the inner
    // ring moves. Reading the current mesh avoids hard-coding one case.
    std::vector<double> innerAngles(nInner);
    for (int i = 0; i < nInner; i++) {
        int ni = band.innerCircleNodeIndices[i];
        double dx = doc->meshNodes[ni].x - band.cx;
        double dy = doc->meshNodes[ni].y - band.cy;
        double a = std::atan2(dy, dx);
        if (a < 0) a += 2.0 * M_PI;
        innerAngles[i] = a;
    }

    std::vector<double> outerAngles(nOuter);
    for (int i = 0; i < nOuter; i++) {
        int ni = band.outerCircleNodeIndices[i];
        double dx = doc->meshNodes[ni].x - band.cx;
        double dy = doc->meshNodes[ni].y - band.cy;
        double a = std::atan2(dy, dx);
        if (a < 0) a += 2.0 * M_PI;
        outerAngles[i] = a;
    }

    // Merge both sets into angle-sorted list
    struct MergedNode {
        double angle;
        int meshNodeIdx;
        bool isInner;  // true=inner circle, false=outer circle
    };
    std::vector<MergedNode> merged;
    merged.reserve(nInner + nOuter);
    for (int i = 0; i < nInner; i++)
        merged.push_back({innerAngles[i], band.innerCircleNodeIndices[i], true});
    for (int i = 0; i < nOuter; i++)
        merged.push_back({outerAngles[i], band.outerCircleNodeIndices[i], false});

    std::sort(merged.begin(), merged.end(),
              [](const MergedNode &a, const MergedNode &b) { return a.angle < b.angle; });

    // Zipper triangulation: walk the merged list, connecting alternating
    // inner/outer nodes into triangles that fill the annular band.
    //
    // Algorithm: maintain pointers to the last-seen inner and outer node.
    // For each node in the merged list, if it's inner, form a triangle with
    // the previous outer node and the next outer node (or vice versa).
    // Simpler: treat consecutive same-side nodes as needing a triangle
    // with the most recent opposite-side node.

    std::vector<FMeshElement> bandElems;
    bandElems.reserve(nInner + nOuter);

    int lastInnerIdx = -1, lastOuterIdx = -1;
    int firstInnerMesh = -1, firstOuterMesh = -1;

    // Walk the merged list and form triangles
    for (int k = 0; k < (int)merged.size(); k++) {
        const auto &cur = merged[k];
        if (cur.isInner) {
            if (lastInnerIdx >= 0 && lastOuterIdx >= 0) {
                // Triangle: lastInner, cur, lastOuter
                FMeshElement el;
                el.p[0] = merged[lastInnerIdx].meshNodeIdx;
                el.p[1] = cur.meshNodeIdx;
                el.p[2] = merged[lastOuterIdx].meshNodeIdx;
                el.label = band.airBlockLabel;
                bandElems.push_back(el);
            }
            if (firstInnerMesh < 0) firstInnerMesh = k;
            lastInnerIdx = k;
        } else {
            if (lastOuterIdx >= 0 && lastInnerIdx >= 0) {
                // Triangle: lastOuter, cur, lastInner
                FMeshElement el;
                el.p[0] = merged[lastOuterIdx].meshNodeIdx;
                el.p[1] = cur.meshNodeIdx;
                el.p[2] = merged[lastInnerIdx].meshNodeIdx;
                el.label = band.airBlockLabel;
                bandElems.push_back(el);
            }
            if (firstOuterMesh < 0) firstOuterMesh = k;
            lastOuterIdx = k;
        }
    }

    // Close the loop: connect last nodes back to first nodes
    if (lastInnerIdx >= 0 && firstOuterMesh >= 0) {
        // Last inner → first inner, with last outer
        FMeshElement el;
        el.p[0] = merged[lastInnerIdx].meshNodeIdx;
        el.p[1] = merged[firstInnerMesh].meshNodeIdx;
        el.p[2] = merged[lastOuterIdx].meshNodeIdx;
        el.label = band.airBlockLabel;
        bandElems.push_back(el);
    }
    if (lastOuterIdx >= 0 && firstInnerMesh >= 0) {
        // Last outer → first outer, with last inner
        FMeshElement el;
        el.p[0] = merged[lastOuterIdx].meshNodeIdx;
        el.p[1] = merged[firstOuterMesh].meshNodeIdx;
        el.p[2] = merged[lastInnerIdx].meshNodeIdx;
        el.label = band.airBlockLabel;
        bandElems.push_back(el);
    }

    // Write band elements into doc->meshElements
    // Resize if needed (band element count may differ slightly from initial)
    int newBandCount = (int)bandElems.size();
    int totalFixed = band.bandElementStart;
    doc->meshElements.resize(totalFixed + newBandCount);
    for (int i = 0; i < newBandCount; i++)
        doc->meshElements[totalFixed + i] = bandElems[i];
    band.bandElementCount = newBandCount;

    // Assemble full edge list: fixed edges + band edges
    // Band edges are all internal (marker=0) since the airgap is air with no BCs
    edgesOut = band.fixedEdges;

    // Add edges from band triangles — must deduplicate!
    // CuthillFromMemory allocates buffers sized for 2*numEdges, so duplicates
    // would overflow that buffer and corrupt memory.
    std::set<std::pair<int,int>> bandEdgeSet;
    for (const auto &el : bandElems) {
        for (int e = 0; e < 3; e++) {
            int n0 = el.p[e], n1 = el.p[(e + 1) % 3];
            if (n0 > n1) std::swap(n0, n1);
            bandEdgeSet.insert({n0, n1});
        }
    }
    for (const auto &ep : bandEdgeSet) {
        MeshEdge me;
        me.n0 = ep.first;
        me.n1 = ep.second;
        me.marker = 0;
        edgesOut.push_back(me);
    }
}
