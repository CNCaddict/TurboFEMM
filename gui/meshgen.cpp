// FEMM Qt 6 GUI — Mesh generation implementation
#include "meshgen.h"
#include "document.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <cmath>
#include <cstdlib>
#include <cstring>

// Triangle library API (compiled with TRILIBRARY)
extern "C" {
#include "triangle.h"
void trifree(VOID *memptr);
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

bool MeshGenerator::generateMeshInProcess(FemmeDocument *doc, std::vector<MeshEdge> &edgesOut)
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
    //  a=area constraints, z=zero-indexed, Q=quiet)
    char switches[] = "pPq30eAazQ";
    triangulate(switches, &in, &out, NULL);

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
