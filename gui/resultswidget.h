// FEMM Qt 6 GUI — Post-processor results visualization widget
#ifndef RESULTSWIDGET_H
#define RESULTSWIDGET_H

#include "resultsdoc.h"
#include "resultsoverlay.h"
#include <QWidget>
#include <QPainter>

class ResultsDocument;

class ResultsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ResultsWidget(ResultsDocument *doc, QWidget *parent = nullptr);
    ~ResultsWidget() override;

    ResultsDocument *document() const { return m_doc; }

    // View controls
    void zoomIn();
    void zoomOut();
    void zoomFit();

    // Display options (delegate to renderer)
    void setShowMesh(bool on) { m_renderer.setShowMesh(on); update(); }
    void setShowDensity(DensityType type) { m_renderer.setShowDensity(type); update(); }
    void setShowContours(bool on) { m_renderer.setShowContours(on); update(); }
    void setNumContours(int n) { m_renderer.setNumContours(n); update(); }
    void setShowLegend(bool on) { m_renderer.setShowLegend(on); update(); }

    bool showMesh() const { return m_renderer.showMesh(); }
    DensityType densityType() const { return m_renderer.densityType(); }
    bool showContours() const { return m_renderer.showContours(); }
    int numContours() const { return m_renderer.numContours(); }
    bool showLegend() const { return m_renderer.showLegend(); }

signals:
    void coordinatesChanged(double x, double y);
    void statusMessage(const QString &msg);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Coordinate transforms
    QPointF drawingToScreen(double xd, double yd) const;
    void screenToDrawing(int xs, int ys, double &xd, double &yd) const;

    ResultsDocument *m_doc;
    ResultsOverlayRenderer m_renderer;

    // View transform
    double m_ox = 0.0, m_oy = 0.0;
    double m_mag = 100.0;

    // Mouse tracking
    bool m_panning = false;
    QPoint m_panStart;
    double m_panOx = 0.0, m_panOy = 0.0;
};

#endif // RESULTSWIDGET_H
