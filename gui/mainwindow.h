// FEMM Qt 6 GUI — Main Window (MDI container)
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMdiArea>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QActionGroup>
#include <QPlainTextEdit>
#include <QSplitter>

class DrawingWidget;
class FemmeDocument;
class MeshGenerator;
class SolverRunner;
class ResultsDocument;
class ResultsOverlayRenderer;
class MotionRunner;
class ModelDataDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString &path);

private slots:
    // File menu
    void onNewDocument();
    void onOpenDocument();
    void onSaveDocument();
    void onSaveAs();
    void onImportDXF();

    // Edit tools
    void onEditModeChanged(QAction *action);

    // Properties
    void onProblemDef();
    void onMaterials();
    void onBoundaries();
    void onCircuits();
    void onPointProps();
    void onModelData();

    // Mesh/Analysis
    void onCreateMesh();
    void onAdaptiveRefine();
    void onAddBoundaryRegion();
    void onAnalyze();
    void onViewResults();

    // Solver finished
    void onSolverFinished(bool success);

    // Move / Motion
    void onParametricMotion();
    void onMotionProgress(const QString &msg);
    void onMotionFinished(bool success, const QString &csvPath, const QString &videoPath);

    // View
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();

    // Overlay toggles
    void onToggleOverlayDensity(bool checked);
    void onToggleOverlayContours(bool checked);
    void onToggleOverlayMesh(bool checked);
    void onToggleOverlayLegend(bool checked);

    // Geometry visibility toggles
    void onToggleShowNodes(bool checked);
    void onToggleShowBlockLabels(bool checked);
    void onToggleShowGeometry(bool checked);

    // Rendering quality
    void onAAQualityChanged(QAction *action);

    // Status updates
    void updateCoordinates(double x, double y);
    void updateStatus(const QString &msg);

    // Log panel
    void toggleLogPanel(bool visible);

private:
    void createMenus();
    void createToolBars();
    void createStatusBar();
    void createLogPanel();
    void appendLog(const QString &msg);

    DrawingWidget *currentDrawing();
    FemmeDocument *currentDocument();

    // Load results overlay onto a DrawingWidget
    void loadResultsOverlay(DrawingWidget *dw);

    QMdiArea *mdiArea;
    QSplitter *m_splitter = nullptr;
    QPlainTextEdit *m_logPanel = nullptr;
    QAction *actShowLog = nullptr;

    // Toolbars
    QToolBar *fileToolBar;
    QToolBar *editToolBar;
    QToolBar *viewToolBar;
    QToolBar *meshToolBar;

    // Status bar labels
    QLabel *coordLabel;
    QLabel *statusLabel;

    // Edit mode actions
    QActionGroup *editModeGroup;
    QAction *actPointer;
    QAction *actNode;
    QAction *actSegment;
    QAction *actArc;
    QAction *actBlockLabel;

    // Overlay toggle actions
    QAction *actOverlayDensity = nullptr;
    QAction *actOverlayContours = nullptr;
    QAction *actOverlayMesh = nullptr;
    QAction *actOverlayLegend = nullptr;

    // Geometry visibility actions
    QAction *actShowNodes = nullptr;
    QAction *actShowBlockLabels = nullptr;
    QAction *actShowGeometry = nullptr;

    // Rendering quality actions
    QActionGroup *m_aaGroup = nullptr;
    QAction *actAANone = nullptr;
    QAction *actAALow = nullptr;
    QAction *actAAHigh = nullptr;
    QAction *actAAUltra = nullptr;
    QAction *actAAExtreme = nullptr;

    // Mesh and solver
    MeshGenerator *m_meshGen;
    SolverRunner *m_solver;

    // Results overlay (owned by MainWindow)
    ResultsOverlayRenderer *m_overlayRenderer = nullptr;
    ResultsDocument *m_overlayDoc = nullptr;

    // Auto-solve state tracking
    bool m_autoSolvePending = false;
    DrawingWidget *m_autoSolveTarget = nullptr;

    // Motion runner
    MotionRunner *m_motionRunner = nullptr;
    ModelDataDialog *m_modelDataDialog = nullptr;

    // Persistent view settings (saved/restored via QSettings)
    bool m_savedOverlayDensity = true;
    bool m_savedOverlayContours = true;
    bool m_savedOverlayMesh = false;
    bool m_savedOverlayLegend = true;
};

#endif // MAINWINDOW_H
