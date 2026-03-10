// FEMM Qt 6 GUI — Parametric Motion Runner
// Drives the sweep: transform → mesh → solve → capture → repeat
#ifndef MOTIONRUNNER_H
#define MOTIONRUNNER_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QDateTime>
#include <vector>
#include "femm_types.h"
#include "meshgen.h"
#include "resultsdoc.h"
#include "dialogs/motiondialog.h"
#include "motoroptimizer.h"

class FemmeDocument;
class DrawingWidget;
class MeshGenerator;
class SolverRunner;
class InProcessSolver;
class ResultsOverlayRenderer;
class SolveThread;

class MotionRunner : public QObject
{
    Q_OBJECT

public:
    explicit MotionRunner(QObject *parent = nullptr);
    ~MotionRunner() override;

    void start(const MotionConfig &config,
               FemmeDocument *doc,
               DrawingWidget *dw,
               MeshGenerator *meshGen,
               SolverRunner *solver,
               ResultsOverlayRenderer *overlay);

    void abort();
    bool isRunning() const { return m_running; }

signals:
    void stepCompleted(int step, int total);
    void progress(const QString &msg);
    void finished(bool success, const QString &csvPath, const QString &videoPath);

private slots:
    void onSolverFinished(bool success);
    void onInProcessSolveFinished();

private:
    void runNextStep();
    QPixmap captureFrame();
    void writeCSV();
    void assembleGIF();
    void restoreGeometry();

    bool m_running = false;
    bool m_aborting = false;
    int m_currentStep = 0;

    MotionConfig m_config;
    FemmeDocument *m_doc = nullptr;
    DrawingWidget *m_dw = nullptr;
    MeshGenerator *m_meshGen = nullptr;
    SolverRunner *m_solver = nullptr;
    InProcessSolver *m_inProcessSolver = nullptr;
    ResultsOverlayRenderer *m_overlay = nullptr;
    SolveThread *m_solveThread = nullptr;
    ResultsDocument *m_pendingResults = nullptr;

    // Circuit backup for motor module restore
    std::vector<FCircuit> m_backupCircuits;

    // Geometry backup for restore after sweep
    std::vector<FNode> m_backupNodes;
    std::vector<FSegment> m_backupSegments;
    std::vector<FArcSegment> m_backupArcs;
    std::vector<FBlockLabel> m_backupLabels;

    // Mesh backup — preserves the adaptive-refined mesh across the sweep
    // so it isn't lost when geometry is restored and re-meshed.
    std::vector<FMeshNode> m_backupMeshNodes;
    std::vector<FMeshElement> m_backupMeshElements;
    std::vector<MeshEdge> m_backupMeshEdges;
    bool m_backupHasMesh = false;

    // View state backup for restore after sweep
    double m_backupViewOx = 0.0;
    double m_backupViewOy = 0.0;
    double m_backupViewMag = 100.0;

    // Widget + subwindow + top window size backup (frozen during sweep)
    QSize m_backupMinSize;
    QSize m_backupMaxSize;
    QSize m_backupSubMinSize;
    QSize m_backupSubMaxSize;
    QSize m_backupTopMinSize;
    QSize m_backupTopMaxSize;

    // Captured frames for GIF assembly
    std::vector<QImage> m_frames;

    // Deferred PNG frames (written to disk at end of run, not per-step)
    struct DeferredFrame {
        QPixmap pixmap;
        QString path;
    };
    std::vector<DeferredFrame> m_deferredPNGs;

    // Results accumulated per step
    struct StepResult {
        int step;
        double cumDx, cumDy, cumAngle;  // cumulative displacement/angle
        ResultsSummary summary;
        double instantTorque = 0.0;     // from probe solve (motor mode)
        bool hasTorque = false;
    };
    std::vector<StepResult> m_results;

    // Timestamp for unique output filenames (set once per sweep)
    QString m_timestamp;

    // Solver connection management
    QMetaObject::Connection m_solverConn;
};

#endif // MOTIONRUNNER_H
