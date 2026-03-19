// FEMM Qt 6 GUI — Post-processor results document implementation
#include "resultsdoc.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <cmath>
#include <cstdio>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------
// Length conversion factors (to meters)
// ---------------------------------------------------------------
static double lengthConversion(int units)
{
    switch (units) {
    case 0: return 0.0254;       // inches
    case 1: return 0.001;        // millimeters
    case 2: return 0.01;         // centimeters
    case 3: return 1.0;          // meters
    case 4: return 2.54e-5;      // mils
    case 5: return 1.0e-6;       // microns
    default: return 0.0254;
    }
}

// ---------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------

ResultsDocument::ResultsDocument(QObject *parent)
    : QObject(parent)
{
}

ResultsDocument::~ResultsDocument() = default;

// ---------------------------------------------------------------
// .ans file loader
// ---------------------------------------------------------------

bool ResultsDocument::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    m_filePath = path;

    // Helper to strip tag keys
    auto stripKey = [](const QString &line) -> QString {
        int eq = line.indexOf('=');
        if (eq >= 0) return line.mid(eq + 1).trimmed();
        return line.trimmed();
    };

    // Phase 1: Parse header
    QString line;
    int numPointProps = 0, numBoundaryProps = 0, numBlockProps = 0, numCircuitProps = 0;

    while (!in.atEnd()) {
        line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        QString lower = line.toLower();

        if (lower.startsWith("[format]")) {
            // skip version check
        }
        else if (lower.startsWith("[frequency]")) {
            frequency = stripKey(line).toDouble();
        }
        else if (lower.startsWith("[depth]")) {
            depth = stripKey(line).toDouble();
        }
        else if (lower.startsWith("[lengthunits]")) {
            QString u = stripKey(line).toLower().trimmed();
            // Handle both integer codes and string names
            bool isInt = false;
            int code = u.toInt(&isInt);
            if (isInt && u.length() <= 2) {
                lengthUnits = code;
            } else {
                // Parse string names (same as document.cpp)
                if (u.startsWith("inches"))            lengthUnits = 0;
                else if (u.startsWith("millimeters"))  lengthUnits = 1;
                else if (u.startsWith("centimeters"))  lengthUnits = 2;
                else if (u.startsWith("meters"))       lengthUnits = 3;
                else if (u.startsWith("mils"))         lengthUnits = 4;
                else if (u.startsWith("microns"))      lengthUnits = 5;
                else lengthUnits = 0;  // default to inches
            }
            lengthConv = lengthConversion(lengthUnits);
        }
        else if (lower.startsWith("[problemtype]")) {
            QString val = stripKey(line).toLower();
            problemType = val.startsWith("axi") ? 1 : 0;
        }
        else if (lower.startsWith("[numpointprops]")) {
            numPointProps = stripKey(line).toInt();
        }
        else if (lower.startsWith("[numboundaryprops]") || lower.startsWith("[numlineprops]")) {
            numBoundaryProps = stripKey(line).toInt();
        }
        else if (lower.startsWith("[numblockprops]")) {
            numBlockProps = stripKey(line).toInt();
        }
        else if (lower.startsWith("[numcircuitprops]") || lower.startsWith("[numcircprops]")) {
            numCircuitProps = stripKey(line).toInt();
        }
        else if (lower.startsWith("<beginpoint>")) {
            // Read point properties (skip for now — rarely used in post-proc)
            for (int i = 0; i < numPointProps; i++) {
                while (!in.atEnd()) {
                    line = in.readLine().trimmed();
                    if (line.toLower().startsWith("<endpoint>")) break;
                    if (line.toLower().startsWith("<beginpoint>")) continue;
                }
            }
        }
        else if (lower.startsWith("<beginbdry>")) {
            // Skip boundary properties
            for (int i = 0; i < numBoundaryProps; i++) {
                while (!in.atEnd()) {
                    line = in.readLine().trimmed();
                    if (line.toLower().startsWith("<endbdry>")) break;
                }
            }
        }
        else if (lower.startsWith("<beginblock>")) {
            // Read material/block properties
            while (!in.atEnd()) {
                line = in.readLine().trimmed();
                if (line.toLower().startsWith("<endblock>")) break;

                SolnMaterial mat;
                // Parse block property tags
                while (!in.atEnd() && !line.toLower().startsWith("<endblock>")) {
                    QString ll = line.toLower();
                    if (ll.startsWith("<blockname>"))
                        mat.blockName = stripKey(line);
                    else if (ll.startsWith("<mu_x>"))
                        mat.mu_x = stripKey(line).toDouble();
                    else if (ll.startsWith("<mu_y>"))
                        mat.mu_y = stripKey(line).toDouble();
                    else if (ll.startsWith("<h_c>"))
                        mat.H_c = stripKey(line).toDouble();
                    else if (ll.startsWith("<sigma>") || ll.startsWith("<cduct>"))
                        mat.Cduct = stripKey(line).toDouble();
                    else if (ll.startsWith("<lamtype>"))
                        mat.lamType = stripKey(line).toInt();
                    else if (ll.startsWith("<lamfill>"))
                        mat.lamFill = stripKey(line).toDouble();
                    else if (ll.startsWith("<lam_d>"))
                        mat.Lam_d = stripKey(line).toDouble();
                    else if (ll.startsWith("<bhpoints>")) {
                        mat.bhPoints = stripKey(line).toInt();
                        mat.Bdata.resize(mat.bhPoints);
                        mat.Hdata.resize(mat.bhPoints);
                        for (int j = 0; j < mat.bhPoints; j++) {
                            line = in.readLine().trimmed();
                            QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                            if (parts.size() >= 2) {
                                mat.Bdata[j] = parts[0].toDouble();
                                mat.Hdata[j] = CmplxF(parts[1].toDouble(), 0.0);
                            }
                        }
                    }
                    else if (ll.startsWith("<endblock>"))
                        break;

                    line = in.readLine().trimmed();
                }
                materials.push_back(mat);
                if (line.toLower().startsWith("<endblock>")) break;
            }
        }
        else if (lower.startsWith("<begincircuit>")) {
            // Skip circuit properties in .ans (we read them differently)
            for (int i = 0; i < numCircuitProps; i++) {
                while (!in.atEnd()) {
                    line = in.readLine().trimmed();
                    if (line.toLower().startsWith("<endcircuit>")) break;
                }
            }
        }
        else if (lower.startsWith("[numpoints]")) {
            int n = stripKey(line).toInt();
            // Skip geometry points (pre-processor data, not mesh)
            for (int i = 0; i < n; i++) in.readLine();
        }
        else if (lower.startsWith("[numsegments]")) {
            int n = stripKey(line).toInt();
            for (int i = 0; i < n; i++) in.readLine();
        }
        else if (lower.startsWith("[numarcsegments]")) {
            int n = stripKey(line).toInt();
            for (int i = 0; i < n; i++) in.readLine();
        }
        else if (lower.startsWith("[numholes]")) {
            int n = stripKey(line).toInt();
            for (int i = 0; i < n; i++) in.readLine();
        }
        else if (lower.startsWith("[numblocklabels]")) {
            int n = stripKey(line).toInt();
            labels.resize(n);
            for (int i = 0; i < n; i++) {
                line = in.readLine().trimmed();
                QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.size() >= 5) {
                    labels[i].x = parts[0].toDouble();
                    labels[i].y = parts[1].toDouble();
                    labels[i].blockType = parts[2].toInt() - 1; // 1-based in file
                    labels[i].magDir = parts[3].toDouble();
                    labels[i].inGroup = parts[4].toInt();
                    if (parts.size() > 5) labels[i].turns = parts[5].toInt();
                }
            }
        }
        else if (lower.startsWith("[solution]")) {
            // Read mesh solution data
            break;
        }
    }

    // Phase 2: Read solution mesh data
    // Mesh nodes — use sscanf for speed (avoids QString/QRegularExpression overhead)
    line = in.readLine().trimmed();
    int numNodes = line.toInt();
    nodes.resize(numNodes);

    bool isAC = (frequency > 0);

    for (int i = 0; i < numNodes; i++) {
        QByteArray ba = in.readLine().toLatin1();
        const char *s = ba.constData();
        double x, y, Are, Aim = 0.0;
        if (isAC)
            std::sscanf(s, "%lf %lf %lf %lf", &x, &y, &Are, &Aim);
        else
            std::sscanf(s, "%lf %lf %lf", &x, &y, &Are);
        nodes[i].x = x;
        nodes[i].y = y;
        nodes[i].A = CmplxF(Are, Aim);
    }

    // Mesh elements — use sscanf for speed
    line = in.readLine().trimmed();
    int numElements = line.toInt();
    elements.resize(numElements);

    int numLabels = (int)labels.size();
    for (int i = 0; i < numElements; i++) {
        QByteArray ba = in.readLine().toLatin1();
        const char *s = ba.constData();
        int p0, p1, p2, lbl;
        std::sscanf(s, "%d %d %d %d", &p0, &p1, &p2, &lbl);
        elements[i].p[0] = p0;
        elements[i].p[1] = p1;
        elements[i].p[2] = p2;
        elements[i].lbl = lbl;

        // Derive block type from label
        if (lbl >= 0 && lbl < numLabels)
            elements[i].blk = labels[lbl].blockType;
    }

    file.close();

    // Phase 3: Post-processing computations
    // Compute element centroids and bounding radii
    for (auto &elm : elements) {
        double x0 = nodes[elm.p[0]].x, y0 = nodes[elm.p[0]].y;
        double x1 = nodes[elm.p[1]].x, y1 = nodes[elm.p[1]].y;
        double x2 = nodes[elm.p[2]].x, y2 = nodes[elm.p[2]].y;
        elm.cx = (x0 + x1 + x2) / 3.0;
        elm.cy = (y0 + y1 + y2) / 3.0;

        // Bounding radius squared
        double d0 = (x0-elm.cx)*(x0-elm.cx) + (y0-elm.cy)*(y0-elm.cy);
        double d1 = (x1-elm.cx)*(x1-elm.cx) + (y1-elm.cy)*(y1-elm.cy);
        double d2 = (x2-elm.cx)*(x2-elm.cx) + (y2-elm.cy)*(y2-elm.cy);
        elm.rsqr = std::max({d0, d1, d2});
    }

    // Compute element flux density (needed for computeSummary/computeTorque)
    computeElementB();

    // Build adjacency (needed for findBoundaryEdges and smoothedB)
    buildAdjacency();
    findBoundaryEdges();

    // Compute element B, smoothed B, and plot bounds eagerly so that
    // getPointValues() and computeSummary() work immediately after load.
    ensureSmoothedB();

    return true;
}

// ---------------------------------------------------------------
// In-memory loading from solver arrays
// ---------------------------------------------------------------
bool ResultsDocument::loadFromSolverData(
    int probType, int lenUnits, double freq, double dpth,
    int numMats, const QString *matNames,
    const double *mat_mu_x, const double *mat_mu_y,
    const double *mat_H_c, const double *mat_Cduct,
    const int *mat_lamType, const double *mat_lamFill, const double *mat_Lam_d,
    const int *mat_bhPoints, const double *const *mat_Bdata, const double *const *mat_Hdata_re,
    int numLabels, const double *lblX, const double *lblY,
    const int *lblBlockType, const double *lblMagDir, const int *lblInGroup, const int *lblTurns,
    int numNodes, const double *nodeX, const double *nodeY,
    const double *nodeA_re, const double *nodeA_im,
    int numElements, const int *elmP, const int *elmLbl, const int *elmBlk)
{
    // Problem metadata
    problemType = probType;
    lengthUnits = lenUnits;
    lengthConv = lengthConversion(lenUnits);
    frequency = freq;
    depth = dpth;

    // Materials
    materials.clear();
    materials.resize(numMats);
    for (int i = 0; i < numMats; i++) {
        SolnMaterial &m = materials[i];
        m.blockName = matNames[i];
        m.mu_x = mat_mu_x[i];
        m.mu_y = mat_mu_y[i];
        m.H_c = mat_H_c[i];
        m.Cduct = mat_Cduct[i];
        m.lamType = mat_lamType[i];
        m.lamFill = mat_lamFill[i];
        m.Lam_d = mat_Lam_d[i];
        m.bhPoints = mat_bhPoints[i];
        if (m.bhPoints > 0 && mat_Bdata[i] && mat_Hdata_re[i]) {
            m.Bdata.assign(mat_Bdata[i], mat_Bdata[i] + m.bhPoints);
            m.Hdata.resize(m.bhPoints);
            for (int j = 0; j < m.bhPoints; j++)
                m.Hdata[j] = CmplxF(mat_Hdata_re[i][j], 0.0);
        }
    }

    // Labels
    labels.clear();
    labels.resize(numLabels);
    for (int i = 0; i < numLabels; i++) {
        labels[i].x = lblX[i];
        labels[i].y = lblY[i];
        labels[i].blockType = lblBlockType[i];
        labels[i].magDir = lblMagDir[i];
        labels[i].inGroup = lblInGroup[i];
        labels[i].turns = lblTurns[i];
    }

    // Nodes — solver stores in cm; convert back to model units
    // Solver node coords = model_coords * c[LengthUnits]  (cm)
    // ResultsDocument stores in model units
    // c[LengthUnits] converts model→cm, so model = cm / c[LengthUnits]
    double cmConv[] = {2.54, 0.1, 1., 100., 0.00254, 1.e-04};
    double invCm = (lenUnits >= 0 && lenUnits <= 5) ? 1.0 / cmConv[lenUnits] : 1.0;

    nodes.clear();
    nodes.resize(numNodes);
    for (int i = 0; i < numNodes; i++) {
        nodes[i].x = nodeX[i] * invCm;
        nodes[i].y = nodeY[i] * invCm;
        nodes[i].A = CmplxF(nodeA_re[i], nodeA_im ? nodeA_im[i] : 0.0);
    }

    // Elements
    elements.clear();
    elements.resize(numElements);
    for (int i = 0; i < numElements; i++) {
        elements[i].p[0] = elmP[i * 3];
        elements[i].p[1] = elmP[i * 3 + 1];
        elements[i].p[2] = elmP[i * 3 + 2];
        elements[i].lbl = elmLbl[i];
        elements[i].blk = elmBlk[i];
    }

    // Post-processing: centroids, bounding radii
    for (auto &elm : elements) {
        double x0 = nodes[elm.p[0]].x, y0 = nodes[elm.p[0]].y;
        double x1 = nodes[elm.p[1]].x, y1 = nodes[elm.p[1]].y;
        double x2 = nodes[elm.p[2]].x, y2 = nodes[elm.p[2]].y;
        elm.cx = (x0 + x1 + x2) / 3.0;
        elm.cy = (y0 + y1 + y2) / 3.0;
        double d0 = (x0-elm.cx)*(x0-elm.cx) + (y0-elm.cy)*(y0-elm.cy);
        double d1 = (x1-elm.cx)*(x1-elm.cx) + (y1-elm.cy)*(y1-elm.cy);
        double d2 = (x2-elm.cx)*(x2-elm.cx) + (y2-elm.cy)*(y2-elm.cy);
        elm.rsqr = std::max({d0, d1, d2});
    }

    computeElementB();
    buildAdjacency();
    findBoundaryEdges();
    ensureSmoothedB();

    return true;
}

// ---------------------------------------------------------------
// Compute element-average B from curl(A)
// ---------------------------------------------------------------

void ResultsDocument::computeElementB()
{
    double L = lengthConv;

    for (auto &elm : elements) {
        getElementB(elm);
    }
}

void ResultsDocument::getElementB(SolnElement &elm)
{
    double L = lengthConv;

    double x0 = nodes[elm.p[0]].x, y0 = nodes[elm.p[0]].y;
    double x1 = nodes[elm.p[1]].x, y1 = nodes[elm.p[1]].y;
    double x2 = nodes[elm.p[2]].x, y2 = nodes[elm.p[2]].y;

    // Shape function coefficients
    // b[i] = y_{i+1} - y_{i+2}, c[i] = x_{i+2} - x_{i+1}
    double b0 = y1 - y2, b1 = y2 - y0, b2 = y0 - y1;
    double c0 = x2 - x1, c1 = x0 - x2, c2 = x1 - x0;

    // Twice the area
    double da = b0 * c1 - b1 * c0;
    if (std::fabs(da) < 1e-30) {
        elm.B1 = elm.B2 = CmplxF(0, 0);
        return;
    }

    CmplxF A0 = nodes[elm.p[0]].A;
    CmplxF A1 = nodes[elm.p[1]].A;
    CmplxF A2 = nodes[elm.p[2]].A;

    if (problemType == 0) {
        // Planar: B = curl(A)
        // B1 (Bx) = dA/dy = (A0*c0 + A1*c1 + A2*c2) / (da * L)
        // B2 (By) = -dA/dx = -(A0*b0 + A1*b1 + A2*b2) / (da * L)
        elm.B1 = (A0 * c0 + A1 * c1 + A2 * c2) / (da * L);
        elm.B2 = -(A0 * b0 + A1 * b1 + A2 * b2) / (da * L);
    }
    else {
        // Axisymmetric: B = curl(A) / r
        // For first-order elements, use centroid r
        double r = (x0 + x1 + x2) / 3.0;
        if (std::fabs(r) < 1e-12) r = 1e-12;

        // Br = (1/r) * dA/dz, Bz = -(1/r) * d(rA)/dr
        elm.B1 = (A0 * c0 + A1 * c1 + A2 * c2) / (da * L * r);
        elm.B2 = -(A0 * b0 + A1 * b1 + A2 * b2) / (da * L);
        // Correction for 1/r factor
        CmplxF Aavg = (A0 + A1 + A2) / 3.0;
        elm.B2 -= Aavg / (r * L);
    }
}

// ---------------------------------------------------------------
// Build node-to-element adjacency
// ---------------------------------------------------------------

void ResultsDocument::buildAdjacency()
{
    int nn = (int)nodes.size();
    m_numList.assign(nn, 0);
    m_conList.resize(nn);
    for (auto &v : m_conList) v.clear();

    for (int i = 0; i < (int)elements.size(); i++) {
        for (int j = 0; j < 3; j++) {
            int ni = elements[i].p[j];
            if (ni >= 0 && ni < nn) {
                m_numList[ni]++;
                m_conList[ni].push_back(i);
            }
        }
    }
}

// ---------------------------------------------------------------
// Find boundary edges (neighbor elements)
// ---------------------------------------------------------------

void ResultsDocument::findBoundaryEdges()
{
    // For each element, find neighbor across each edge
    // Edge j is opposite vertex j, i.e., connects p[(j+1)%3] and p[(j+2)%3]
    for (auto &elm : elements)
        elm.n[0] = elm.n[1] = elm.n[2] = -1;

    for (int i = 0; i < (int)elements.size(); i++) {
        for (int j = 0; j < 3; j++) {
            if (elements[i].n[j] >= 0) continue; // already found

            int na = elements[i].p[(j + 1) % 3];
            int nb = elements[i].p[(j + 2) % 3];

            // Search elements connected to node na for one that also has nb
            for (int k : m_conList[na]) {
                if (k == i) continue;
                // Check if element k shares edge na-nb
                for (int m = 0; m < 3; m++) {
                    int pa = elements[k].p[(m + 1) % 3];
                    int pb = elements[k].p[(m + 2) % 3];
                    if ((pa == na && pb == nb) || (pa == nb && pb == na)) {
                        elements[i].n[j] = k;
                        elements[k].n[m] = i;
                        goto next_edge;
                    }
                }
            }
            next_edge:;
        }
    }
}

// ---------------------------------------------------------------
// Compute smoothed nodal B values
// ---------------------------------------------------------------

void ResultsDocument::computeSmoothedB()
{
    // Simple distance-weighted average of adjacent element B values
    for (auto &elm : elements) {
        for (int j = 0; j < 3; j++) {
            int ni = elm.p[j];
            double nx = nodes[ni].x, ny = nodes[ni].y;

            CmplxF sumB1(0, 0), sumB2(0, 0);
            double sumW = 0.0;

            for (int ei : m_conList[ni]) {
                const auto &e = elements[ei];
                // Check same material for smoothing across interfaces
                if (e.blk != elm.blk) continue;

                double dx = e.cx - nx, dy = e.cy - ny;
                double dist = std::sqrt(dx * dx + dy * dy);
                if (dist < 1e-20) dist = 1e-20;
                double w = 1.0 / dist;
                sumB1 += w * e.B1;
                sumB2 += w * e.B2;
                sumW += w;
            }

            if (sumW > 0) {
                elm.b1[j] = sumB1 / sumW;
                elm.b2[j] = sumB2 / sumW;
            } else {
                elm.b1[j] = elm.B1;
                elm.b2[j] = elm.B2;
            }
        }
    }
}

void ResultsDocument::computeSmoothedIronLoss()
{
    const int numElm = (int)elements.size();
    const bool haveLoss = ((int)ironLoss_Wkg.size() == numElm);

    for (auto &elm : elements) {
        for (int j = 0; j < 3; j++)
            elm.ironLoss[j] = 0.0;
    }

    if (!haveLoss)
        return;

    for (int ei = 0; ei < numElm; ei++) {
        auto &elm = elements[ei];
        const double selfLoss = ironLoss_Wkg[ei];

        for (int j = 0; j < 3; j++) {
            int ni = elm.p[j];
            double nx = nodes[ni].x, ny = nodes[ni].y;

            double sumLoss = 0.0;
            double sumW = 0.0;

            for (int adjEi : m_conList[ni]) {
                if (adjEi < 0 || adjEi >= numElm) continue;

                const auto &adj = elements[adjEi];
                // Keep smoothing within the same region/material so we don't
                // blur across physical boundaries.
                if (adj.lbl != elm.lbl || adj.blk != elm.blk) continue;

                double dx = adj.cx - nx;
                double dy = adj.cy - ny;
                double dist = std::sqrt(dx * dx + dy * dy);
                if (dist < 1e-20) dist = 1e-20;
                double w = 1.0 / dist;

                sumLoss += w * ironLoss_Wkg[adjEi];
                sumW += w;
            }

            elm.ironLoss[j] = (sumW > 0.0) ? (sumLoss / sumW) : selfLoss;
        }
    }
}

// ---------------------------------------------------------------
// Lazy smoothed B + plot bounds (called before rendering)
// ---------------------------------------------------------------

void ResultsDocument::ensureSmoothedB()
{
    if (m_smoothedBReady) return;
    m_smoothedBReady = true;
    computeSmoothedB();
    computePlotBounds();
}

void ResultsDocument::ensureSmoothedIronLoss()
{
    int count = (int)ironLoss_Wkg.size();
    if (m_smoothedIronLossReady && m_smoothedIronLossCount == count)
        return;

    m_smoothedIronLossReady = true;
    m_smoothedIronLossCount = count;
    computeSmoothedIronLoss();
}

// ---------------------------------------------------------------
// Compute plot bounds
// ---------------------------------------------------------------

void ResultsDocument::computePlotBounds()
{
    A_Low = 1e30;
    A_High = -1e30;
    B_Low = 0.0;
    B_High = 0.0;

    for (const auto &nd : nodes) {
        double a = nd.A.real();
        if (a < A_Low) A_Low = a;
        if (a > A_High) A_High = a;
    }

    // B bounds: use element averages, skip tiny elements
    double totalArea = 0;
    for (const auto &elm : elements) {
        double x0 = nodes[elm.p[0]].x, y0 = nodes[elm.p[0]].y;
        double x1 = nodes[elm.p[1]].x, y1 = nodes[elm.p[1]].y;
        double x2 = nodes[elm.p[2]].x, y2 = nodes[elm.p[2]].y;
        double area = std::fabs((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)) / 2.0;
        totalArea += area;
    }

    double avgArea = totalArea / std::max(1, (int)elements.size());

    for (const auto &elm : elements) {
        double x0 = nodes[elm.p[0]].x, y0 = nodes[elm.p[0]].y;
        double x1 = nodes[elm.p[1]].x, y1 = nodes[elm.p[1]].y;
        double x2 = nodes[elm.p[2]].x, y2 = nodes[elm.p[2]].y;
        double area = std::fabs((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)) / 2.0;

        // Skip tiny elements that can have artificially high B
        if (area < avgArea * 0.001) continue;

        double Bmag = std::sqrt(std::norm(elm.B1) + std::norm(elm.B2));
        if (Bmag > B_High) B_High = Bmag;
    }
}

// ---------------------------------------------------------------
// Point-in-triangle test and field interpolation
// ---------------------------------------------------------------

int ResultsDocument::inTriangle(double x, double y) const
{
    // Search from last found triangle outward
    int n = (int)elements.size();
    if (n == 0) return -1;

    auto test = [&](int i) -> bool {
        const SolnElement &elm = elements[i];
        double x0 = nodes[elm.p[0]].x, y0 = nodes[elm.p[0]].y;
        double x1 = nodes[elm.p[1]].x, y1 = nodes[elm.p[1]].y;
        double x2 = nodes[elm.p[2]].x, y2 = nodes[elm.p[2]].y;

        // First check bounding circle
        double dx = x - elm.cx, dy = y - elm.cy;
        if (dx * dx + dy * dy > elm.rsqr * 1.5) return false;

        // Barycentric test
        double da = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
        if (std::fabs(da) < 1e-30) return false;

        double a = ((x1 - x) * (y2 - y) - (x2 - x) * (y1 - y)) / da;
        double b = ((x2 - x) * (y0 - y) - (x0 - x) * (y2 - y)) / da;
        double c = 1.0 - a - b;
        return (a >= -1e-6 && b >= -1e-6 && c >= -1e-6);
    };

    // Try last found first
    if (m_lastTriangle >= 0 && m_lastTriangle < n && test(m_lastTriangle))
        return m_lastTriangle;

    // Expand outward
    for (int r = 0; r < n; r++) {
        int ia = m_lastTriangle + r;
        int ib = m_lastTriangle - r;
        if (ia < n && test(ia)) { m_lastTriangle = ia; return ia; }
        if (ib >= 0 && ib != ia && test(ib)) { m_lastTriangle = ib; return ib; }
    }

    return -1;
}

// ---------------------------------------------------------------
// Compute summary statistics for CSV export
// ---------------------------------------------------------------
ResultsSummary ResultsDocument::computeSummary() const
{
    ResultsSummary s;
    if (elements.empty() || nodes.empty()) return s;

    static const double mu0 = 4.0e-7 * M_PI;
    double L = lengthConv;  // conversion to meters

    s.B_min = 1e30;
    s.A_max = -1e30;
    s.A_min = 1e30;

    // Node stats (A values)
    for (const auto &nd : nodes) {
        double aVal = nd.A.real();
        if (aVal > s.A_max) s.A_max = aVal;
        if (aVal < s.A_min) s.A_min = aVal;
    }

    // Element stats (B values, energy)
    double bSum = 0.0;
    for (const auto &elm : elements) {
        // Element area in length-unit coords
        double x0 = nodes[elm.p[0]].x, y0 = nodes[elm.p[0]].y;
        double x1 = nodes[elm.p[1]].x, y1 = nodes[elm.p[1]].y;
        double x2 = nodes[elm.p[2]].x, y2 = nodes[elm.p[2]].y;
        double area = 0.5 * std::fabs((x1-x0)*(y2-y0) - (x2-x0)*(y1-y0));
        double areaM2 = area * L * L;  // in m^2

        double bMag = std::sqrt(std::norm(elm.B1) + std::norm(elm.B2));
        if (bMag > s.B_max) s.B_max = bMag;
        if (bMag < s.B_min) s.B_min = bMag;
        bSum += bMag;

        // Energy: W = 0.5 * B^2 / (mu0 * mu_r) * area * depth
        double mu_r = 1.0;
        if (elm.lbl >= 0 && elm.lbl < (int)labels.size()) {
            int matIdx = labels[elm.lbl].blockType;
            if (matIdx >= 0 && matIdx < (int)materials.size())
                mu_r = std::max(1e-6, 0.5 * (materials[matIdx].mu_x + materials[matIdx].mu_y));
        }
        double w = 0.5 * (bMag * bMag) / (mu0 * mu_r) * areaM2 * (depth * L);
        s.totalEnergy += w;
        s.totalArea += areaM2;
    }

    s.B_avg = bSum / (double)elements.size();
    return s;
}

// ---------------------------------------------------------------
// Maxwell stress tensor torque via Henrotte method
// Matches FEMM 4.2 block integral type 22 (mask integral).
// ---------------------------------------------------------------

double ResultsDocument::computeTorque(double aboutX, double aboutY,
                                       int groupNumber) const
{
    if (elements.empty() || nodes.empty()) return 0.0;
    if (problemType != 0) return 0.0;  // planar only for now

    static const double mu0 = 4.0e-7 * M_PI;
    double L = lengthConv;

    // --- Step 1: Build mask (1 = selected group, 0 = everything else) ---
    std::vector<double> msk(nodes.size(), 0.0);
    for (size_t e = 0; e < elements.size(); e++) {
        int li = elements[e].lbl;
        if (li >= 0 && li < (int)labels.size()
            && labels[li].inGroup == groupNumber) {
            for (int k = 0; k < 3; k++)
                msk[elements[e].p[k]] = 1.0;
        }
    }

    // Convert rotation centre to metres
    double aboutXm = aboutX * L;
    double aboutYm = aboutY * L;

    double totalTorque = 0.0;

    // --- Step 2: Iterate over ALL elements (mask integral) ---
    for (size_t e = 0; e < elements.size(); e++) {
        int n0 = elements[e].p[0];
        int n1 = elements[e].p[1];
        int n2 = elements[e].p[2];

        double x0 = nodes[n0].x, y0 = nodes[n0].y;
        double x1 = nodes[n1].x, y1 = nodes[n1].y;
        double x2 = nodes[n2].x, y2 = nodes[n2].y;

        // Shape-function gradient coefficients
        double b0 = y1 - y2, b1 = y2 - y0, b2 = y0 - y1;
        double c0 = x2 - x1, c1 = x0 - x2, c2 = x1 - x0;
        double da = b0 * c1 - b1 * c0;   // 2 × area (original units²)
        if (std::fabs(da) < 1e-30) continue;

        // Henrotte vector: gradient of mask (units: 1/metre)
        double vx = -(msk[n0]*b0 + msk[n1]*b1 + msk[n2]*b2) / (da * L);
        double vy = -(msk[n0]*c0 + msk[n1]*c1 + msk[n2]*c2) / (da * L);

        // Skip elements where the mask is uniform (gradient ≈ 0)
        if (std::fabs(vx) < 1e-20 && std::fabs(vy) < 1e-20) continue;

        // B field (real part for magnetostatic)
        double Bx = elements[e].B1.real();
        double By = elements[e].B2.real();

        // Stress-tensor weighted force density (N/m³)
        double F1 = ((Bx*Bx - By*By)*vx + 2.0*Bx*By*vy) / (2.0 * mu0);
        double F2 = ((By*By - Bx*Bx)*vy + 2.0*Bx*By*vx) / (2.0 * mu0);

        // Element centroid in metres
        double xc = (x0 + x1 + x2) / 3.0 * L;
        double yc = (y0 + y1 + y2) / 3.0 * L;

        // Torque about (aboutX, aboutY)
        double torqueContrib = (xc - aboutXm) * F2 - (yc - aboutYm) * F1;

        // Element volume: area (m²) × depth (m)
        double area = std::fabs(da) * 0.5 * L * L * (depth * L);

        totalTorque += area * torqueContrib;
    }

    return totalTorque;
}

PointValues ResultsDocument::getPointValues(double x, double y) const
{
    PointValues pv;
    int ei = inTriangle(x, y);
    if (ei < 0) return pv;

    const SolnElement &elm = elements[ei];
    pv.valid = true;

    // Barycentric interpolation for A
    double x0 = nodes[elm.p[0]].x, y0 = nodes[elm.p[0]].y;
    double x1 = nodes[elm.p[1]].x, y1 = nodes[elm.p[1]].y;
    double x2 = nodes[elm.p[2]].x, y2 = nodes[elm.p[2]].y;

    double da = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(da) < 1e-30) return pv;

    double a = ((x1 - x) * (y2 - y) - (x2 - x) * (y1 - y)) / da;
    double b = ((x2 - x) * (y0 - y) - (x0 - x) * (y2 - y)) / da;
    double c = 1.0 - a - b;

    pv.A = a * nodes[elm.p[0]].A + b * nodes[elm.p[1]].A + c * nodes[elm.p[2]].A;

    // B from smoothed values if available
    if (smoothB) {
        pv.B1 = a * elm.b1[0] + b * elm.b1[1] + c * elm.b1[2];
        pv.B2 = a * elm.b2[0] + b * elm.b2[1] + c * elm.b2[2];
    } else {
        pv.B1 = elm.B1;
        pv.B2 = elm.B2;
    }

    // H from B (simplified — assumes linear material)
    double mu0 = 4.0e-7 * M_PI;
    if (elm.blk >= 0 && elm.blk < (int)materials.size()) {
        const SolnMaterial &mat = materials[elm.blk];
        if (mat.mu_x > 0) pv.H1 = pv.B1 / (mu0 * mat.mu_x);
        if (mat.mu_y > 0) pv.H2 = pv.B2 / (mu0 * mat.mu_y);
    }

    return pv;
}

// ---------------------------------------------------------------
// Bounding box
// ---------------------------------------------------------------

bool ResultsDocument::getBoundingBox(double &xmin, double &ymin,
                                      double &xmax, double &ymax) const
{
    if (nodes.empty()) return false;

    xmin = ymin = 1e30;
    xmax = ymax = -1e30;

    for (const auto &nd : nodes) {
        if (nd.x < xmin) xmin = nd.x;
        if (nd.x > xmax) xmax = nd.x;
        if (nd.y < ymin) ymin = nd.y;
        if (nd.y > ymax) ymax = nd.y;
    }

    // Add 5% padding
    double dx = xmax - xmin, dy = ymax - ymin;
    xmin -= dx * 0.05;
    xmax += dx * 0.05;
    ymin -= dy * 0.05;
    ymax += dy * 0.05;

    return true;
}
