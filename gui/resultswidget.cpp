// FEMM Qt 6 GUI — Post-processor results visualization widget
#include "resultswidget.h"
#include "resultsdoc.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QResizeEvent>
#include <cmath>

static const QColor kColorBackground(255, 255, 255);

// ---------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------

ResultsWidget::ResultsWidget(ResultsDocument *doc, QWidget *parent)
    : QWidget(parent), m_doc(doc)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);

    m_renderer.setDocument(m_doc);
}

ResultsWidget::~ResultsWidget() = default;

// ---------------------------------------------------------------
// Coordinate transforms
// ---------------------------------------------------------------

QPointF ResultsWidget::drawingToScreen(double xd, double yd) const
{
    double sx = m_mag * (xd - m_ox);
    double sy = (double)(height() - 1) - m_mag * (yd - m_oy);
    return QPointF(sx, sy);
}

void ResultsWidget::screenToDrawing(int xs, int ys, double &xd, double &yd) const
{
    xd = (double)xs / m_mag + m_ox;
    yd = ((double)(height() - 1) - (double)ys) / m_mag + m_oy;
}

// ---------------------------------------------------------------
// Zoom / Pan
// ---------------------------------------------------------------

void ResultsWidget::zoomIn()
{
    double cx = width() / 2.0;
    double cy = height() / 2.0;
    double xd, yd;
    screenToDrawing((int)cx, (int)cy, xd, yd);
    m_mag *= 2.0;
    m_ox = xd - cx / m_mag;
    m_oy = yd - ((double)(height() - 1) - cy) / m_mag;
    update();
}

void ResultsWidget::zoomOut()
{
    double cx = width() / 2.0;
    double cy = height() / 2.0;
    double xd, yd;
    screenToDrawing((int)cx, (int)cy, xd, yd);
    m_mag /= 2.0;
    if (m_mag < 0.001) m_mag = 0.001;
    m_ox = xd - cx / m_mag;
    m_oy = yd - ((double)(height() - 1) - cy) / m_mag;
    update();
}

void ResultsWidget::zoomFit()
{
    if (!m_doc) return;

    double xmin, ymin, xmax, ymax;
    if (!m_doc->getBoundingBox(xmin, ymin, xmax, ymax)) return;

    double dx = xmax - xmin, dy = ymax - ymin;
    if (dx < 1e-12) dx = 1.0;
    if (dy < 1e-12) dy = 1.0;

    // Fit model to 80% of viewport (10% margin on each side)
    double magX = (double)width() * 0.8 / dx;
    double magY = (double)height() * 0.8 / dy;
    m_mag = std::min(magX, magY);

    double modelCenterX = (xmin + xmax) / 2.0;
    double modelCenterY = (ymin + ymax) / 2.0;
    m_ox = modelCenterX - (double)width() / (2.0 * m_mag);
    m_oy = modelCenterY - (double)(height() - 1) / (2.0 * m_mag);
    update();
}

// ---------------------------------------------------------------
// Paint — delegates to shared renderer
// ---------------------------------------------------------------

void ResultsWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), kColorBackground);

    if (!m_doc) return;

    m_renderer.render(p, m_ox, m_oy, m_mag, width(), height());
}

// ---------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------

void ResultsWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_doc) return;

    // Middle or right button: pan
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        m_panning = true;
        m_panStart = event->pos();
        m_panOx = m_ox;
        m_panOy = m_oy;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    // Left button: point query
    if (event->button() == Qt::LeftButton) {
        double xd, yd;
        screenToDrawing(event->pos().x(), event->pos().y(), xd, yd);

        PointValues pv = m_doc->getPointValues(xd, yd);
        if (pv.valid) {
            double Bmag = std::sqrt(std::norm(pv.B1) + std::norm(pv.B2));
            double Hmag = std::sqrt(std::norm(pv.H1) + std::norm(pv.H2));
            emit statusMessage(QString("(%1, %2)  A=%3  |B|=%4 T  |H|=%5 A/m")
                .arg(xd, 0, 'f', 4).arg(yd, 0, 'f', 4)
                .arg(pv.A.real(), 0, 'e', 4)
                .arg(Bmag, 0, 'e', 4)
                .arg(Hmag, 0, 'e', 4));
        }
    }
}

void ResultsWidget::mouseMoveEvent(QMouseEvent *event)
{
    double xd, yd;
    screenToDrawing(event->pos().x(), event->pos().y(), xd, yd);
    emit coordinatesChanged(xd, yd);

    if (m_panning) {
        double dx = (double)(event->pos().x() - m_panStart.x()) / m_mag;
        double dy = (double)(event->pos().y() - m_panStart.y()) / m_mag;
        m_ox = m_panOx - dx;
        m_oy = m_panOy + dy;
        update();
    }
}

void ResultsWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if ((event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) && m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
    }
}

void ResultsWidget::wheelEvent(QWheelEvent *event)
{
    int delta = event->angleDelta().y();
    if (delta == 0) delta = event->pixelDelta().y();
    if (delta == 0) return;

    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom (pinch-to-zoom on macOS, Ctrl+scroll elsewhere)
        double xd, yd;
        QPoint pos = event->position().toPoint();
        screenToDrawing(pos.x(), pos.y(), xd, yd);

        double factor = std::pow(1.005, (double)delta);
        m_mag *= factor;
        if (m_mag < 0.001) m_mag = 0.001;
        if (m_mag > 1e8) m_mag = 1e8;

        m_ox = xd - (double)pos.x() / m_mag;
        m_oy = yd - ((double)(height() - 1) - (double)pos.y()) / m_mag;
    } else {
        // Pan (two-finger scroll on macOS, regular scroll elsewhere)
        double dx = event->pixelDelta().x();
        double dy = event->pixelDelta().y();
        if (dx == 0 && dy == 0) {
            dx = event->angleDelta().x();
            dy = event->angleDelta().y();
        }
        m_ox -= dx / m_mag;
        m_oy += dy / m_mag;
    }

    update();
}

bool ResultsWidget::event(QEvent *ev)
{
    if (ev->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(ev);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            double scaleDelta = gesture->value();
            if (std::fabs(scaleDelta) < 1e-10) return true;

            QPointF pos = gesture->position();
            double xd, yd;
            screenToDrawing((int)pos.x(), (int)pos.y(), xd, yd);

            double factor = 1.0 + scaleDelta;
            m_mag *= factor;
            if (m_mag < 0.001) m_mag = 0.001;
            if (m_mag > 1e8) m_mag = 1e8;

            m_ox = xd - pos.x() / m_mag;
            m_oy = yd - ((double)(height() - 1) - pos.y()) / m_mag;

            update();
            return true;
        }
    }
    return QWidget::event(ev);
}

void ResultsWidget::resizeEvent(QResizeEvent * /*event*/)
{
    update();
}
