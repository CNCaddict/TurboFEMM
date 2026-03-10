// FEMM Qt 6 GUI — DXF R12 Import implementation
// Ported from original FEMM's ReadDXF() in cd_movecopy.cpp

#include "dxfimporter.h"
#include "document.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------
// Construction
// ---------------------------------------------------------------

DxfImporter::DxfImporter(FemmeDocument *doc)
    : m_doc(doc)
{
}

// ---------------------------------------------------------------
// Convenience one-shot import
// ---------------------------------------------------------------

bool DxfImporter::import(const QString &filePath, double tolerance)
{
    if (!parseFile(filePath))
        return false;

    double tol = (tolerance <= 0.0) ? defaultTolerance() : tolerance;
    mergeIntoDocument(tol);
    return true;
}

// ---------------------------------------------------------------
// Phase 1: Parse DXF file into temporary lists
// ---------------------------------------------------------------

bool DxfImporter::parseFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = QStringLiteral("Could not open file: ") + filePath;
        return false;
    }

    QTextStream in(&file);

    // Reset state
    m_nodes.clear();
    m_segments.clear();
    m_arcs.clear();
    m_layers.clear();
    m_polylineActive = false;
    m_polylineClosed = false;
    m_polylineFirstNode = -1;
    m_polylineLastNode = -1;
    m_polylinePendingAngle = 0.0;
    m_nodesAdded = 0;
    m_segmentsAdded = 0;
    m_arcsAdded = 0;

    // Main parse loop: read lines one at a time, matching entity keywords.
    // In DXF R12, entity type keywords appear as values after a group code 0 line.
    // The outer loop reads every line; when an entity keyword is found, the
    // inner parser reads subsequent group-code/value pairs until groupCode==0.
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();

        // Entity keyword matching (mirrors original's strncmp checks)
        if (line.startsWith(QLatin1String("LAYER")) && line.length() == 5) {
            parseLayer(in);
        }
        else if (line == QLatin1String("POINT")) {
            parsePoint(in);
        }
        else if (line == QLatin1String("LWPOLYLINE")) {
            parseLWPolyline(in);
        }
        else if (line.startsWith(QLatin1String("POLYLINE")) && line.length() == 8) {
            parsePolyline(in);
        }
        else if (line == QLatin1String("SEQEND")) {
            parseSeqEnd();
        }
        else if (line == QLatin1String("VERTEX")) {
            parseVertex(in);
        }
        else if (line.startsWith(QLatin1String("LINE")) && line.length() == 4) {
            parseLine(in);
        }
        // Guard: ARCALIGNEDTEXT must not trigger ARC parser
        else if (line.startsWith(QLatin1String("ARCA"))) {
            // skip — do nothing, let the outer loop continue
        }
        else if (line == QLatin1String("ARC")) {
            parseArc(in);
        }
        else if (line == QLatin1String("CIRCLE")) {
            parseCircle(in);
        }
    }

    file.close();

    if (m_nodes.empty()) {
        m_error = QStringLiteral("No geometry found in DXF file.");
        return false;
    }

    return true;
}

// ---------------------------------------------------------------
// Default tolerance: 0.01% of bounding box diagonal
// ---------------------------------------------------------------

double DxfImporter::defaultTolerance() const
{
    if (m_nodes.size() < 2) return 1e-8;

    double xmin = m_nodes[0].x, xmax = xmin;
    double ymin = m_nodes[0].y, ymax = ymin;

    for (const auto &nd : m_nodes) {
        if (nd.x < xmin) xmin = nd.x;
        if (nd.x > xmax) xmax = nd.x;
        if (nd.y < ymin) ymin = nd.y;
        if (nd.y > ymax) ymax = nd.y;
    }

    double diag = std::sqrt((xmax - xmin) * (xmax - xmin) +
                            (ymax - ymin) * (ymax - ymin));
    double R = diag * 1e-4;  // 0.01% of diagonal
    if (R <= 0.0) return 1e-8;

    // Round to a "nice" number (matching original FEMM)
    double logR = std::floor(std::log10(R));
    double rounded = std::floor(R / std::pow(10.0, logR)) * std::pow(10.0, logR);
    return (rounded > 0.0) ? rounded : 1e-8;
}

// ---------------------------------------------------------------
// Phase 2: Merge temporary geometry into FemmeDocument
// ---------------------------------------------------------------

void DxfImporter::mergeIntoDocument(double tolerance)
{
    // Record starting counts for statistics
    int startNodes = static_cast<int>(m_doc->nodes.size());
    int startSegs  = static_cast<int>(m_doc->segments.size());
    int startArcs  = static_cast<int>(m_doc->arcSegments.size());

    // Build mapping from temp node indices → document node indices.
    // Uses addNode with tolerance for deduplication (matching FancyEnforcePSLG).
    std::vector<int> nodeMap(m_nodes.size(), -1);

    for (int i = 0; i < static_cast<int>(m_nodes.size()); i++) {
        double x = m_nodes[i].x;
        double y = m_nodes[i].y;

        // addNode returns false if a node within tolerance already exists
        m_doc->addNode(x, y, tolerance);

        // Find the actual index (newly added or existing match)
        int idx = m_doc->closestNode(x, y);
        nodeMap[i] = idx;

        // Set group on the node
        if (idx >= 0 && idx < static_cast<int>(m_doc->nodes.size())) {
            if (m_nodes[i].inGroup != 0)
                m_doc->nodes[idx].inGroup = m_nodes[i].inGroup;
        }
    }

    // Add segments (remapping node indices)
    for (const auto &seg : m_segments) {
        int n0 = nodeMap[seg.n0];
        int n1 = nodeMap[seg.n1];
        if (n0 >= 0 && n1 >= 0 && n0 != n1) {
            if (m_doc->addSegment(n0, n1)) {
                auto &s = m_doc->segments.back();
                s.inGroup = seg.inGroup;
            }
        }
    }

    // Add arc segments (remapping node indices)
    for (const auto &arc : m_arcs) {
        int n0 = nodeMap[arc.n0];
        int n1 = nodeMap[arc.n1];
        if (n0 >= 0 && n1 >= 0 && n0 != n1) {
            if (m_doc->addArcSegment(n0, n1, arc.arcLength, 5.0)) {
                auto &a = m_doc->arcSegments.back();
                a.inGroup = arc.inGroup;
            }
        }
    }

    // Statistics
    m_nodesAdded    = static_cast<int>(m_doc->nodes.size()) - startNodes;
    m_segmentsAdded = static_cast<int>(m_doc->segments.size()) - startSegs;
    m_arcsAdded     = static_cast<int>(m_doc->arcSegments.size()) - startArcs;

    m_doc->isModified = true;
}

// ---------------------------------------------------------------
// Temp list helpers
// ---------------------------------------------------------------

int DxfImporter::addTempNode(double x, double y, int group)
{
    int idx = static_cast<int>(m_nodes.size());
    TempNode nd;
    nd.x = x;
    nd.y = y;
    nd.inGroup = group;
    m_nodes.push_back(nd);
    return idx;
}

void DxfImporter::addTempSegment(int n0, int n1, int group)
{
    TempSegment seg;
    seg.n0 = n0;
    seg.n1 = n1;
    seg.inGroup = group;
    m_segments.push_back(seg);
}

void DxfImporter::addTempArc(int n0, int n1, double arcLen, int group)
{
    TempArc arc;
    arc.n0 = n0;
    arc.n1 = n1;
    arc.arcLength = arcLen;
    arc.inGroup = group;
    m_arcs.push_back(arc);
}

// ---------------------------------------------------------------
// Layer → group mapping
// ---------------------------------------------------------------

int DxfImporter::layerToGroup(const QString &layerName)
{
    int idx = m_layers.indexOf(layerName);
    if (idx >= 0) return idx;
    m_layers.append(layerName);
    return m_layers.size() - 1;
}

// ---------------------------------------------------------------
// Skip an entity (consume pairs until next entity boundary)
// ---------------------------------------------------------------

void DxfImporter::skipEntity(QTextStream &in)
{
    while (!in.atEnd()) {
        QString codeLine = in.readLine().trimmed();
        int k = codeLine.toInt();
        if (k == 0) break;       // entity boundary — don't read value line
        in.readLine();            // consume the value line
    }
}

// ---------------------------------------------------------------
// LAYER entity parser
// ---------------------------------------------------------------

void DxfImporter::parseLayer(QTextStream &in)
{
    while (!in.atEnd()) {
        QString codeLine = in.readLine().trimmed();
        int k = codeLine.toInt();
        if (k == 0) break;
        QString val = in.readLine();
        if (k == 2) {
            layerToGroup(val.trimmed());
        }
    }
}

// ---------------------------------------------------------------
// POINT entity parser
// ---------------------------------------------------------------

void DxfImporter::parsePoint(QTextStream &in)
{
    double px = 0.0, py = 0.0;
    int group = 0;
    int flags = 0;

    while (!in.atEnd()) {
        QString codeLine = in.readLine().trimmed();
        int k = codeLine.toInt();
        if (k == 0) break;
        QString val = in.readLine();

        switch (k) {
        case 8:  group = layerToGroup(val.trimmed()); break;
        case 10: px = val.toDouble(); flags |= 1; break;
        case 20: py = val.toDouble(); flags |= 2; break;
        }
    }

    if (flags == 3) {
        addTempNode(px, py, group);
    }
}

// ---------------------------------------------------------------
// LINE entity parser
// ---------------------------------------------------------------

void DxfImporter::parseLine(QTextStream &in)
{
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int group = 0;
    int flags = 0;

    while (!in.atEnd()) {
        QString codeLine = in.readLine().trimmed();
        int k = codeLine.toInt();
        if (k == 0) break;
        QString val = in.readLine();

        switch (k) {
        case 8:  group = layerToGroup(val.trimmed()); break;
        case 10: x0 = val.toDouble(); flags |= 1; break;
        case 20: y0 = val.toDouble(); flags |= 2; break;
        case 11: x1 = val.toDouble(); flags |= 4; break;
        case 21: y1 = val.toDouble(); flags |= 8; break;
        }
    }

    if (flags == 15) {
        int idx0 = addTempNode(x0, y0, group);
        int idx1 = addTempNode(x1, y1, group);
        addTempSegment(idx0, idx1, group);
    }
}

// ---------------------------------------------------------------
// ARC entity parser
// ---------------------------------------------------------------

void DxfImporter::parseArc(QTextStream &in)
{
    double cx = 0, cy = 0, R = 0, a0 = 0, a1 = 0;
    int group = 0;
    int flags = 0;

    while (!in.atEnd()) {
        QString codeLine = in.readLine().trimmed();
        int k = codeLine.toInt();
        if (k == 0) break;
        QString val = in.readLine();

        switch (k) {
        case 8:  group = layerToGroup(val.trimmed()); break;
        case 10: cx = val.toDouble(); flags |= 1; break;
        case 20: cy = val.toDouble(); flags |= 2; break;
        case 40: R  = val.toDouble(); flags |= 4; break;
        case 50: a0 = val.toDouble(); flags |= 8; break;
        case 51: a1 = val.toDouble(); flags |= 16; break;
        }
    }

    if (flags != 31) return;

    if (a1 < a0) a1 += 360.0;
    double span = a1 - a0;

    if (span <= 180.0) {
        // Single arc
        double px0 = cx + R * std::cos(a0 * M_PI / 180.0);
        double py0 = cy + R * std::sin(a0 * M_PI / 180.0);
        double px1 = cx + R * std::cos(a1 * M_PI / 180.0);
        double py1 = cy + R * std::sin(a1 * M_PI / 180.0);

        int idx0 = addTempNode(px0, py0, group);
        int idx1 = addTempNode(px1, py1, group);
        addTempArc(idx0, idx1, span, group);
    }
    else {
        // Split into two half-arcs at midpoint angle
        double amid = (a0 + a1) / 2.0;

        double px0 = cx + R * std::cos(a0 * M_PI / 180.0);
        double py0 = cy + R * std::sin(a0 * M_PI / 180.0);
        double pxm = cx + R * std::cos(amid * M_PI / 180.0);
        double pym = cy + R * std::sin(amid * M_PI / 180.0);
        double px1 = cx + R * std::cos(a1 * M_PI / 180.0);
        double py1 = cy + R * std::sin(a1 * M_PI / 180.0);

        int idx0 = addTempNode(px0, py0, group);
        int idxm = addTempNode(pxm, pym, group);
        int idx1 = addTempNode(px1, py1, group);

        addTempArc(idx0, idxm, span / 2.0, group);
        addTempArc(idxm, idx1, span / 2.0, group);
    }
}

// ---------------------------------------------------------------
// CIRCLE entity parser — two 180° semicircular arcs
// ---------------------------------------------------------------

void DxfImporter::parseCircle(QTextStream &in)
{
    double cx = 0, cy = 0, R = 0;
    int group = 0;
    int flags = 0;

    while (!in.atEnd()) {
        QString codeLine = in.readLine().trimmed();
        int k = codeLine.toInt();
        if (k == 0) break;
        QString val = in.readLine();

        switch (k) {
        case 8:  group = layerToGroup(val.trimmed()); break;
        case 10: cx = val.toDouble(); flags |= 1; break;
        case 20: cy = val.toDouble(); flags |= 2; break;
        case 40: R  = val.toDouble(); flags |= 4; break;
        }
    }

    if (flags != 7) return;

    // Right point and left point
    int idx0 = addTempNode(cx + R, cy, group);  // rightmost
    int idx1 = addTempNode(cx - R, cy, group);  // leftmost

    // Top semicircle: idx0 → idx1, 180° CCW
    addTempArc(idx0, idx1, 180.0, group);
    // Bottom semicircle: idx1 → idx0, 180° CCW
    addTempArc(idx1, idx0, 180.0, group);
}

// ---------------------------------------------------------------
// Bulge arc helper: connect two nodes with arc, split if > 180°
// Mirrors original FEMM's bulge handling in LWPOLYLINE/POLYLINE.
// angle > 0 means CCW (n0 → n1), angle < 0 means CW.
// ---------------------------------------------------------------

void DxfImporter::addBulgeArc(int prevIdx, int curIdx, double angle, int group)
{
    int n0, n1;
    if (angle > 0) {
        n0 = prevIdx;
        n1 = curIdx;
    } else {
        n0 = curIdx;
        n1 = prevIdx;
        angle = std::fabs(angle);
    }

    if (angle > 180.0) {
        // Split into two half-arcs with midpoint on the arc
        double halfAngle = angle / 2.0;
        double ha_rad = halfAngle * M_PI / 180.0;

        double p0x = m_nodes[n0].x, p0y = m_nodes[n0].y;
        double p1x = m_nodes[n1].x, p1y = m_nodes[n1].y;

        // Midpoint on arc (from original's complex formula):
        // p2 = (p0+p1)/2 - i*(1-cos(ha))*(p1-p0) / (2*sin(ha))
        // In real coords: real part gets +factor*(p1y-p0y),
        //                 imag part gets -factor*(p1x-p0x)
        double factor = (1.0 - std::cos(ha_rad)) / (2.0 * std::sin(ha_rad));
        double mx = (p0x + p1x) / 2.0 + factor * (p1y - p0y);
        double my = (p0y + p1y) / 2.0 - factor * (p1x - p0x);

        int midIdx = addTempNode(mx, my, group);

        // First half-arc
        if (n0 < n1) {
            addTempArc(n0, midIdx, halfAngle, group);
        } else {
            addTempArc(midIdx, n1, halfAngle, group);
        }

        // Adjust indices for second half (matching original InsertAt logic)
        int sn0 = n0, sn1 = n1;
        if (n1 < n0) {
            // n1 was the smaller index — midIdx goes between them
            sn1 = midIdx;
            sn0 = n0;  // n0 stays (it was larger, no shift needed with push_back)
        } else {
            sn0 = midIdx;
            sn1 = n1;
        }

        // Second half-arc
        addTempArc(sn0, sn1, halfAngle, group);
    }
    else {
        addTempArc(n0, n1, angle, group);
    }
}

// ---------------------------------------------------------------
// LWPOLYLINE entity parser
// Lightweight polyline with inline vertex list.
// ---------------------------------------------------------------

void DxfImporter::parseLWPolyline(QTextStream &in)
{
    int segs = 0;
    bool closed = false;
    int firstPoint = -1;
    double pendingAngle = 0.0;
    int group = 0;

    double vx = 0.0, vy = 0.0;
    int vflags = 0;   // bit 1 = have x, bit 2 = have y
    int lastNode = -1;

    while (!in.atEnd()) {
        QString codeLine = in.readLine().trimmed();
        int k = codeLine.toInt();
        if (k == 0) break;
        QString val = in.readLine();

        switch (k) {
        case 8:
            group = layerToGroup(val.trimmed());
            break;
        case 70:
            // Bit 0 (value 1) means closed
            if (val.trimmed().toInt() & 1)
                closed = true;
            break;
        case 42:
            pendingAngle = 720.0 * std::atan(val.toDouble()) / M_PI;
            if (pendingAngle < -360.0) pendingAngle = -360.0;
            if (pendingAngle >  360.0) pendingAngle =  360.0;
            break;
        case 10:
            // If we already have a complete vertex pending, commit it first
            if (vflags == 3) {
                int nodeIdx = addTempNode(vx, vy, group);
                if (segs == 0) {
                    firstPoint = nodeIdx;
                } else {
                    // Connect to previous vertex
                    if (pendingAngle == 0.0) {
                        addTempSegment(nodeIdx, lastNode, group);
                    } else {
                        addBulgeArc(lastNode, nodeIdx, pendingAngle, group);
                        pendingAngle = 0.0;
                    }
                }
                lastNode = nodeIdx;
                segs++;
                vflags = 0;
            }
            vx = val.toDouble();
            vflags |= 1;
            break;
        case 20:
            vy = val.toDouble();
            vflags |= 2;
            break;
        }
    }

    // Commit the last vertex
    if (vflags == 3) {
        int nodeIdx = addTempNode(vx, vy, group);
        if (segs == 0) {
            firstPoint = nodeIdx;
        } else {
            if (pendingAngle == 0.0) {
                addTempSegment(nodeIdx, lastNode, group);
            } else {
                addBulgeArc(lastNode, nodeIdx, pendingAngle, group);
                pendingAngle = 0.0;
            }
        }
        lastNode = nodeIdx;
        segs++;
    }

    // Close the polyline if required
    if (closed && firstPoint >= 0 && segs > 1) {
        if (pendingAngle == 0.0) {
            addTempSegment(lastNode, firstPoint, group);
        } else {
            addBulgeArc(lastNode, firstPoint, pendingAngle, group);
        }
    }
}

// ---------------------------------------------------------------
// POLYLINE entity parser (old-style — sets up state for VERTEX)
// ---------------------------------------------------------------

void DxfImporter::parsePolyline(QTextStream &in)
{
    m_polylineActive = true;
    m_polylineFirstVertex = true;
    m_polylineFirstNode = -1;
    m_polylineLastNode = -1;
    m_polylineClosed = false;
    m_polylinePendingAngle = 0.0;
    m_polylineGroup = 0;

    while (!in.atEnd()) {
        QString codeLine = in.readLine().trimmed();
        int k = codeLine.toInt();
        if (k == 0) break;
        QString val = in.readLine();

        if (k == 70) {
            int flags = val.trimmed().toInt();
            if (flags & 1) m_polylineClosed = true;
        }
    }
}

// ---------------------------------------------------------------
// VERTEX entity parser (part of old-style POLYLINE)
// ---------------------------------------------------------------

void DxfImporter::parseVertex(QTextStream &in)
{
    if (!m_polylineActive) {
        skipEntity(in);
        return;
    }

    double vx = 0.0, vy = 0.0;
    double nextAngle = 0.0;
    int group = m_polylineGroup;
    int flags = 0;

    while (!in.atEnd()) {
        QString codeLine = in.readLine().trimmed();
        int k = codeLine.toInt();
        if (k == 0) break;
        QString val = in.readLine();

        switch (k) {
        case 8:
            group = layerToGroup(val.trimmed());
            break;
        case 10:
            vx = val.toDouble();
            flags |= 1;
            break;
        case 20:
            vy = val.toDouble();
            flags |= 2;
            break;
        case 42:
            nextAngle = 720.0 * std::atan(val.toDouble()) / M_PI;
            if (nextAngle < -360.0) nextAngle = -360.0;
            if (nextAngle >  360.0) nextAngle =  360.0;
            break;
        }
    }

    if (flags != 3) return;

    int nodeIdx = addTempNode(vx, vy, group);

    if (m_polylineFirstVertex) {
        // First vertex — just record it
        m_polylineFirstNode = nodeIdx;
        m_polylineFirstVertex = false;
    }
    else {
        // Connect to previous vertex
        if (m_polylinePendingAngle != 0.0) {
            addBulgeArc(m_polylineLastNode, nodeIdx, m_polylinePendingAngle, group);
        } else {
            addTempSegment(m_polylineLastNode, nodeIdx, group);
        }
    }

    m_polylineLastNode = nodeIdx;
    m_polylinePendingAngle = nextAngle;
}

// ---------------------------------------------------------------
// SEQEND entity — closes old-style POLYLINE
// ---------------------------------------------------------------

void DxfImporter::parseSeqEnd()
{
    if (!m_polylineActive) return;

    // Close the contour if required
    if (m_polylineClosed && m_polylineFirstNode >= 0 && m_polylineLastNode >= 0
        && m_polylineFirstNode != m_polylineLastNode)
    {
        if (m_polylinePendingAngle != 0.0) {
            addBulgeArc(m_polylineLastNode, m_polylineFirstNode,
                        m_polylinePendingAngle, m_polylineGroup);
        } else {
            addTempSegment(m_polylineLastNode, m_polylineFirstNode, m_polylineGroup);
        }
    }

    // Reset state
    m_polylineActive = false;
    m_polylineClosed = false;
    m_polylineFirstNode = -1;
    m_polylineLastNode = -1;
    m_polylinePendingAngle = 0.0;
}
