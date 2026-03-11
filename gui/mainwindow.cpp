// FEMM Qt 6 GUI — Main Window implementation
#include "mainwindow.h"
#include "drawingwidget.h"
#include "document.h"
#include "meshgen.h"
#include "solverrunner.h"
#include "resultsdoc.h"
#include "resultswidget.h"
#include "resultsoverlay.h"
#include "dialogs/problemdlg.h"
#include "dialogs/proplistdlg.h"
#include "dialogs/motiondialog.h"
#include "dialogs/adaptivedlg.h"
#include "dialogs/boundaryregiondlg.h"
#include "dialogs/modeldatadlg.h"
#include "adaptiverefine.h"
#include "motionrunner.h"
#include "motoroptimizer.h"
#include "dxfimporter.h"

#include <QMenuBar>
#include <QProgressDialog>
#include <QMenu>
#include <QToolBar>
#include <QStatusBar>
#include <QMdiSubWindow>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QApplication>
#include <QCloseEvent>
#include <QSettings>
#include <QDebug>
#include <QThread>
#include <QEventLoop>
#include <QTimer>
#include <QScrollBar>
#include <QTime>
#include <cstdio>

// Worker thread for running adaptive refinement off the main thread
class AdaptiveRefineThread : public QThread {
public:
    AdaptiveRefiner *refiner = nullptr;
    FemmeDocument *doc = nullptr;
    AdaptiveConfig config;
    bool success = false;
protected:
    void run() override {
        success = refiner->run(doc, config);
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("FEMM 4.2"));
    resize(1200, 800);

    // Central MDI area + log panel in a vertical splitter
    mdiArea = new QMdiArea(this);
    mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mdiArea->setViewMode(QMdiArea::TabbedView);
    mdiArea->setTabsClosable(true);

    createLogPanel();

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(mdiArea);
    m_splitter->addWidget(m_logPanel);
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);

    // Mesh generator
    m_meshGen = new MeshGenerator(this);
    connect(m_meshGen, &MeshGenerator::progress,
            this, &MainWindow::updateStatus);

    // Solver runner
    m_solver = new SolverRunner(this);
    connect(m_solver, &SolverRunner::progress,
            this, &MainWindow::updateStatus);
    connect(m_solver, &SolverRunner::finished,
            this, &MainWindow::onSolverFinished);

    createMenus();
    createToolBars();
    createStatusBar();

    // Restore persistent view settings
    QSettings settings;
    m_savedOverlayDensity = settings.value("view/overlayDensity", true).toBool();
    m_savedOverlayContours = settings.value("view/overlayContours", true).toBool();
    m_savedOverlayMesh = settings.value("view/overlayMesh", false).toBool();
    m_savedOverlayLegend = settings.value("view/overlayLegend", true).toBool();

    // Restore geometry visibility settings and sync menu actions
    bool showNodes = settings.value("view/showNodes", true).toBool();
    bool showBlockLabels = settings.value("view/showBlockLabels", true).toBool();
    bool showGeometry = settings.value("view/showGeometry", true).toBool();
    actShowNodes->setChecked(showNodes);
    actShowBlockLabels->setChecked(showBlockLabels);
    actShowGeometry->setChecked(showGeometry);

    // Restore AA quality setting
    int aaVal = settings.value("view/aaQuality", (int)AAQuality::Low).toInt();
    if (aaVal == (int)AAQuality::None)        actAANone->setChecked(true);
    else if (aaVal == (int)AAQuality::High)   actAAHigh->setChecked(true);
    else if (aaVal == (int)AAQuality::Ultra)  actAAUltra->setChecked(true);
    else if (aaVal == (int)AAQuality::Extreme) actAAExtreme->setChecked(true);
    else                                       actAALow->setChecked(true);

    // Restore log panel state and set initial splitter sizes.
    // Default: visible at one-line height (~24px).
    bool logVisible = settings.value("view/showLogPanel", true).toBool();
    int logHeight = settings.value("view/logPanelHeight", 24).toInt();
    m_logPanel->setVisible(logVisible);
    if (actShowLog) actShowLog->setChecked(logVisible);
    m_splitter->setSizes({height() - logHeight, logHeight});
}

MainWindow::~MainWindow()
{
    // Null after delete so the lambda in openFile() doesn't double-free
    // when DrawingWidget::destroyed fires during child destruction.
    delete m_overlayRenderer;
    m_overlayRenderer = nullptr;
    delete m_overlayDoc;
    m_overlayDoc = nullptr;
}

// ---------------------------------------------------------------
// Menu creation
// ---------------------------------------------------------------

void MainWindow::createMenus()
{
    // ---- File menu ----
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *actNew = fileMenu->addAction(tr("&New"), this, &MainWindow::onNewDocument);
    actNew->setShortcut(QKeySequence::New);

    QAction *actOpen = fileMenu->addAction(tr("&Open..."), this, &MainWindow::onOpenDocument);
    actOpen->setShortcut(QKeySequence::Open);

    QAction *actSave = fileMenu->addAction(tr("&Save"), this, &MainWindow::onSaveDocument);
    actSave->setShortcut(QKeySequence::Save);

    fileMenu->addAction(tr("Save &As..."), this, &MainWindow::onSaveAs);
    fileMenu->addAction(tr("Import &DXF..."), this, &MainWindow::onImportDXF);
    fileMenu->addSeparator();

    QAction *actQuit = fileMenu->addAction(tr("E&xit"), qApp, &QApplication::closeAllWindows);
    actQuit->setShortcut(QKeySequence::Quit);

    // ---- Edit menu ----
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));

    editModeGroup = new QActionGroup(this);
    editModeGroup->setExclusive(true);
    connect(editModeGroup, &QActionGroup::triggered,
            this, &MainWindow::onEditModeChanged);

    actPointer = editMenu->addAction(tr("&Pointer Mode"));
    actPointer->setCheckable(true);
    actPointer->setChecked(true);
    actPointer->setData(0);
    actPointer->setShortcut(Qt::Key_Escape);
    editModeGroup->addAction(actPointer);

    actNode = editMenu->addAction(tr("Add &Node"));
    actNode->setCheckable(true);
    actNode->setData(1);
    actNode->setShortcut(Qt::Key_N);
    editModeGroup->addAction(actNode);

    actSegment = editMenu->addAction(tr("Add &Segment"));
    actSegment->setCheckable(true);
    actSegment->setData(2);
    actSegment->setShortcut(Qt::Key_S);
    editModeGroup->addAction(actSegment);

    actArc = editMenu->addAction(tr("Add &Arc Segment"));
    actArc->setCheckable(true);
    actArc->setData(3);
    actArc->setShortcut(Qt::Key_A);
    editModeGroup->addAction(actArc);

    actBlockLabel = editMenu->addAction(tr("Add &Block Label"));
    actBlockLabel->setCheckable(true);
    actBlockLabel->setData(4);
    actBlockLabel->setShortcut(Qt::Key_B);
    editModeGroup->addAction(actBlockLabel);

    // ---- View menu ----
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *actZIn = viewMenu->addAction(tr("Zoom &In"), this, &MainWindow::onZoomIn);
    actZIn->setShortcut(QKeySequence::ZoomIn);
    QAction *actZOut = viewMenu->addAction(tr("Zoom &Out"), this, &MainWindow::onZoomOut);
    actZOut->setShortcut(QKeySequence::ZoomOut);
    viewMenu->addAction(tr("Zoom to &Fit"), this, &MainWindow::onZoomFit);

    // Overlay toggles
    viewMenu->addSeparator();

    actOverlayDensity = viewMenu->addAction(tr("Overlay &Density Plot"));
    actOverlayDensity->setCheckable(true);
    actOverlayDensity->setChecked(false);
    actOverlayDensity->setEnabled(false);
    connect(actOverlayDensity, &QAction::toggled,
            this, &MainWindow::onToggleOverlayDensity);

    actOverlayContours = viewMenu->addAction(tr("Overlay &Contours"));
    actOverlayContours->setCheckable(true);
    actOverlayContours->setChecked(false);
    actOverlayContours->setEnabled(false);
    connect(actOverlayContours, &QAction::toggled,
            this, &MainWindow::onToggleOverlayContours);

    actOverlayMesh = viewMenu->addAction(tr("Overlay &Mesh"));
    actOverlayMesh->setCheckable(true);
    actOverlayMesh->setChecked(false);
    actOverlayMesh->setEnabled(false);
    connect(actOverlayMesh, &QAction::toggled,
            this, &MainWindow::onToggleOverlayMesh);

    actOverlayLegend = viewMenu->addAction(tr("Overlay &Legend"));
    actOverlayLegend->setCheckable(true);
    actOverlayLegend->setChecked(false);
    actOverlayLegend->setEnabled(false);
    connect(actOverlayLegend, &QAction::toggled,
            this, &MainWindow::onToggleOverlayLegend);

    // Geometry visibility toggles
    viewMenu->addSeparator();

    actShowGeometry = viewMenu->addAction(tr("Show &Geometry"));
    actShowGeometry->setCheckable(true);
    actShowGeometry->setChecked(true);
    connect(actShowGeometry, &QAction::toggled,
            this, &MainWindow::onToggleShowGeometry);

    actShowNodes = viewMenu->addAction(tr("Show &Nodes"));
    actShowNodes->setCheckable(true);
    actShowNodes->setChecked(true);
    connect(actShowNodes, &QAction::toggled,
            this, &MainWindow::onToggleShowNodes);

    actShowBlockLabels = viewMenu->addAction(tr("Show &Block Labels"));
    actShowBlockLabels->setCheckable(true);
    actShowBlockLabels->setChecked(true);
    connect(actShowBlockLabels, &QAction::toggled,
            this, &MainWindow::onToggleShowBlockLabels);

    // Log panel toggle
    viewMenu->addSeparator();
    actShowLog = viewMenu->addAction(tr("Show &Log Panel"));
    actShowLog->setCheckable(true);
    actShowLog->setChecked(true);
    connect(actShowLog, &QAction::toggled,
            this, &MainWindow::toggleLogPanel);

    // Rendering submenu
    viewMenu->addSeparator();
    QMenu *renderMenu = viewMenu->addMenu(tr("&Rendering"));

    m_aaGroup = new QActionGroup(this);
    m_aaGroup->setExclusive(true);
    connect(m_aaGroup, &QActionGroup::triggered,
            this, &MainWindow::onAAQualityChanged);

    actAANone = renderMenu->addAction(tr("Anti-Aliasing: &Off"));
    actAANone->setCheckable(true);
    actAANone->setData((int)AAQuality::None);
    m_aaGroup->addAction(actAANone);

    actAALow = renderMenu->addAction(tr("Anti-Aliasing: &Low (Lines Only)"));
    actAALow->setCheckable(true);
    actAALow->setData((int)AAQuality::Low);
    m_aaGroup->addAction(actAALow);

    actAAHigh = renderMenu->addAction(tr("Anti-Aliasing: &High (2x SSAA)"));
    actAAHigh->setCheckable(true);
    actAAHigh->setData((int)AAQuality::High);
    m_aaGroup->addAction(actAAHigh);

    actAAUltra = renderMenu->addAction(tr("Anti-Aliasing: &Ultra (4x SSAA)"));
    actAAUltra->setCheckable(true);
    actAAUltra->setData((int)AAQuality::Ultra);
    m_aaGroup->addAction(actAAUltra);

    actAAExtreme = renderMenu->addAction(tr("Anti-Aliasing: &Extreme (8x SSAA)"));
    actAAExtreme->setCheckable(true);
    actAAExtreme->setData((int)AAQuality::Extreme);
    m_aaGroup->addAction(actAAExtreme);

    // ---- Properties menu ----
    QMenu *propsMenu = menuBar()->addMenu(tr("&Properties"));
    propsMenu->addAction(tr("Problem &Definition..."), this, &MainWindow::onProblemDef);
    propsMenu->addSeparator();
    propsMenu->addAction(tr("&Materials..."), this, &MainWindow::onMaterials);
    propsMenu->addAction(tr("&Boundary Conditions..."), this, &MainWindow::onBoundaries);
    propsMenu->addAction(tr("&Circuits..."), this, &MainWindow::onCircuits);
    propsMenu->addAction(tr("&Point Properties..."), this, &MainWindow::onPointProps);
    propsMenu->addSeparator();
    propsMenu->addAction(tr("Model &Data..."), this, &MainWindow::onModelData);

    // ---- Mesh menu ----
    QMenu *meshMenu = menuBar()->addMenu(tr("&Mesh"));
    meshMenu->addAction(tr("&Create Mesh"), this, &MainWindow::onCreateMesh);
    meshMenu->addAction(tr("&H-Adaptive Refinement..."), this, &MainWindow::onAdaptiveRefine);
    meshMenu->addSeparator();
    meshMenu->addAction(tr("Add &Boundary Region..."), this, &MainWindow::onAddBoundaryRegion);

    // ---- Analysis menu ----
    QMenu *analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    analysisMenu->addAction(tr("&Analyze"), this, &MainWindow::onAnalyze);
    analysisMenu->addAction(tr("&View Results"), this, &MainWindow::onViewResults);

    // ---- Move menu ----
    QMenu *moveMenu = menuBar()->addMenu(tr("&Move"));
    moveMenu->addAction(tr("&Parametric Motion..."), this, &MainWindow::onParametricMotion);
}

// ---------------------------------------------------------------
// Toolbar creation
// ---------------------------------------------------------------

void MainWindow::createToolBars()
{
    // File toolbar
    fileToolBar = addToolBar(tr("File"));
    fileToolBar->setIconSize(QSize(24, 24));
    fileToolBar->addAction(actPointer);

    // Edit toolbar (draw modes)
    editToolBar = addToolBar(tr("Edit"));
    editToolBar->addAction(actNode);
    editToolBar->addAction(actSegment);
    editToolBar->addAction(actArc);
    editToolBar->addAction(actBlockLabel);

    // View toolbar
    viewToolBar = addToolBar(tr("View"));

    // Mesh toolbar
    meshToolBar = addToolBar(tr("Mesh"));
}

// ---------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------

void MainWindow::createStatusBar()
{
    coordLabel = new QLabel(tr("(0.0000, 0.0000)"));
    coordLabel->setMinimumWidth(200);
    coordLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    statusLabel = new QLabel(tr("Ready"));
    statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    statusBar()->addWidget(coordLabel);
    statusBar()->addWidget(statusLabel, 1);

    // Lock the status bar height so that variable-length solver progress
    // text doesn't cause the main window to resize on every update.
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setFixedHeight(statusBar()->sizeHint().height());
}

// ---------------------------------------------------------------
// Log panel
// ---------------------------------------------------------------

void MainWindow::createLogPanel()
{
    m_logPanel = new QPlainTextEdit();
    m_logPanel->setReadOnly(true);
    m_logPanel->setMaximumBlockCount(5000);
    m_logPanel->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_logPanel->setMinimumHeight(24);

    // Monospace font for log readability
    QFont mono = QFont("Menlo", 11);
    mono.setStyleHint(QFont::Monospace);
    m_logPanel->setFont(mono);

    // Set placeholder text
    m_logPanel->setPlaceholderText(tr("Solver log output will appear here..."));
}

void MainWindow::appendLog(const QString &msg)
{
    if (!m_logPanel) return;

    // Split multi-line messages and append each line with timestamp
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    const QStringList lines = msg.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            m_logPanel->appendPlainText(QString("[%1] %2").arg(timestamp, trimmed));
    }

    // Auto-scroll to bottom
    QScrollBar *sb = m_logPanel->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::toggleLogPanel(bool visible)
{
    if (m_logPanel) {
        m_logPanel->setVisible(visible);
        if (visible && m_splitter) {
            // If panel was hidden, give it some initial height
            QList<int> sizes = m_splitter->sizes();
            if (sizes.size() == 2 && sizes[1] == 0) {
                sizes[0] -= 150;
                sizes[1] = 150;
                m_splitter->setSizes(sizes);
            }
        }
    }
    QSettings settings;
    settings.setValue("view/showLogPanel", visible);
}

// ---------------------------------------------------------------
// Helper: get current drawing / document
// ---------------------------------------------------------------

DrawingWidget *MainWindow::currentDrawing()
{
    QMdiSubWindow *sw = mdiArea->activeSubWindow();
    if (!sw) return nullptr;
    return qobject_cast<DrawingWidget *>(sw->widget());
}

FemmeDocument *MainWindow::currentDocument()
{
    DrawingWidget *dw = currentDrawing();
    if (!dw) return nullptr;
    return dw->document();
}

// ---------------------------------------------------------------
// File slots
// ---------------------------------------------------------------

void MainWindow::onNewDocument()
{
    auto *doc = new FemmeDocument();
    auto *drawing = new DrawingWidget(doc);
    connect(drawing, &DrawingWidget::coordinatesChanged,
            this, &MainWindow::updateCoordinates);
    connect(drawing, &DrawingWidget::statusMessage,
            this, &MainWindow::updateStatus);

    // Apply saved geometry visibility
    drawing->setShowNodes(actShowNodes->isChecked());
    drawing->setShowBlockLabels(actShowBlockLabels->isChecked());
    drawing->setShowGeometry(actShowGeometry->isChecked());

    QMdiSubWindow *sub = mdiArea->addSubWindow(drawing);
    sub->setWindowTitle(tr("Untitled"));
    sub->show();

    // Defer zoomFit until the tabbed layout has assigned the final size.
    QTimer::singleShot(0, drawing, &DrawingWidget::zoomFit);
}

void MainWindow::onOpenDocument()
{
    QSettings settings;
    QString lastDir = settings.value("file/lastDirectory", QString()).toString();

    QString path = QFileDialog::getOpenFileName(
        this, tr("Open FEMM File"), lastDir,
        tr("FEMM Files (*.fem);;All Files (*)"));
    if (path.isEmpty()) return;

    // Remember the directory for next time
    settings.setValue("file/lastDirectory", QFileInfo(path).absolutePath());

    openFile(path);
}

void MainWindow::openFile(const QString &path)
{
    auto *doc = new FemmeDocument();
    if (!doc->loadFromFile(path)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not open file:\n%1").arg(path));
        delete doc;
        return;
    }

    // Remember this file so the app can reopen it on next launch
    QSettings settings;
    settings.setValue("file/lastOpenedFile", path);

    auto *drawing = new DrawingWidget(doc);
    connect(drawing, &DrawingWidget::coordinatesChanged,
            this, &MainWindow::updateCoordinates);
    connect(drawing, &DrawingWidget::statusMessage,
            this, &MainWindow::updateStatus);

    // Clean up overlay if the drawing widget is destroyed
    connect(drawing, &QObject::destroyed, this, [this, drawing]() {
        if (m_autoSolveTarget == drawing) {
            m_autoSolveTarget = nullptr;
            m_autoSolvePending = false;
        }
        if (m_overlayRenderer && drawing->resultsOverlay() == m_overlayRenderer) {
            delete m_overlayRenderer;
            m_overlayRenderer = nullptr;
            delete m_overlayDoc;
            m_overlayDoc = nullptr;
            actOverlayDensity->setEnabled(false);
            actOverlayContours->setEnabled(false);
            actOverlayMesh->setEnabled(false);
            actOverlayLegend->setEnabled(false);
        }
    });

    // Apply saved geometry visibility
    drawing->setShowNodes(actShowNodes->isChecked());
    drawing->setShowBlockLabels(actShowBlockLabels->isChecked());
    drawing->setShowGeometry(actShowGeometry->isChecked());

    QMdiSubWindow *sub = mdiArea->addSubWindow(drawing);
    sub->setWindowTitle(doc->filePath());
    // In TabbedView mode the MDI area sizes the subwindow automatically.
    // Don't call sub->resize() — it causes a visible size jump as the
    // layout manager immediately overrides the requested size.
    sub->show();

    // Defer zoomFit and auto-mesh/solve until the widget has received its
    // final layout size.  Without this, zoomFit() reads the widget's
    // pre-layout dimensions, then the layout settles to a different size
    // and the view jumps again, potentially multiple times.
    QTimer::singleShot(0, this, [this, drawing, path]() {
        drawing->zoomFit();
        updateStatus(QString("Loaded: %1 nodes, %2 segments, %3 arcs, %4 block labels")
            .arg(drawing->document()->nodes.size())
            .arg(drawing->document()->segments.size())
            .arg(drawing->document()->arcSegments.size())
            .arg(drawing->document()->blockLabels.size()));

        FemmeDocument *doc = drawing->document();

        // --- Auto mesh + solve ---
        // Check if an .ans file already exists and is up to date
        QString ansPath = doc->filePath();
        if (ansPath.endsWith(".fem", Qt::CaseInsensitive))
            ansPath = ansPath.left(ansPath.length() - 4) + ".ans";
        else
            ansPath += ".ans";

        QFileInfo femInfo(path);
        QFileInfo ansInfo(ansPath);

        if (ansInfo.exists() && ansInfo.lastModified() >= femInfo.lastModified()) {
            // Results are up to date, just load overlay directly
            loadResultsOverlay(drawing);
            return;
        }

        // Need to mesh and solve
        if (m_solver->isRunning()) {
            updateStatus(tr("Solver busy — skipping auto-solve."));
            return;
        }

        updateStatus(tr("Auto-meshing..."));
        if (!m_meshGen->generateMesh(doc)) {
            updateStatus(tr("Auto-mesh failed: ") + m_meshGen->lastError());
            return;
        }
        drawing->update();
        updateStatus(QString("Auto-mesh: %1 nodes, %2 elements. Starting solver...")
            .arg(doc->meshNodes.size()).arg(doc->meshElements.size()));

        // Save before solving (solver reads from disk)
        if (!doc->saveToFile(doc->filePath())) {
            updateStatus(tr("Could not save file before auto-solve."));
            return;
        }

        // Track which drawing widget the auto-solve is for
        m_autoSolvePending = true;
        m_autoSolveTarget = drawing;

        m_solver->runSolver(doc);
    });
}

void MainWindow::onSaveDocument()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) return;

    if (doc->filePath().isEmpty()) {
        onSaveAs();
        return;
    }
    if (!doc->saveToFile(doc->filePath())) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not save file."));
    } else {
        updateStatus(tr("File saved."));
    }
}

void MainWindow::onSaveAs()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) return;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Save FEMM File"), QString(),
        tr("FEMM Files (*.fem);;All Files (*)"));
    if (path.isEmpty()) return;

    if (!doc->saveToFile(path)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not save file."));
        return;
    }

    QMdiSubWindow *sw = mdiArea->activeSubWindow();
    if (sw) sw->setWindowTitle(path);
    updateStatus(tr("File saved."));
}

void MainWindow::onImportDXF()
{
    FemmeDocument *doc = currentDocument();
    DrawingWidget *dw = currentDrawing();
    if (!doc || !dw) {
        QMessageBox::information(this, tr("Import DXF"),
            tr("Please create or open a document first."));
        return;
    }

    // File dialog
    QSettings settings;
    QString lastDir = settings.value("file/lastDxfDirectory",
                       settings.value("file/lastDirectory", QString())).toString();

    QString path = QFileDialog::getOpenFileName(
        this, tr("Import DXF File"), lastDir,
        tr("DXF Files (*.dxf);;All Files (*)"));
    if (path.isEmpty()) return;

    settings.setValue("file/lastDxfDirectory", QFileInfo(path).absolutePath());

    // Phase 1: parse DXF into temporary lists
    DxfImporter importer(doc);
    if (!importer.parseFile(path)) {
        QMessageBox::warning(this, tr("Import DXF"), importer.errorMessage());
        return;
    }

    // Phase 2: show tolerance dialog
    double defTol = importer.defaultTolerance();
    bool ok;
    double tol = QInputDialog::getDouble(
        this, tr("DXF Import Tolerance"),
        tr("Node merge tolerance:"),
        defTol,        // default
        0.0,           // min
        1e6,           // max
        6,             // decimals
        &ok);
    if (!ok) return;  // user cancelled

    // Phase 3: merge geometry into document
    importer.mergeIntoDocument(tol);

    // Refresh display
    dw->update();
    dw->zoomFit();

    updateStatus(tr("DXF imported: %1 nodes, %2 segments, %3 arcs")
        .arg(importer.nodesAdded())
        .arg(importer.segmentsAdded())
        .arg(importer.arcsAdded()));
}

// ---------------------------------------------------------------
// Edit mode
// ---------------------------------------------------------------

void MainWindow::onEditModeChanged(QAction *action)
{
    DrawingWidget *dw = currentDrawing();
    if (!dw) return;
    dw->setEditMode(action->data().toInt());
}

// ---------------------------------------------------------------
// Properties
// ---------------------------------------------------------------

void MainWindow::onProblemDef()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }
    ProblemDialog dlg(doc, this);
    if (dlg.exec() == QDialog::Accepted) {
        updateStatus(tr("Problem definition updated."));
    }
}

void MainWindow::onMaterials()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }
    PropertyListDialog dlg(doc, PropertyListDialog::Materials, this);
    dlg.exec();
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->update();  // block label text may have changed
}

void MainWindow::onBoundaries()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }
    PropertyListDialog dlg(doc, PropertyListDialog::Boundaries, this);
    dlg.exec();
}

void MainWindow::onCircuits()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }
    PropertyListDialog dlg(doc, PropertyListDialog::Circuits, this);
    dlg.exec();
}

void MainWindow::onPointProps()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }
    PropertyListDialog dlg(doc, PropertyListDialog::PointProps, this);
    dlg.exec();
}

void MainWindow::onModelData()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }

    // Reuse or create the non-modal dialog
    if (m_modelDataDialog) {
        m_modelDataDialog->refresh();
        m_modelDataDialog->raise();
        m_modelDataDialog->activateWindow();
        return;
    }

    m_modelDataDialog = new ModelDataDialog(doc, currentDrawing(), this);
    connect(m_modelDataDialog, &QDialog::destroyed, this, [this]() {
        m_modelDataDialog = nullptr;
    });
    m_modelDataDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_modelDataDialog->show();
}

// ---------------------------------------------------------------
// Mesh / Analysis
// ---------------------------------------------------------------

void MainWindow::onCreateMesh()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }

    // Auto-save if needed (standalone solver reads .fem from disk)
    if (doc->filePath().isEmpty()) {
        updateStatus(tr("Please save the document before meshing."));
        onSaveAs();
        if (doc->filePath().isEmpty()) return;
    }

    // Use in-process Triangle library (same path as adaptive refinement).
    // This is more reliable than the subprocess path and doesn't require
    // the triangle executable.
    std::vector<MeshEdge> edges;
    if (m_meshGen->generateMeshInProcess(doc, edges)) {
        doc->hasMesh = true;

        // Also write mesh files to disk for the standalone solver
        if (!doc->filePath().isEmpty()) {
            QString basePath = doc->filePath();
            if (basePath.endsWith(".fem", Qt::CaseInsensitive))
                basePath.chop(4);
            doc->writeMeshFiles(basePath, edges);
        }

        updateStatus(QString("Mesh: %1 nodes, %2 elements")
            .arg(doc->meshNodes.size()).arg(doc->meshElements.size()));
        DrawingWidget *dw = currentDrawing();
        if (dw) {
            // Clear stale results overlay so the new mesh wireframe is visible
            dw->setResultsOverlay(nullptr);
            delete m_overlayRenderer;
            m_overlayRenderer = nullptr;
            delete m_overlayDoc;
            m_overlayDoc = nullptr;
            dw->update();
        }
    } else {
        QMessageBox::warning(this, tr("Mesh Error"),
            m_meshGen->lastError());
    }
}

void MainWindow::onAdaptiveRefine()
{
    FemmeDocument *doc = currentDocument();
    DrawingWidget *dw = currentDrawing();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }

    // Auto-save if needed
    if (doc->filePath().isEmpty()) {
        updateStatus(tr("Please save the document before adaptive refinement."));
        onSaveAs();
        if (doc->filePath().isEmpty()) return;
    }

    // Show settings dialog
    AdaptiveDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    AdaptiveConfig config = dlg.config();

    // Clear any stale results overlay so the user sees the mesh wireframe
    // during adaptive refinement (not a colored density plot from a prior solve).
    if (dw) dw->setResultsOverlay(nullptr);
    delete m_overlayRenderer;
    m_overlayRenderer = nullptr;
    delete m_overlayDoc;
    m_overlayDoc = nullptr;

    int initialNodes = (int)doc->meshNodes.size();
    int initialElements = (int)doc->meshElements.size();
    fprintf(stderr, "[ADAPTIVE-UI] Starting adaptive refinement. "
            "Current mesh: %d nodes, %d elements, hasMesh=%d\n",
            initialNodes, initialElements, doc->hasMesh ? 1 : 0);
    fprintf(stderr, "[ADAPTIVE-UI] Config: tolerance=%g, maxIterations=%d\n",
            config.tolerance, config.maxIterations);

    // Create refiner (no parent — runs on worker thread)
    auto *refiner = new AdaptiveRefiner();

    // Worker thread
    auto *thread = new AdaptiveRefineThread();
    thread->refiner = refiner;
    thread->doc = doc;
    thread->config = config;

    // Progress dialog
    QProgressDialog progressDlg(tr("Running adaptive refinement..."),
                                tr("Cancel"), 0, config.maxIterations, this);
    progressDlg.setWindowModality(Qt::WindowModal);
    progressDlg.setMinimumDuration(0);
    progressDlg.setAutoClose(false);
    progressDlg.setAutoReset(false);
    progressDlg.show();

    // Event loop to wait for thread while keeping UI responsive
    QEventLoop loop;
    connect(thread, &QThread::finished, &loop, &QEventLoop::quit);

    // Cancel button → request cancellation
    connect(&progressDlg, &QProgressDialog::canceled,
            refiner, &AdaptiveRefiner::cancel);

    // Progress updates from worker thread → main thread
    connect(refiner, &AdaptiveRefiner::iterationComplete,
            this, [&progressDlg](const AdaptiveIterResult &r) {
                progressDlg.setValue(r.iteration + 1);
                progressDlg.setLabelText(
                    QString("Iteration %1: %2 elements, error %3")
                        .arg(r.iteration + 1)
                        .arg(r.numElements)
                        .arg(r.globalRelError, 0, 'e', 2));
            }, Qt::QueuedConnection);

    connect(refiner, &AdaptiveRefiner::progress,
            this, &MainWindow::updateStatus, Qt::QueuedConnection);

    // Also show solver-level progress in the progress dialog label
    // so the user can see PCG/nonlinear iteration status during long solves.
    connect(refiner, &AdaptiveRefiner::progress,
            this, [&progressDlg](const QString &msg) {
                progressDlg.setLabelText(msg);
            }, Qt::QueuedConnection);

    // Mesh updated → repaint drawing widget synchronously.
    // BlockingQueuedConnection: the worker thread pauses while the main
    // thread repaints, guaranteeing the paint actually happens and that
    // the mesh data isn't modified during the repaint.
    connect(refiner, &AdaptiveRefiner::meshUpdated,
            this, [dw, doc]() {
                fprintf(stderr, "[ADAPTIVE-UI] meshUpdated slot fired on main thread. "
                        "doc->hasMesh=%d, nodes=%d, elements=%d, overlay=%p\n",
                        doc->hasMesh ? 1 : 0,
                        (int)doc->meshNodes.size(),
                        (int)doc->meshElements.size(),
                        (void*)dw->resultsOverlay());
                if (dw) {
                    dw->repaint();
                    // Process any pending events so the repaint
                    // is flushed to screen even under a modal dialog.
                    QApplication::processEvents();
                    fprintf(stderr, "[ADAPTIVE-UI] repaint + processEvents done\n");
                }
            }, Qt::BlockingQueuedConnection);

    // Run refinement on worker thread
    thread->start();
    loop.exec();
    thread->wait();   // Ensure thread has fully stopped before cleanup

    // Thread finished — collect results
    progressDlg.close();

    // Delete stale .ans file left by the solver so it won't be auto-loaded
    // as a results overlay the next time the file is opened.
    {
        QString ansPath = doc->filePath();
        if (ansPath.endsWith(".fem", Qt::CaseInsensitive))
            ansPath = ansPath.left(ansPath.length() - 4) + ".ans";
        else
            ansPath += ".ans";
        QFile::remove(ansPath);
    }

    // Fully clean up the results overlay objects (not just the widget pointer).
    if (dw) dw->setResultsOverlay(nullptr);
    delete m_overlayRenderer;
    m_overlayRenderer = nullptr;
    delete m_overlayDoc;
    m_overlayDoc = nullptr;

    fprintf(stderr, "[ADAPTIVE-UI] Thread finished. success=%d, doc->hasMesh=%d, "
            "nodes=%d, elements=%d\n",
            thread->success ? 1 : 0, doc->hasMesh ? 1 : 0,
            (int)doc->meshNodes.size(), (int)doc->meshElements.size());

    if (thread->success) {
        doc->hasMesh = true;

        // Apply the adaptive area targets to block labels so that subsequent
        // mesh generation (motion sweep steps, Analyze, etc.) uses the
        // adapted density rather than reverting to the user's original coarse
        // maxArea values.  Re-running adaptive with a different tolerance
        // still works: the algorithm captures current maxArea at the start
        // of each run and uses it as the coarsening baseline.
        refiner->applyTargetsToDocument(doc);

        // Force immediate repaint so the final mesh is visible
        if (dw) {
            dw->repaint();
            QApplication::processEvents();
        }

        const auto &hist = refiner->history();
        if (!hist.empty()) {
            const auto &last = hist.back();
            QString msg = QString("Adaptive refinement: %1 iterations, "
                                 "%2 nodes, %3 elements, error %4")
                .arg(hist.size())
                .arg(last.numNodes)
                .arg(last.numElements)
                .arg(last.globalRelError, 0, 'e', 2);
            updateStatus(msg);
            fprintf(stderr, "[ADAPTIVE-UI] %s\n", msg.toUtf8().constData());
        }
    } else {
        if (dw) {
            dw->repaint();
            QApplication::processEvents();
        }
        QMessageBox::warning(this, tr("Adaptive Refinement Error"),
            refiner->lastError());
    }

    thread->deleteLater();
    delete refiner;
}

// ---------------------------------------------------------------
// Add Boundary Region — creates a surrounding air region with A=0 BC
// ---------------------------------------------------------------

void MainWindow::onAddBoundaryRegion()
{
    FemmeDocument *doc = currentDocument();
    DrawingWidget *dw = currentDrawing();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }
    if (doc->nodes.empty()) {
        QMessageBox::warning(this, tr("Add Boundary Region"),
            tr("The document has no geometry. Draw or import a model first."));
        return;
    }

    // Show config dialog
    BoundaryRegionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    BoundaryRegionConfig cfg = dlg.config();

    // Get bounding box of existing geometry
    double xmin, ymin, xmax, ymax;
    if (!doc->getBoundingBox(xmin, ymin, xmax, ymax)) {
        QMessageBox::warning(this, tr("Add Boundary Region"),
            tr("Could not compute model bounding box."));
        return;
    }

    double cx = (xmin + xmax) / 2.0;
    double cy = (ymin + ymax) / 2.0;
    double halfW = (xmax - xmin) / 2.0;
    double halfH = (ymax - ymin) / 2.0;
    double d = cfg.distance;

    // --- Ensure "Air" material exists ---
    if (doc->findMaterialPropIndex("Air") < 0) {
        FMaterialProp air;
        air.blockName = "Air";
        air.mu_x = 1.0;
        air.mu_y = 1.0;
        doc->materialProps.push_back(air);
    }

    // --- Ensure "A=0" boundary condition exists ---
    int bcIdx = -1;
    for (int i = 0; i < (int)doc->boundaryProps.size(); i++) {
        if (doc->boundaryProps[i].bdryName == "A=0") {
            bcIdx = i;
            break;
        }
    }
    if (bcIdx < 0) {
        FBoundaryProp bc;
        bc.bdryName = "A=0";
        bc.bdryFormat = 0;  // Prescribed A (all zero by default)
        doc->boundaryProps.push_back(bc);
    }

    if (cfg.shape == BoundaryRegionConfig::Circle) {
        // Circle: 2 nodes + 2 x 180-degree arcs
        double radius = std::max(halfW, halfH) + d;

        FNode n0;
        n0.x = cx - radius;
        n0.y = cy;
        FNode n1;
        n1.x = cx + radius;
        n1.y = cy;

        int idx0 = (int)doc->nodes.size();
        doc->nodes.push_back(n0);
        int idx1 = (int)doc->nodes.size();
        doc->nodes.push_back(n1);

        // Arc from n0 to n1 (bottom half, 180 degrees CCW)
        FArcSegment arc0;
        arc0.n0 = idx0;
        arc0.n1 = idx1;
        arc0.arcLength = 180.0;
        arc0.maxSideLength = 10.0;
        arc0.boundaryMarker = "A=0";
        doc->arcSegments.push_back(arc0);

        // Arc from n1 to n0 (top half, 180 degrees CCW)
        FArcSegment arc1;
        arc1.n0 = idx1;
        arc1.n1 = idx0;
        arc1.arcLength = 180.0;
        arc1.maxSideLength = 10.0;
        arc1.boundaryMarker = "A=0";
        doc->arcSegments.push_back(arc1);

        // Block label in the air gap (between model top and circle top)
        FBlockLabel lbl;
        lbl.x = cx;
        lbl.y = cy + (std::max(halfW, halfH) + radius) / 2.0;
        lbl.blockType = "Air";
        lbl.isExternal = true;
        lbl.maxArea = cfg.meshSize;  // 0 = no constraint
        doc->blockLabels.push_back(lbl);

    } else {
        // Rectangle: 4 nodes + 4 segments
        FNode c0, c1, c2, c3;
        c0.x = xmin - d;  c0.y = ymin - d;
        c1.x = xmax + d;  c1.y = ymin - d;
        c2.x = xmax + d;  c2.y = ymax + d;
        c3.x = xmin - d;  c3.y = ymax + d;

        int i0 = (int)doc->nodes.size();
        doc->nodes.push_back(c0);
        int i1 = (int)doc->nodes.size();
        doc->nodes.push_back(c1);
        int i2 = (int)doc->nodes.size();
        doc->nodes.push_back(c2);
        int i3 = (int)doc->nodes.size();
        doc->nodes.push_back(c3);

        auto addSeg = [&](int na, int nb) {
            FSegment seg;
            seg.n0 = na;
            seg.n1 = nb;
            seg.boundaryMarker = "A=0";
            doc->segments.push_back(seg);
        };
        addSeg(i0, i1);
        addSeg(i1, i2);
        addSeg(i2, i3);
        addSeg(i3, i0);

        // Block label in the corner gap
        FBlockLabel lbl;
        lbl.x = xmin - d / 2.0;
        lbl.y = ymin - d / 2.0;
        lbl.blockType = "Air";
        lbl.isExternal = true;
        lbl.maxArea = cfg.meshSize;
        doc->blockLabels.push_back(lbl);
    }

    // Invalidate mesh — geometry changed, old mesh doesn't include new region
    doc->hasMesh = false;
    doc->meshNodes.clear();
    doc->meshElements.clear();
    doc->meshEdges.clear();

    // Clear stale results overlay (it was computed on the old mesh)
    if (dw) dw->setResultsOverlay(nullptr);
    delete m_overlayRenderer;
    m_overlayRenderer = nullptr;
    delete m_overlayDoc;
    m_overlayDoc = nullptr;

    doc->isModified = true;
    if (dw) dw->update();

    updateStatus(tr("Boundary region added (%1, distance %2 mm). Re-mesh to include it.")
        .arg(cfg.shape == BoundaryRegionConfig::Circle ? tr("circle") : tr("rectangle"))
        .arg(cfg.distance));
}

void MainWindow::onAnalyze()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }

    // Must have a mesh first
    if (!doc->hasMesh) {
        updateStatus(tr("Creating mesh first..."));
        onCreateMesh();
        if (!doc->hasMesh) return;
    }

    if (m_solver->isRunning()) {
        updateStatus(tr("Solver is already running."));
        return;
    }

    // Auto-save .fem file — the solver re-reads it
    if (doc->isModified || !doc->filePath().isEmpty()) {
        if (!doc->saveToFile(doc->filePath())) {
            QMessageBox::warning(this, tr("Error"),
                tr("Could not save file before analysis."));
            return;
        }
    }

    // Ensure mesh files (.node, .ele, .pbc) exist on disk.
    // After adaptive refinement the mesh lives only in memory
    // (generateMeshInProcess doesn't write files), so the external
    // fkn solver would fail with "problem loading mesh" (exit code 2).
    fprintf(stderr, "[ANALYZE] Writing mesh files: %d nodes, %d elements, path=%s\n",
            (int)doc->meshNodes.size(), (int)doc->meshElements.size(),
            doc->filePath().toUtf8().constData());
    if (!m_meshGen->writeMeshFiles(doc)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not write mesh files: %1").arg(m_meshGen->lastError()));
        return;
    }
    // Verify mesh files actually exist on disk
    {
        QString base = doc->filePath();
        if (base.endsWith(".fem", Qt::CaseInsensitive)) base.chop(4);
        QStringList needed = { base + ".node", base + ".ele", base + ".pbc" };
        for (const auto &f : needed) {
            QFileInfo fi(f);
            fprintf(stderr, "[ANALYZE] %s: exists=%d size=%lld\n",
                    f.toUtf8().constData(), fi.exists(), fi.size());
        }
    }
    fprintf(stderr, "[ANALYZE] Mesh files written successfully\n");

    // Track for auto-overlay after solve
    DrawingWidget *dw = currentDrawing();
    if (dw) {
        m_autoSolvePending = true;
        m_autoSolveTarget = dw;
    }

    m_solver->runSolver(doc);
}

void MainWindow::onSolverFinished(bool success)
{
    // If a motion sweep is running, it handles solver results via its own
    // connection — don't interfere here.
    if (m_motionRunner && m_motionRunner->isRunning())
        return;

    if (success) {
        if (m_autoSolvePending && m_autoSolveTarget) {
            m_autoSolvePending = false;
            DrawingWidget *target = m_autoSolveTarget;
            m_autoSolveTarget = nullptr;
            updateStatus(tr("Analysis complete. Loading results overlay..."));
            loadResultsOverlay(target);
        } else {
            updateStatus(tr("Analysis complete. Use View Results to see output."));
        }
    } else {
        m_autoSolvePending = false;
        m_autoSolveTarget = nullptr;
        QMessageBox::warning(this, tr("Solver Error"),
            m_solver->lastError());
    }
}

void MainWindow::onViewResults()
{
    FemmeDocument *doc = currentDocument();
    if (!doc) {
        updateStatus(tr("No document open."));
        return;
    }

    if (doc->filePath().isEmpty()) {
        updateStatus(tr("Please save the document first."));
        return;
    }

    // Derive .ans path from .fem path
    QString ansPath = doc->filePath();
    if (ansPath.endsWith(".fem", Qt::CaseInsensitive))
        ansPath = ansPath.left(ansPath.length() - 4) + ".ans";
    else
        ansPath += ".ans";

    if (!QFile::exists(ansPath)) {
        QMessageBox::warning(this, tr("No Results"),
            tr("Solution file not found:\n%1\n\nPlease run Analysis first.").arg(ansPath));
        return;
    }

    auto *rdoc = new ResultsDocument();
    if (!rdoc->loadFromFile(ansPath)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not load solution file:\n%1").arg(ansPath));
        delete rdoc;
        return;
    }

    auto *results = new ResultsWidget(rdoc);
    connect(results, &ResultsWidget::coordinatesChanged,
            this, &MainWindow::updateCoordinates);
    connect(results, &ResultsWidget::statusMessage,
            this, &MainWindow::updateStatus);

    QMdiSubWindow *sub = mdiArea->addSubWindow(results);
    sub->setWindowTitle(tr("Results: %1").arg(ansPath));
    sub->show();

    // Defer zoomFit until the tabbed layout has assigned the final size.
    QTimer::singleShot(0, results, &ResultsWidget::zoomFit);
    updateStatus(QString("Results loaded: %1 nodes, %2 elements, B_max=%3 T")
        .arg(rdoc->nodes.size()).arg(rdoc->elements.size())
        .arg(rdoc->B_High, 0, 'e', 4));
}

// ---------------------------------------------------------------
// Results overlay loading
// ---------------------------------------------------------------

void MainWindow::loadResultsOverlay(DrawingWidget *dw)
{
    if (!dw) return;
    FemmeDocument *doc = dw->document();
    if (!doc || doc->filePath().isEmpty()) return;

    // Derive .ans path
    QString ansPath = doc->filePath();
    if (ansPath.endsWith(".fem", Qt::CaseInsensitive))
        ansPath = ansPath.left(ansPath.length() - 4) + ".ans";
    else
        ansPath += ".ans";

    if (!QFile::exists(ansPath)) {
        updateStatus(tr("No .ans file found for overlay."));
        return;
    }

    // Clean up previous overlay
    delete m_overlayRenderer;
    m_overlayRenderer = nullptr;
    delete m_overlayDoc;
    m_overlayDoc = nullptr;

    m_overlayDoc = new ResultsDocument(this);
    if (!m_overlayDoc->loadFromFile(ansPath)) {
        updateStatus(tr("Failed to load results for overlay."));
        delete m_overlayDoc;
        m_overlayDoc = nullptr;
        return;
    }

    m_overlayRenderer = new ResultsOverlayRenderer();
    m_overlayRenderer->setDocument(m_overlayDoc);
    m_overlayRenderer->setShowDensity(m_savedOverlayDensity ? DensityType::B_mag : DensityType::None);
    m_overlayRenderer->setShowContours(m_savedOverlayContours);
    m_overlayRenderer->setShowLegend(m_savedOverlayLegend);
    m_overlayRenderer->setShowMesh(m_savedOverlayMesh);

    // Apply saved AA quality
    QAction *checkedAA = m_aaGroup->checkedAction();
    if (checkedAA)
        m_overlayRenderer->setAAQuality(static_cast<AAQuality>(checkedAA->data().toInt()));

    dw->setResultsOverlay(m_overlayRenderer);

    // Enable toggle actions and restore saved states (block signals to avoid
    // re-triggering slots while we set the checked states)
    actOverlayDensity->blockSignals(true);
    actOverlayDensity->setEnabled(true);
    actOverlayDensity->setChecked(m_savedOverlayDensity);
    actOverlayDensity->blockSignals(false);

    actOverlayContours->blockSignals(true);
    actOverlayContours->setEnabled(true);
    actOverlayContours->setChecked(m_savedOverlayContours);
    actOverlayContours->blockSignals(false);

    actOverlayMesh->blockSignals(true);
    actOverlayMesh->setEnabled(true);
    actOverlayMesh->setChecked(m_savedOverlayMesh);
    actOverlayMesh->blockSignals(false);

    actOverlayLegend->blockSignals(true);
    actOverlayLegend->setEnabled(true);
    actOverlayLegend->setChecked(m_savedOverlayLegend);
    actOverlayLegend->blockSignals(false);

    updateStatus(QString("Results overlay loaded: B_max=%1 T")
        .arg(m_overlayDoc->B_High, 0, 'e', 4));
}

// ---------------------------------------------------------------
// Overlay toggle slots
// ---------------------------------------------------------------

void MainWindow::onToggleOverlayDensity(bool checked)
{
    m_savedOverlayDensity = checked;
    QSettings().setValue("view/overlayDensity", checked);
    if (!m_overlayRenderer) return;
    m_overlayRenderer->setShowDensity(checked ? DensityType::B_mag : DensityType::None);
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->refreshDisplay();
}

void MainWindow::onToggleOverlayContours(bool checked)
{
    m_savedOverlayContours = checked;
    QSettings().setValue("view/overlayContours", checked);
    if (!m_overlayRenderer) return;
    m_overlayRenderer->setShowContours(checked);
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->refreshDisplay();
}

void MainWindow::onToggleOverlayMesh(bool checked)
{
    m_savedOverlayMesh = checked;
    QSettings().setValue("view/overlayMesh", checked);
    if (!m_overlayRenderer) return;
    m_overlayRenderer->setShowMesh(checked);
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->refreshDisplay();
}

void MainWindow::onToggleOverlayLegend(bool checked)
{
    m_savedOverlayLegend = checked;
    QSettings().setValue("view/overlayLegend", checked);
    if (!m_overlayRenderer) return;
    m_overlayRenderer->setShowLegend(checked);
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->refreshDisplay();
}

// ---------------------------------------------------------------
// Geometry visibility toggle slots
// ---------------------------------------------------------------

void MainWindow::onToggleShowNodes(bool checked)
{
    QSettings().setValue("view/showNodes", checked);
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->setShowNodes(checked);
}

void MainWindow::onToggleShowBlockLabels(bool checked)
{
    QSettings().setValue("view/showBlockLabels", checked);
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->setShowBlockLabels(checked);
}

void MainWindow::onToggleShowGeometry(bool checked)
{
    QSettings().setValue("view/showGeometry", checked);
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->setShowGeometry(checked);
}

// ---------------------------------------------------------------
// Rendering quality
// ---------------------------------------------------------------

void MainWindow::onAAQualityChanged(QAction *action)
{
    int aaVal = action->data().toInt();
    QSettings().setValue("view/aaQuality", aaVal);
    AAQuality q = static_cast<AAQuality>(aaVal);

    if (m_overlayRenderer)
        m_overlayRenderer->setAAQuality(q);

    DrawingWidget *dw = currentDrawing();
    if (dw) dw->refreshDisplay();
}

// ---------------------------------------------------------------
// Move / Parametric Motion
// ---------------------------------------------------------------

void MainWindow::onParametricMotion()
{
    FemmeDocument *doc = currentDocument();
    DrawingWidget *dw = currentDrawing();
    if (!doc || !dw) {
        updateStatus(tr("No document open."));
        return;
    }

    if (doc->filePath().isEmpty()) {
        updateStatus(tr("Please save the document before running motion sweep."));
        onSaveAs();
        if (doc->filePath().isEmpty()) return;
    }

    if (m_solver->isRunning()) {
        QMessageBox::warning(this, tr("Busy"),
            tr("The solver is currently running. Please wait for it to finish."));
        return;
    }

    if (m_motionRunner && m_motionRunner->isRunning()) {
        QMessageBox::warning(this, tr("Busy"),
            tr("A motion sweep is already running."));
        return;
    }

    // Gather circuit names from the current document
    QStringList circuitNames;
    for (const auto &cp : doc->circuitProps)
        circuitNames << cp.circName;

    // Build optimisation callback that runs in the context of MainWindow
    OptimizeCallback optimizeCallback =
        [this, doc, dw](double rmsCurrent, double initialAngle, double angleStep,
                    int polePairs, const QString &phaseA, const QString &phaseB,
                    const QString &phaseC, double &outOptimalAngle,
                    QString &outError) -> bool
    {
        // Freeze the window hierarchy to prevent resize/drift during solves
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        QSize bkMinDw, bkMaxDw, bkMinSub, bkMaxSub, bkMinTop, bkMaxTop;
        bkMinDw = dw->minimumSize();
        bkMaxDw = dw->maximumSize();
        dw->setFixedSize(dw->size());

        QWidget *subWin = dw->parentWidget();
        if (subWin) {
            bkMinSub = subWin->minimumSize();
            bkMaxSub = subWin->maximumSize();
            subWin->setFixedSize(subWin->size());
        }
        QWidget *topWin = dw->window();
        if (topWin) {
            bkMinTop = topWin->minimumSize();
            bkMaxTop = topWin->maximumSize();
            topWin->setFixedSize(topWin->size());
        }
        dw->setViewLocked(true);

        QProgressDialog progress(tr("Optimising phase angle..."),
                                 tr("Cancel"), 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.show();

        bool cancelled = false;
        auto progressCb = [&progress, &cancelled](int step, double angle, double torque) -> bool {
            progress.setLabelText(
                QString("Step %1: angle = %2 deg, torque = %3 Nm")
                    .arg(step).arg(angle, 0, 'f', 2).arg(torque, 0, 'e', 4));
            QApplication::processEvents();
            if (progress.wasCanceled()) {
                cancelled = true;
                return false;
            }
            return true;
        };

        bool ok = MotorOptimizer::optimize(
            doc, m_meshGen, m_solver,
            rmsCurrent, initialAngle, angleStep, polePairs,
            phaseA, phaseB, phaseC,
            outOptimalAngle, outError, progressCb);

        progress.close();

        // Unfreeze window hierarchy
        dw->setViewLocked(false);
        if (topWin) {
            topWin->setMinimumSize(bkMinTop);
            topWin->setMaximumSize(bkMaxTop);
        }
        if (subWin) {
            subWin->setMinimumSize(bkMinSub);
            subWin->setMaximumSize(bkMaxSub);
        }
        dw->setMinimumSize(bkMinDw);
        dw->setMaximumSize(bkMaxDw);

        return ok;
    };

    // Show dialog
    MotionDialog dlg(circuitNames, optimizeCallback, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    MotionConfig config = dlg.config();

    // Save current document state before sweep
    if (!doc->saveToFile(doc->filePath())) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not save file before motion sweep."));
        return;
    }

    // Disconnect auto-solve from the solver (MotionRunner manages its own connection)
    m_autoSolvePending = false;
    m_autoSolveTarget = nullptr;

    // Create runner
    delete m_motionRunner;
    m_motionRunner = new MotionRunner(this);

    connect(m_motionRunner, &MotionRunner::progress,
            this, &MainWindow::onMotionProgress);
    connect(m_motionRunner, &MotionRunner::finished,
            this, &MainWindow::onMotionFinished);

    // Ensure an overlay renderer exists so the sweep can show density/contours.
    // After adaptive refinement the overlay is cleared, so create one now.
    if (!m_overlayRenderer) {
        m_overlayRenderer = new ResultsOverlayRenderer();
        m_overlayRenderer->setShowDensity(m_savedOverlayDensity ? DensityType::B_mag : DensityType::None);
        m_overlayRenderer->setShowContours(m_savedOverlayContours);
        m_overlayRenderer->setShowLegend(m_savedOverlayLegend);
        m_overlayRenderer->setShowMesh(m_savedOverlayMesh);
        QAction *checkedAA = m_aaGroup->checkedAction();
        if (checkedAA)
            m_overlayRenderer->setAAQuality(static_cast<AAQuality>(checkedAA->data().toInt()));
        dw->setResultsOverlay(m_overlayRenderer);
    }

    updateStatus(tr("Starting parametric motion sweep..."));

    m_motionRunner->start(config, doc, dw, m_meshGen, m_solver, m_overlayRenderer);
}

void MainWindow::onMotionProgress(const QString &msg)
{
    updateStatus(msg);
}

void MainWindow::onMotionFinished(bool success, const QString &csvPath, const QString &animPath)
{
    if (!success) {
        updateStatus(tr("Motion sweep failed or aborted."));
        // Still refresh the drawing (geometry was restored)
        DrawingWidget *dw = currentDrawing();
        if (dw) dw->update();
        return;
    }

    // Clear the stale overlay from the sweep (its mesh positions are from
    // the moved geometry and no longer match the restored geometry).
    DrawingWidget *dw = currentDrawing();
    if (dw) {
        dw->setResultsOverlay(nullptr);
        dw->update();
    }
    delete m_overlayRenderer;
    m_overlayRenderer = nullptr;
    delete m_overlayDoc;
    m_overlayDoc = nullptr;

    QString msg = tr("Motion sweep complete!\n\nCSV: %1").arg(csvPath);
    if (!animPath.isEmpty())
        msg += tr("\nAnimation: %1").arg(animPath);
    QMessageBox::information(this, tr("Motion Sweep Complete"), msg);

    updateStatus(tr("Motion sweep finished."));
}

// ---------------------------------------------------------------
// View
// ---------------------------------------------------------------

void MainWindow::onZoomIn()
{
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->zoomIn();
}

void MainWindow::onZoomOut()
{
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->zoomOut();
}

void MainWindow::onZoomFit()
{
    DrawingWidget *dw = currentDrawing();
    if (dw) dw->zoomFit();
}

// ---------------------------------------------------------------
// Status bar updates
// ---------------------------------------------------------------

void MainWindow::updateCoordinates(double x, double y)
{
    coordLabel->setText(QString("(%1, %2)")
        .arg(x, 0, 'f', 4).arg(y, 0, 'f', 4));
}

void MainWindow::updateStatus(const QString &msg)
{
    // Show only the last line in the status bar — solver output can contain
    // multiple lines which would otherwise expand the status bar.
    QString line = msg;
    int nl = line.lastIndexOf('\n');
    if (nl >= 0)
        line = line.mid(nl + 1);
    statusLabel->setText(line.trimmed());

    // Append full message to the scrollable log panel
    appendLog(msg);
}
