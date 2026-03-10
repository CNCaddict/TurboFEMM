// FEMM Qt 6 GUI — Geometry editor widget implementation
#include "drawingwidget.h"
#include "document.h"
#include "resultsoverlay.h"
#include "dialogs/blockpropsdlg.h"
#include "dialogs/segpropsdlg.h"
#include "dialogs/arcpropsdlg.h"
#include "dialogs/nodepropsdlg.h"
#include "dialogs/addarcdlg.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------
// Colors (matching original FEMM style)
// ---------------------------------------------------------------
static const QColor kColorBackground(255, 255, 255);
static const QColor kColorGrid(200, 200, 200);
static const QColor kColorOrigin(0, 0, 0);
static const QColor kColorNode(0, 0, 0);
static const QColor kColorSegment(0, 0, 180);
static const QColor kColorArc(0, 0, 180);
static const QColor kColorBlockLabel(0, 128, 0);
static const QColor kColorSelected(255, 0, 0);
static const QColor kColorMesh(160, 160, 160);
static const QColor kColorText(0, 0, 128);

// ---------------------------------------------------------------
// Helper: minimum distance from point (xd,yd) to an arc segment
// Uses the same geometry as drawArcSegments() for consistency.
// ---------------------------------------------------------------
static double distanceToArc(const FNode &nd0, const FNode &nd1,
                            const FArcSegment &arc,
                            double xd, double yd)
{
    double x0 = nd0.x, y0 = nd0.y;
    double x1 = nd1.x, y1 = nd1.y;
    double arcAngle = arc.arcLength * M_PI / 180.0;

    if (std::fabs(arcAngle) < 1e-6) {
        // Degenerate arc — treat as line segment
        double ldx = x1 - x0, ldy = y1 - y0;
        double len2 = ldx * ldx + ldy * ldy;
        double t = (len2 > 0) ? std::max(0.0, std::min(1.0,
            ((xd - x0) * ldx + (yd - y0) * ldy) / len2)) : 0.0;
        double px = x0 + t * ldx, py = y0 + t * ldy;
        return std::sqrt((xd - px) * (xd - px) + (yd - py) * (yd - py));
    }

    // Compute arc center and radius (same math as drawArcSegments)
    double mx = (x0 + x1) / 2.0;
    double my = (y0 + y1) / 2.0;
    double dx = x1 - x0, dy = y1 - y0;
    double halfChord = std::sqrt(dx * dx + dy * dy) / 2.0;
    if (halfChord < 1e-12)
        return std::sqrt((xd - x0) * (xd - x0) + (yd - y0) * (yd - y0));

    double R = halfChord / std::sin(arcAngle / 2.0);
    double d = R * std::cos(arcAngle / 2.0);

    // Normal to chord (perpendicular)
    double nx = -dy / (2.0 * halfChord);
    double ny = dx / (2.0 * halfChord);

    // Arc center
    double cx = mx + d * nx;
    double cy = my + d * ny;

    // Start angle and end angle of the arc
    double startAngle = std::atan2(y0 - cy, x0 - cx);
    // Arc sweeps counter-clockwise (positive) by arcAngle (FEMM convention)
    double endAngle = startAngle + arcAngle;

    // Check if the test point's angle falls within the arc's angular sweep
    double testAngle = std::atan2(yd - cy, xd - cx);

    // The arc goes from startAngle to endAngle (CCW, i.e. increasing angle)
    // Normalize: ensure endAngle > startAngle
    double sa = startAngle;
    double ea = endAngle;
    // Normalize ea to be in [sa, sa + 2π)
    while (ea < sa) ea += 2.0 * M_PI;
    while (ea >= sa + 2.0 * M_PI) ea -= 2.0 * M_PI;

    // Normalize testAngle to same range
    double ta = testAngle;
    while (ta < sa) ta += 2.0 * M_PI;
    while (ta >= sa + 2.0 * M_PI) ta -= 2.0 * M_PI;

    if (ta >= sa && ta <= ea) {
        // Point's angle is within arc range — closest point is on the arc
        double rDist = std::sqrt((xd - cx) * (xd - cx) + (yd - cy) * (yd - cy));
        return std::fabs(rDist - std::fabs(R));
    } else {
        // Outside arc range — closest is one of the two endpoints
        double d0 = std::sqrt((xd - x0) * (xd - x0) + (yd - y0) * (yd - y0));
        double d1 = std::sqrt((xd - x1) * (xd - x1) + (yd - y1) * (yd - y1));
        return std::min(d0, d1);
    }
}

DrawingWidget::DrawingWidget(FemmeDocument *doc, QWidget *parent)
    : QWidget(parent), m_doc(doc)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

DrawingWidget::~DrawingWidget() = default;

void DrawingWidget::setResultsOverlay(ResultsOverlayRenderer *overlay)
{
    m_overlay = overlay;
    update();
}

void DrawingWidget::setEditMode(int mode)
{
    m_editMode = mode;
    m_firstPoint = -1;  // reset multi-click state
    update();
}

// ---------------------------------------------------------------
// Coordinate transforms
// ---------------------------------------------------------------

QPointF DrawingWidget::drawingToScreen(double xd, double yd) const
{
    double sx = m_mag * (xd - m_ox);
    double sy = (double)(height() - 1) - m_mag * (yd - m_oy);
    return QPointF(sx, sy);
}

void DrawingWidget::screenToDrawing(int xs, int ys, double &xd, double &yd) const
{
    xd = (double)xs / m_mag + m_ox;
    yd = ((double)(height() - 1) - (double)ys) / m_mag + m_oy;
}

void DrawingWidget::applySnap(double &x, double &y) const
{
    if (m_snapToGrid && m_gridSize > 0) {
        x = m_gridSize * std::floor(0.5 + x / m_gridSize);
        y = m_gridSize * std::floor(0.5 + y / m_gridSize);
    }
}

// ---------------------------------------------------------------
// Zoom / Pan
// ---------------------------------------------------------------

void DrawingWidget::zoomIn()
{
    if (m_viewLocked) return;

    double cx = (double)width() / 2.0;
    double cy = (double)height() / 2.0;
    double xd, yd;
    screenToDrawing((int)cx, (int)cy, xd, yd);

    m_mag *= 2.0;

    // Re-center on same drawing point
    m_ox = xd - cx / m_mag;
    m_oy = yd - ((double)(height() - 1) - cy) / m_mag;
    update();
}

void DrawingWidget::zoomOut()
{
    if (m_viewLocked) return;

    double cx = (double)width() / 2.0;
    double cy = (double)height() / 2.0;
    double xd, yd;
    screenToDrawing((int)cx, (int)cy, xd, yd);

    m_mag /= 2.0;
    if (m_mag < 0.001) m_mag = 0.001;

    m_ox = xd - cx / m_mag;
    m_oy = yd - ((double)(height() - 1) - cy) / m_mag;
    update();
}

void DrawingWidget::zoomFit()
{
    if (m_viewLocked) return;
    if (!m_doc) return;

    double xmin, ymin, xmax, ymax;
    if (!m_doc->getBoundingBox(xmin, ymin, xmax, ymax)) {
        // Empty document: show default range
        m_ox = -1.0;
        m_oy = -1.0;
        m_mag = std::min(width(), height()) / 2.0;
        update();
        return;
    }

    double dx = xmax - xmin;
    double dy = ymax - ymin;
    if (dx < 1e-12) dx = 1.0;
    if (dy < 1e-12) dy = 1.0;

    // Fit model to 80% of viewport (10% margin on each side)
    double magX = (double)width() * 0.8 / dx;
    double magY = (double)height() * 0.8 / dy;
    m_mag = std::min(magX, magY);

    // Center the model in the viewport
    double modelCenterX = (xmin + xmax) / 2.0;
    double modelCenterY = (ymin + ymax) / 2.0;
    m_ox = modelCenterX - (double)width() / (2.0 * m_mag);
    m_oy = modelCenterY - (double)(height() - 1) / (2.0 * m_mag);

    // Auto-set grid size based on geometry scale
    double w = std::max(dx, dy);
    m_gridSize = std::pow(10.0, std::floor(std::log10(w) - 0.5));

    update();
}

void DrawingWidget::setViewTransform(double ox, double oy, double mag)
{
    m_ox = ox;
    m_oy = oy;
    m_mag = mag;
    update();
}

// ---------------------------------------------------------------
// Refresh display (handles cached-frame mode for motion sweep)
// ---------------------------------------------------------------

void DrawingWidget::refreshDisplay()
{
    if (m_cachedFrame.isNull()) {
        // Normal mode: schedule async repaint
        update();
        return;
    }

    // Motion sweep: the cached frame is stale after a settings change.
    // Clear it, do a synchronous repaint (which will render live with
    // the new settings), then re-cache the result.
    m_cachedFrame = QPixmap();
    repaint();
    m_cachedFrame = grab();
}

// ---------------------------------------------------------------
// Paint
// ---------------------------------------------------------------

void DrawingWidget::paintEvent(QPaintEvent * /*event*/)
{
    // During motion sweep: paint the cached frame snapshot so the display
    // always shows a correct overlay+geometry pair, even while the document
    // geometry has already been moved for the next solver step.
    if (!m_cachedFrame.isNull()) {
        QPainter p(this);
        p.drawPixmap(0, 0, m_cachedFrame);
        return;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Background
    p.fillRect(rect(), kColorBackground);

    if (!m_doc) return;

    // Draw layers from back to front
    if (m_showGrid) drawGrid(p);
    if (m_showOrigin) drawOrigin(p);

    // Results overlay (behind geometry lines)
    if (m_overlay && m_overlay->document()) {
        m_overlay->render(p, m_ox, m_oy, m_mag, width(), height());
        p.setRenderHint(QPainter::Antialiasing, true);
    } else if (m_doc->hasMesh) {
        // Only draw geometry mesh when no results overlay
        drawMesh(p);
    }

    // Geometry layers on top
    if (m_showGeometry) {
        drawSegments(p);
        drawArcSegments(p);
    }
    if (m_showNodes) drawNodes(p);
    if (m_showBlockLabels) drawBlockLabels(p);

    // Rubber-band selection rectangle
    if (m_rubberBand) {
        QRect rc = QRect(m_rbStart, m_rbEnd).normalized();
        p.setPen(QPen(QColor(80, 120, 200), 1, Qt::DashLine));
        p.setBrush(QColor(80, 120, 200, 30));
        p.drawRect(rc);
    }
}

void DrawingWidget::drawGrid(QPainter &p)
{
    if (m_gridSize <= 0) return;

    // Compute visible range in drawing coords
    double x0, y0, x1, y1;
    screenToDrawing(0, height() - 1, x0, y0);
    screenToDrawing(width() - 1, 0, x1, y1);

    // Skip factor: don't draw grid points closer than ~8 pixels
    int skip = 1;
    while (m_gridSize * skip * m_mag < 8.0) skip++;
    double gs = m_gridSize * skip;

    double gx0 = gs * std::floor(x0 / gs);
    double gy0 = gs * std::floor(y0 / gs);

    p.setPen(QPen(kColorGrid, 1));

    for (double gx = gx0; gx <= x1; gx += gs) {
        for (double gy = gy0; gy <= y1; gy += gs) {
            QPointF sp = drawingToScreen(gx, gy);
            p.drawPoint(sp);
        }
    }
}

void DrawingWidget::drawOrigin(QPainter &p)
{
    QPointF o = drawingToScreen(0.0, 0.0);
    if (o.x() < -50 || o.x() > width() + 50) return;
    if (o.y() < -50 || o.y() > height() + 50) return;

    p.setPen(QPen(kColorOrigin, 1));
    p.drawLine(QPointF(o.x() - 9, o.y()), QPointF(o.x() + 9, o.y()));
    p.drawLine(QPointF(o.x(), o.y() - 9), QPointF(o.x(), o.y() + 9));
}

void DrawingWidget::drawNodes(QPainter &p)
{
    for (const auto &nd : m_doc->nodes) {
        QPointF sp = drawingToScreen(nd.x, nd.y);
        QColor c = nd.isSelected ? kColorSelected : kColorNode;
        p.setPen(QPen(c, 1));
        p.setBrush(Qt::NoBrush);
        // Draw as small diamond/square (4 px radius)
        double r = 3.0;
        p.drawRect(QRectF(sp.x() - r, sp.y() - r, 2*r, 2*r));

        // Draw node name if assigned
        if (!nd.name.isEmpty()) {
            p.setPen(kColorText);
            QFont f = p.font();
            f.setPointSize(7);
            p.setFont(f);
            p.drawText(QPointF(sp.x() + r + 2, sp.y() - 2), nd.name);
        }
    }
}

void DrawingWidget::drawSegments(QPainter &p)
{
    for (const auto &seg : m_doc->segments) {
        if (seg.hidden) continue;
        if (seg.n0 < 0 || seg.n0 >= (int)m_doc->nodes.size()) continue;
        if (seg.n1 < 0 || seg.n1 >= (int)m_doc->nodes.size()) continue;

        const FNode &nd0 = m_doc->nodes[seg.n0];
        const FNode &nd1 = m_doc->nodes[seg.n1];
        QPointF sp0 = drawingToScreen(nd0.x, nd0.y);
        QPointF sp1 = drawingToScreen(nd1.x, nd1.y);

        QColor c = seg.isSelected ? kColorSelected : kColorSegment;
        p.setPen(QPen(c, 1.5));
        p.drawLine(sp0, sp1);
    }
}

void DrawingWidget::drawArcSegments(QPainter &p)
{
    for (const auto &arc : m_doc->arcSegments) {
        if (arc.hidden) continue;
        if (arc.n0 < 0 || arc.n0 >= (int)m_doc->nodes.size()) continue;
        if (arc.n1 < 0 || arc.n1 >= (int)m_doc->nodes.size()) continue;

        const FNode &nd0 = m_doc->nodes[arc.n0];
        const FNode &nd1 = m_doc->nodes[arc.n1];

        QColor c = arc.isSelected ? kColorSelected : kColorArc;
        p.setPen(QPen(c, 1.5));

        // Compute arc center and radius from two endpoints and arc angle
        double x0 = nd0.x, y0 = nd0.y;
        double x1 = nd1.x, y1 = nd1.y;
        double arcAngle = arc.arcLength * M_PI / 180.0;

        if (std::fabs(arcAngle) < 1e-6) {
            // Degenerate — draw as line
            p.drawLine(drawingToScreen(x0, y0), drawingToScreen(x1, y1));
            continue;
        }

        // Midpoint of chord
        double mx = (x0 + x1) / 2.0;
        double my = (y0 + y1) / 2.0;

        // Half-chord length
        double dx = x1 - x0, dy = y1 - y0;
        double halfChord = std::sqrt(dx*dx + dy*dy) / 2.0;
        if (halfChord < 1e-12) continue;

        // Distance from midpoint to center
        double R = halfChord / std::sin(arcAngle / 2.0);
        double d = R * std::cos(arcAngle / 2.0);

        // Normal direction (perpendicular to chord)
        double nx = -dy / (2.0 * halfChord);
        double ny = dx / (2.0 * halfChord);

        // Arc center
        double cx = mx + d * nx;
        double cy = my + d * ny;

        // Draw arc as polyline of small segments (CCW from n0 to n1)
        int nSeg = std::max(3, (int)std::ceil(arc.arcLength / arc.maxSideLength));
        double startAngle = std::atan2(y0 - cy, x0 - cx);
        double dAngle = arcAngle / nSeg;

        QPointF prev = drawingToScreen(x0, y0);
        for (int i = 1; i <= nSeg; i++) {
            double angle = startAngle + dAngle * i;
            double px = cx + std::fabs(R) * std::cos(angle);
            double py = cy + std::fabs(R) * std::sin(angle);
            QPointF curr = drawingToScreen(px, py);
            p.drawLine(prev, curr);
            prev = curr;
        }
    }
}

void DrawingWidget::drawBlockLabels(QPainter &p)
{
    for (const auto &blk : m_doc->blockLabels) {
        QPointF sp = drawingToScreen(blk.x, blk.y);
        QColor c = blk.isSelected ? kColorSelected : kColorBlockLabel;

        // Draw as small square
        double r = 3.0;
        p.setPen(QPen(c, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(sp.x() - r, sp.y() - r, 2*r, 2*r));

        // Draw magnetization direction arrow for magnet blocks
        // Check if the block's material has coercivity (H_c != 0)
        bool isMagnet = false;
        for (const auto &mat : m_doc->materialProps) {
            if (mat.blockName == blk.blockType && mat.H_c != 0.0) {
                isMagnet = true;
                break;
            }
        }
        if (isMagnet) {
            double arrowLen = 10.0;
            double rad = blk.magDir * M_PI / 180.0;
            // Drawing coords: +angle = CCW, but screen y is flipped
            double adx =  arrowLen * std::cos(rad);
            double ady = -arrowLen * std::sin(rad);  // flip y for screen
            QPointF tip(sp.x() + adx, sp.y() + ady);

            p.setPen(QPen(QColor(200, 0, 0), 1.5));
            p.drawLine(sp, tip);

            // Small arrowhead
            double headLen = 3.5;
            double headAngle = 2.5;  // ~143 degrees from shaft direction
            double ax = std::atan2(ady, adx);
            QPointF h1(tip.x() - headLen * std::cos(ax - 0.45),
                       tip.y() - headLen * std::sin(ax - 0.45));
            QPointF h2(tip.x() - headLen * std::cos(ax + 0.45),
                       tip.y() - headLen * std::sin(ax + 0.45));
            p.drawLine(tip, h1);
            p.drawLine(tip, h2);
        }

        // Draw block label text: name (bold) and/or material
        {
            p.setPen(kColorText);
            QFont f = p.font();
            f.setPointSize(8);
            double ty = sp.y();
            if (!blk.name.isEmpty()) {
                f.setBold(true);
                p.setFont(f);
                ty += 3;
                p.drawText(QPointF(sp.x() + r + 2, ty), blk.name);
                ty += 12;
                f.setBold(false);
            }
            if (blk.blockType != "<None>" && !blk.blockType.isEmpty()) {
                p.setFont(f);
                if (blk.name.isEmpty()) ty += 3;
                p.drawText(QPointF(sp.x() + r + 2, ty), blk.blockType);
            }
        }
    }
}

void DrawingWidget::drawMesh(QPainter &p)
{
    if (!m_doc->hasMesh) return;

    // Pre-compute screen coords for mesh nodes
    const int nNodes = (int)m_doc->meshNodes.size();
    std::vector<QPointF> sc(nNodes);
    for (int i = 0; i < nNodes; i++)
        sc[i] = drawingToScreen(m_doc->meshNodes[i].x, m_doc->meshNodes[i].y);

    // Collect visible edges into a batch
    std::vector<QLineF> lines;
    lines.reserve(m_doc->meshElements.size() * 2);

    for (const auto &el : m_doc->meshElements) {
        // Frustum cull
        const QPointF &s0 = sc[el.p[0]];
        const QPointF &s1 = sc[el.p[1]];
        const QPointF &s2 = sc[el.p[2]];
        double sxmin = std::min({s0.x(), s1.x(), s2.x()});
        double sxmax = std::max({s0.x(), s1.x(), s2.x()});
        double symin = std::min({s0.y(), s1.y(), s2.y()});
        double symax = std::max({s0.y(), s1.y(), s2.y()});
        if (sxmax < 0 || sxmin > width() || symax < 0 || symin > height())
            continue;

        for (int e = 0; e < 3; e++) {
            int i0 = el.p[e];
            int i1 = el.p[(e + 1) % 3];
            if (i0 > i1) continue; // skip duplicate shared edges
            lines.push_back(QLineF(sc[i0], sc[i1]));
        }
    }

    // Draw all mesh lines in one batch with AA off
    if (!lines.empty()) {
        bool wasAA = p.renderHints() & QPainter::Antialiasing;
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(kColorMesh, 0.5));
        p.drawLines(lines.data(), (int)lines.size());
        p.setRenderHint(QPainter::Antialiasing, wasAA);
    }
}

// ---------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------

void DrawingWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_doc) return;

    setFocus();  // Ensure keyboard events (Delete key) reach this widget

    double xd, yd;
    screenToDrawing(event->pos().x(), event->pos().y(), xd, yd);

    // Middle or right button: start panning (skip if view is locked)
    if ((event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) && !m_viewLocked) {
        m_panning = true;
        m_panStart = event->pos();
        m_panOx = m_ox;
        m_panOy = m_oy;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    // Left button: depends on edit mode
    if (event->button() == Qt::LeftButton) {
        switch (m_editMode) {
        case 0: // Pointer — select, drag-move, or rubber-band
        {
            bool shiftHeld = event->modifiers() & Qt::ShiftModifier;

            // Deselect all unless Shift is held
            if (!shiftHeld) {
                for (auto &nd : m_doc->nodes) nd.isSelected = false;
                for (auto &sg : m_doc->segments) sg.isSelected = false;
                for (auto &ar : m_doc->arcSegments) ar.isSelected = false;
                for (auto &bl : m_doc->blockLabels) bl.isSelected = false;
            }

            double tol = 5.0 / m_mag; // 5 pixel tolerance in drawing coords
            bool found = false;

            // Try nodes first (highest priority)
            int idx = m_doc->closestNode(xd, yd);
            if (idx >= 0 && m_doc->nodes[idx].distanceTo(xd, yd) < tol) {
                if (shiftHeld)
                    m_doc->nodes[idx].isSelected = !m_doc->nodes[idx].isSelected;
                else
                    m_doc->nodes[idx].isSelected = true;
                emit statusMessage(QString("Node %1 selected").arg(idx));
                found = true;
            }

            // Try block labels
            if (!found) {
                idx = m_doc->closestBlockLabel(xd, yd);
                if (idx >= 0 && m_doc->blockLabels[idx].distanceTo(xd, yd) < tol) {
                    if (shiftHeld)
                        m_doc->blockLabels[idx].isSelected = !m_doc->blockLabels[idx].isSelected;
                    else
                        m_doc->blockLabels[idx].isSelected = true;
                    emit statusMessage(QString("Block label %1 selected").arg(idx));
                    found = true;
                }
            }

            // Try segments (distance to line)
            if (!found) {
                double bestDist = tol;
                int bestSeg = -1;
                for (int i = 0; i < (int)m_doc->segments.size(); i++) {
                    const FSegment &seg = m_doc->segments[i];
                    if (seg.n0 < 0 || seg.n0 >= (int)m_doc->nodes.size()) continue;
                    if (seg.n1 < 0 || seg.n1 >= (int)m_doc->nodes.size()) continue;
                    double x0 = m_doc->nodes[seg.n0].x, y0 = m_doc->nodes[seg.n0].y;
                    double x1 = m_doc->nodes[seg.n1].x, y1 = m_doc->nodes[seg.n1].y;
                    double ldx = x1-x0, ldy = y1-y0;
                    double len2 = ldx*ldx + ldy*ldy;
                    double t = (len2 > 0) ? std::max(0.0, std::min(1.0,
                        ((xd-x0)*ldx + (yd-y0)*ldy) / len2)) : 0.0;
                    double px = x0 + t*ldx, py = y0 + t*ldy;
                    double dist = std::sqrt((xd-px)*(xd-px) + (yd-py)*(yd-py));
                    if (dist < bestDist) { bestDist = dist; bestSeg = i; }
                }
                if (bestSeg >= 0) {
                    if (shiftHeld)
                        m_doc->segments[bestSeg].isSelected = !m_doc->segments[bestSeg].isSelected;
                    else
                        m_doc->segments[bestSeg].isSelected = true;
                    emit statusMessage(QString("Segment %1 selected").arg(bestSeg));
                    found = true;
                }
            }

            // Try arc segments
            if (!found) {
                double bestDist = tol;
                int bestArc = -1;
                for (int i = 0; i < (int)m_doc->arcSegments.size(); i++) {
                    const FArcSegment &arc = m_doc->arcSegments[i];
                    if (arc.n0 < 0 || arc.n0 >= (int)m_doc->nodes.size()) continue;
                    if (arc.n1 < 0 || arc.n1 >= (int)m_doc->nodes.size()) continue;
                    double dist = distanceToArc(m_doc->nodes[arc.n0],
                                                m_doc->nodes[arc.n1], arc, xd, yd);
                    if (dist < bestDist) { bestDist = dist; bestArc = i; }
                }
                if (bestArc >= 0) {
                    if (shiftHeld)
                        m_doc->arcSegments[bestArc].isSelected = !m_doc->arcSegments[bestArc].isSelected;
                    else
                        m_doc->arcSegments[bestArc].isSelected = true;
                    emit statusMessage(QString("Arc segment %1 selected").arg(bestArc));
                    found = true;
                }
            }

            if (found) {
                // Clicked on an item — prepare for potential drag-move
                m_potentialDrag = true;
                m_dragging = false;
                m_dragPressPos = event->pos();
                m_dragStartX = xd;
                m_dragStartY = yd;
            } else {
                // Clicked empty space — start rubber-band selection
                m_rubberBand = true;
                m_rbStart = event->pos();
                m_rbEnd = event->pos();
                emit statusMessage(QString("(%1, %2)")
                    .arg(xd, 0, 'f', 4).arg(yd, 0, 'f', 4));
            }

            update();
            break;
        }
        case 1: // Add Node
            applySnap(xd, yd);
            if (m_doc->addNode(xd, yd, 1.0 / m_mag)) {
                emit statusMessage(QString("Node added at (%1, %2)")
                    .arg(xd, 0, 'f', 4).arg(yd, 0, 'f', 4));
            }
            update();
            break;

        case 2: // Add Segment (two-click)
            if (m_firstPoint < 0) {
                m_firstPoint = m_doc->closestNode(xd, yd);
                if (m_firstPoint >= 0) {
                    m_doc->nodes[m_firstPoint].isSelected = true;
                    emit statusMessage(QString("Select second node for segment..."));
                    update();
                }
            } else {
                int secondPoint = m_doc->closestNode(xd, yd);
                m_doc->nodes[m_firstPoint].isSelected = false;
                if (m_doc->addSegment(m_firstPoint, secondPoint)) {
                    emit statusMessage(tr("Segment added"));
                }
                m_firstPoint = -1;
                update();
            }
            break;

        case 3: // Add Arc Segment (two-click + dialog)
            if (m_firstPoint < 0) {
                m_firstPoint = m_doc->closestNode(xd, yd);
                if (m_firstPoint >= 0) {
                    m_doc->nodes[m_firstPoint].isSelected = true;
                    emit statusMessage(tr("Select second node for arc (CCW from first)..."));
                    update();
                }
            } else {
                int secondPoint = m_doc->closestNode(xd, yd);
                m_doc->nodes[m_firstPoint].isSelected = false;

                if (secondPoint >= 0 && secondPoint != m_firstPoint) {
                    // Show dialog for arc parameters (matching original FEMM behavior)
                    AddArcDialog dlg(m_doc, this);
                    if (dlg.exec() == QDialog::Accepted) {
                        if (m_doc->addArcSegment(m_firstPoint, secondPoint,
                                                  dlg.arcAngle(), dlg.maxSegment())) {
                            // Apply boundary marker if specified
                            if (!m_doc->arcSegments.empty()) {
                                m_doc->arcSegments.back().boundaryMarker = dlg.boundaryMarker();
                            }
                            emit statusMessage(QString("Arc segment added (%1 deg)")
                                .arg(dlg.arcAngle(), 0, 'f', 1));
                        }
                    }
                }
                m_firstPoint = -1;
                update();
            }
            break;

        case 4: // Add Block Label
            applySnap(xd, yd);
            if (m_doc->addBlockLabel(xd, yd, 1.0 / m_mag)) {
                emit statusMessage(QString("Block label added at (%1, %2)")
                    .arg(xd, 0, 'f', 4).arg(yd, 0, 'f', 4));
            }
            update();
            break;
        }
    }
}

void DrawingWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_doc || event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    double xd, yd;
    screenToDrawing(event->pos().x(), event->pos().y(), xd, yd);
    double tol = 5.0 / m_mag;

    // Check block labels first (most common to edit)
    int idx = m_doc->closestBlockLabel(xd, yd);
    if (idx >= 0 && m_doc->blockLabels[idx].distanceTo(xd, yd) < tol) {
        BlockPropsDialog dlg(m_doc, &m_doc->blockLabels[idx], this);
        if (dlg.exec() == QDialog::Accepted) {
            emit statusMessage(QString("Block label %1 updated").arg(idx));
            update();
        }
        return;
    }

    // Check nodes
    idx = m_doc->closestNode(xd, yd);
    if (idx >= 0 && m_doc->nodes[idx].distanceTo(xd, yd) < tol) {
        NodePropsDialog dlg(m_doc, &m_doc->nodes[idx], this);
        if (dlg.exec() == QDialog::Accepted) {
            emit statusMessage(QString("Node %1 updated").arg(idx));
            update();
        }
        return;
    }

    // Check segments (point-to-line distance)
    {
        double bestDist = tol;
        int bestSeg = -1;
        for (int i = 0; i < (int)m_doc->segments.size(); i++) {
            const FSegment &seg = m_doc->segments[i];
            if (seg.n0 < 0 || seg.n0 >= (int)m_doc->nodes.size()) continue;
            if (seg.n1 < 0 || seg.n1 >= (int)m_doc->nodes.size()) continue;
            double x0 = m_doc->nodes[seg.n0].x, y0 = m_doc->nodes[seg.n0].y;
            double x1 = m_doc->nodes[seg.n1].x, y1 = m_doc->nodes[seg.n1].y;
            double ldx = x1-x0, ldy = y1-y0;
            double len2 = ldx*ldx + ldy*ldy;
            double t = (len2 > 0) ? std::max(0.0, std::min(1.0,
                ((xd-x0)*ldx + (yd-y0)*ldy) / len2)) : 0.0;
            double px = x0 + t*ldx, py = y0 + t*ldy;
            double dist = std::sqrt((xd-px)*(xd-px) + (yd-py)*(yd-py));
            if (dist < bestDist) { bestDist = dist; bestSeg = i; }
        }
        if (bestSeg >= 0) {
            SegmentPropsDialog dlg(m_doc, &m_doc->segments[bestSeg], this);
            if (dlg.exec() == QDialog::Accepted) {
                emit statusMessage(QString("Segment %1 updated").arg(bestSeg));
                update();
            }
            return;
        }
    }

    // Check arc segments (point-to-arc distance)
    {
        double bestDist = tol;
        int bestArc = -1;
        for (int i = 0; i < (int)m_doc->arcSegments.size(); i++) {
            const FArcSegment &arc = m_doc->arcSegments[i];
            if (arc.n0 < 0 || arc.n0 >= (int)m_doc->nodes.size()) continue;
            if (arc.n1 < 0 || arc.n1 >= (int)m_doc->nodes.size()) continue;
            double dist = distanceToArc(m_doc->nodes[arc.n0],
                                        m_doc->nodes[arc.n1], arc, xd, yd);
            if (dist < bestDist) { bestDist = dist; bestArc = i; }
        }
        if (bestArc >= 0) {
            ArcPropsDialog dlg(m_doc, &m_doc->arcSegments[bestArc], this);
            if (dlg.exec() == QDialog::Accepted) {
                emit statusMessage(QString("Arc segment %1 updated").arg(bestArc));
                update();
            }
            return;
        }
    }

    QWidget::mouseDoubleClickEvent(event);
}

void DrawingWidget::mouseMoveEvent(QMouseEvent *event)
{
    double xd, yd;
    screenToDrawing(event->pos().x(), event->pos().y(), xd, yd);

    // Apply snap for display
    double sx = xd, sy = yd;
    applySnap(sx, sy);
    m_mx = sx;
    m_my = sy;

    emit coordinatesChanged(sx, sy);

    // Handle panning
    if (m_panning) {
        double dx = (double)(event->pos().x() - m_panStart.x()) / m_mag;
        double dy = (double)(event->pos().y() - m_panStart.y()) / m_mag;
        m_ox = m_panOx - dx;
        m_oy = m_panOy + dy;  // y is inverted
        update();
        return;
    }

    // Handle drag-move (pointer mode)
    if (m_potentialDrag && !m_dragging) {
        // Check if mouse moved enough to start dragging (3px threshold)
        QPoint delta = event->pos() - m_dragPressPos;
        if (delta.manhattanLength() > 3) {
            m_dragging = true;
            m_potentialDrag = false;
            setCursor(Qt::SizeAllCursor);
        }
    }
    if (m_dragging) {
        double dx = xd - m_dragStartX;
        double dy = yd - m_dragStartY;
        m_doc->translateSelected(dx, dy);
        m_dragStartX = xd;
        m_dragStartY = yd;
        update();
        return;
    }

    // Handle rubber-band selection
    if (m_rubberBand) {
        m_rbEnd = event->pos();
        update();
        return;
    }
}

void DrawingWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if ((event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) && m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // Finalize drag-move
        if (m_dragging) {
            m_dragging = false;
            m_potentialDrag = false;
            m_doc->isModified = true;
            m_doc->hasMesh = false;
            setCursor(Qt::ArrowCursor);
            emit statusMessage(tr("Moved selected items"));
            update();
            return;
        }
        m_potentialDrag = false;

        // Finalize rubber-band selection
        if (m_rubberBand) {
            m_rubberBand = false;
            bool shiftHeld = event->modifiers() & Qt::ShiftModifier;

            // Convert screen rect to drawing coords
            double x0d, y0d, x1d, y1d;
            screenToDrawing(m_rbStart.x(), m_rbStart.y(), x0d, y0d);
            screenToDrawing(m_rbEnd.x(), m_rbEnd.y(), x1d, y1d);
            double xmin = std::min(x0d, x1d), xmax = std::max(x0d, x1d);
            double ymin = std::min(y0d, y1d), ymax = std::max(y0d, y1d);

            if (!shiftHeld) {
                for (auto &nd : m_doc->nodes) nd.isSelected = false;
                for (auto &sg : m_doc->segments) sg.isSelected = false;
                for (auto &ar : m_doc->arcSegments) ar.isSelected = false;
                for (auto &bl : m_doc->blockLabels) bl.isSelected = false;
            }

            int count = 0;

            // Select nodes inside rect
            for (auto &nd : m_doc->nodes) {
                if (nd.x >= xmin && nd.x <= xmax && nd.y >= ymin && nd.y <= ymax) {
                    nd.isSelected = true;
                    count++;
                }
            }

            // Select block labels inside rect
            for (auto &bl : m_doc->blockLabels) {
                if (bl.x >= xmin && bl.x <= xmax && bl.y >= ymin && bl.y <= ymax) {
                    bl.isSelected = true;
                    count++;
                }
            }

            // Select segments with both endpoints inside rect
            for (auto &seg : m_doc->segments) {
                if (seg.n0 < 0 || seg.n0 >= (int)m_doc->nodes.size()) continue;
                if (seg.n1 < 0 || seg.n1 >= (int)m_doc->nodes.size()) continue;
                const auto &nd0 = m_doc->nodes[seg.n0];
                const auto &nd1 = m_doc->nodes[seg.n1];
                if (nd0.x >= xmin && nd0.x <= xmax && nd0.y >= ymin && nd0.y <= ymax &&
                    nd1.x >= xmin && nd1.x <= xmax && nd1.y >= ymin && nd1.y <= ymax) {
                    seg.isSelected = true;
                    count++;
                }
            }

            // Select arc segments with both endpoints inside rect
            for (auto &arc : m_doc->arcSegments) {
                if (arc.n0 < 0 || arc.n0 >= (int)m_doc->nodes.size()) continue;
                if (arc.n1 < 0 || arc.n1 >= (int)m_doc->nodes.size()) continue;
                const auto &nd0 = m_doc->nodes[arc.n0];
                const auto &nd1 = m_doc->nodes[arc.n1];
                if (nd0.x >= xmin && nd0.x <= xmax && nd0.y >= ymin && nd0.y <= ymax &&
                    nd1.x >= xmin && nd1.x <= xmax && nd1.y >= ymin && nd1.y <= ymax) {
                    arc.isSelected = true;
                    count++;
                }
            }

            emit statusMessage(QString("Selected %1 item(s)").arg(count));
            update();
            return;
        }
    }
}

void DrawingWidget::wheelEvent(QWheelEvent *event)
{
    if (m_viewLocked) return;

    int delta = event->angleDelta().y();
    if (delta == 0) return;

    bool ctrlHeld = event->modifiers() & Qt::ControlModifier;

    // Detect device type — on macOS even a mouse can report pixelDelta
    // (due to smooth scrolling), so pixelDelta is unreliable for detection.
    // Use the Qt input device type to distinguish mouse from trackpad.
    const QInputDevice *device = event->device();
    bool isTouchPad = device &&
        (device->type() == QInputDevice::DeviceType::TouchPad);

    // Zoom vs pan logic:
    //   - Ctrl modifier: always zoom
    //   - Mouse scroll wheel (any type): zoom
    //   - Trackpad two-finger scroll: pan
    bool shouldZoom = ctrlHeld || !isTouchPad;

    if (shouldZoom) {
        // Zoom centered on cursor position
        double xd, yd;
        QPoint pos = event->position().toPoint();
        screenToDrawing(pos.x(), pos.y(), xd, yd);

        // Smooth zoom: scale factor proportional to delta magnitude
        double factor = std::pow(1.005, (double)delta);
        m_mag *= factor;
        if (m_mag < 0.001) m_mag = 0.001;
        if (m_mag > 1e8) m_mag = 1e8;

        // Adjust origin so point under cursor stays fixed
        m_ox = xd - (double)pos.x() / m_mag;
        m_oy = yd - ((double)(height() - 1) - (double)pos.y()) / m_mag;
    } else {
        // Pan (trackpad two-finger scroll)
        double dx = event->pixelDelta().x();
        double dy = event->pixelDelta().y();
        m_ox -= dx / m_mag;
        m_oy += dy / m_mag;
    }

    update();
}

bool DrawingWidget::event(QEvent *ev)
{
    if (ev->type() == QEvent::NativeGesture && !m_viewLocked) {
        auto *gesture = static_cast<QNativeGestureEvent *>(ev);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            // value() is the scale delta (e.g. 0.02 means 2% zoom in)
            double scaleDelta = gesture->value();
            if (std::fabs(scaleDelta) < 1e-10) return true;

            QPointF pos = gesture->position();
            double xd, yd;
            screenToDrawing((int)pos.x(), (int)pos.y(), xd, yd);

            double factor = 1.0 + scaleDelta;
            m_mag *= factor;
            if (m_mag < 0.001) m_mag = 0.001;
            if (m_mag > 1e8) m_mag = 1e8;

            // Keep point under cursor fixed
            m_ox = xd - pos.x() / m_mag;
            m_oy = yd - ((double)(height() - 1) - pos.y()) / m_mag;

            update();
            return true;
        }
    }
    return QWidget::event(ev);
}

void DrawingWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_doc) { QWidget::keyPressEvent(event); return; }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        int count = m_doc->deleteSelected();
        if (count > 0) {
            emit statusMessage(QString("Deleted %1 item(s)").arg(count));
            update();
        }
        return;
    }

    QWidget::keyPressEvent(event);
}

void DrawingWidget::resizeEvent(QResizeEvent * /*event*/)
{
    update();
}
