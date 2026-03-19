// FEMM Qt 6 GUI — Geometry editor widget
#ifndef DRAWINGWIDGET_H
#define DRAWINGWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QPixmap>

class FemmeDocument;
class ResultsOverlayRenderer;
struct SlidingBand;

class DrawingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DrawingWidget(FemmeDocument *doc, QWidget *parent = nullptr);
    ~DrawingWidget() override;

    FemmeDocument *document() const { return m_doc; }

    // Edit mode: 0=pointer, 1=node, 2=segment, 3=arc, 4=blocklabel
    void setEditMode(int mode);
    int editMode() const { return m_editMode; }

    // Zoom/pan
    void zoomIn();
    void zoomOut();
    void zoomFit();

    // View state accessors (for save/restore)
    double viewOx() const { return m_ox; }
    double viewOy() const { return m_oy; }
    double viewMag() const { return m_mag; }
    void setViewTransform(double ox, double oy, double mag);

    // View lock — prevents zoom/pan from user input (used during motion sweep)
    void setViewLocked(bool locked) { m_viewLocked = locked; }
    bool isViewLocked() const { return m_viewLocked; }

    // Render the current view directly to an offscreen pixmap, ignoring any
    // cached sweep frame so callers always get the live overlay + geometry.
    QPixmap renderLiveFrame();

    // Results overlay
    void setResultsOverlay(ResultsOverlayRenderer *overlay);
    void setSlidingBand(const SlidingBand *band) { m_slidingBand = band; }
    ResultsOverlayRenderer *resultsOverlay() const { return m_overlay; }

    // Cached frame — during motion sweep the widget paints this pixmap
    // instead of live-rendering, so the display always shows a correct
    // overlay+geometry snapshot even while geometry is being moved for
    // the next solver step.
    void setCachedFrame(const QPixmap &frame) { m_cachedFrame = frame; }
    void clearCachedFrame() { m_cachedFrame = QPixmap(); }
    bool hasCachedFrame() const { return !m_cachedFrame.isNull(); }

    // Refresh display — handles both normal mode and motion sweep.
    // In normal mode: schedules a repaint via update().
    // During motion sweep: clears the cached frame, does a synchronous
    // live repaint, and re-caches the result so display toggles (density,
    // contours, mesh, legend, geometry) take effect immediately.
    void refreshDisplay();

    // Geometry visibility
    void setShowNodes(bool on) { m_showNodes = on; refreshDisplay(); }
    void setShowBlockLabels(bool on) { m_showBlockLabels = on; refreshDisplay(); }
    void setShowGeometry(bool on) { m_showGeometry = on; refreshDisplay(); }
    bool showNodes() const { return m_showNodes; }
    bool showBlockLabels() const { return m_showBlockLabels; }
    bool showGeometry() const { return m_showGeometry; }

signals:
    void coordinatesChanged(double x, double y);
    void statusMessage(const QString &msg);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Coordinate transforms
    QPointF drawingToScreen(double xd, double yd) const;
    void screenToDrawing(int xs, int ys, double &xd, double &yd) const;

    // Drawing helpers
    void paintScene(QPainter &p, bool allowCachedFrame);
    void drawGrid(QPainter &p);
    void drawOrigin(QPainter &p);
    void drawNodes(QPainter &p);
    void drawSegments(QPainter &p);
    void drawArcSegments(QPainter &p);
    void drawBlockLabels(QPainter &p);
    void drawMesh(QPainter &p);
    // Editing
    void deleteSelected();

    // Snap
    void applySnap(double &x, double &y) const;

    FemmeDocument *m_doc;

    // View transform: screen_x = mag * (xd - ox)
    //                 screen_y = height - 1 - mag * (yd - oy)
    double m_ox = 0.0, m_oy = 0.0;  // origin in drawing coords
    double m_mag = 100.0;             // zoom factor (pixels per unit)

    // Edit state
    int m_editMode = 0;
    int m_firstPoint = -1;  // for segment/arc (two-click operations)

    // Grid
    double m_gridSize = 0.1;
    bool m_showGrid = true;
    bool m_snapToGrid = true;
    bool m_showOrigin = true;

    // Geometry visibility
    bool m_showNodes = true;
    bool m_showBlockLabels = true;
    bool m_showGeometry = true;  // segments + arcs

    // View lock
    bool m_viewLocked = false;

    // Mouse tracking
    double m_mx = 0.0, m_my = 0.0;  // mouse pos in drawing coords
    bool m_panning = false;
    QPoint m_panStart;
    double m_panOx = 0.0, m_panOy = 0.0;

    // Drag-move state (pointer mode: move selected items)
    bool m_potentialDrag = false;    // mouse pressed on selected item, waiting for movement
    bool m_dragging = false;         // drag-move in progress
    QPoint m_dragPressPos;           // screen coords of initial press (for threshold)
    double m_dragStartX = 0.0, m_dragStartY = 0.0; // drawing coords at drag start

    // Rubber-band box selection
    bool m_rubberBand = false;
    QPoint m_rbStart;                // screen coords of box origin
    QPoint m_rbEnd;                  // screen coords of current corner

    // Results overlay (non-owning pointer, managed by MainWindow)
    ResultsOverlayRenderer *m_overlay = nullptr;

    // Sliding band interface circles (non-owning, set during motion sweep)
    const SlidingBand *m_slidingBand = nullptr;

    // Cached frame for motion sweep (null when not in use)
    QPixmap m_cachedFrame;
};

#endif // DRAWINGWIDGET_H
