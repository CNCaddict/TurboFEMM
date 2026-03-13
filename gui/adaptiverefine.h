// FEMM Qt 6 GUI — H-Adaptive Mesh Refinement
// Iteratively solves and refines the mesh, concentrating elements
// where the Zienkiewicz-Zhu error estimator is high.
#ifndef ADAPTIVEREFINE_H
#define ADAPTIVEREFINE_H

#include <QObject>
#include <vector>
#include <map>
#include <memory>
#include <atomic>
#include <QString>

class FemmeDocument;
class ResultsDocument;
class InProcessSolver;
class MeshGenerator;
struct MeshEdge;

// ---------------------------------------------------------------
// Configuration for adaptive refinement
// ---------------------------------------------------------------
struct AdaptiveConfig {
    double tolerance = 0.03;         // relative error tolerance (3%)
    int maxIterations = 5;           // max refinement passes
    double markingFraction = 0.20;   // fraction of elements to refine (top 20%)

    // Coarsening back-off: after reaching tolerance, try to coarsen
    // the mesh to find the minimum density that still meets tolerance.
    bool enableCoarsening = true;    // enable Phase 2 coarsening
    int maxCoarsenSteps = 5;         // max bisection steps (5 = 32x resolution)
};

// ---------------------------------------------------------------
// Per-iteration results
// ---------------------------------------------------------------
struct AdaptiveIterResult {
    int iteration = 0;
    int numElements = 0;
    int numNodes = 0;
    double maxRelError = 0.0;
    double globalRelError = 0.0;
    int numMarked = 0;
    bool isCoarseningStep = false;   // true for Phase 2 iterations
};

// ---------------------------------------------------------------
// Adaptive mesh refiner
// ---------------------------------------------------------------
class AdaptiveRefiner : public QObject
{
    Q_OBJECT

public:
    explicit AdaptiveRefiner(QObject *parent = nullptr);
    ~AdaptiveRefiner() override;

    // Run the adaptive refinement loop.
    // On success, doc->meshNodes and doc->meshElements contain the refined mesh.
    bool run(FemmeDocument *doc, const AdaptiveConfig &config);

    QString lastError() const { return m_lastError; }
    const std::vector<AdaptiveIterResult> &history() const { return m_history; }

    // Final per-region target areas (lbl index → target maxArea).
    // Valid after run() returns successfully. Use applyTargetsToDocument()
    // to write these back to the block labels so they persist on save.
    const std::map<int, double> &finalRegionTargets() const { return m_finalRegionTargets; }

    // Apply the computed target areas to the document's block labels.
    // Call this after run() to make the adaptive mesh sizes persist.
    void applyTargetsToDocument(FemmeDocument *doc) const;

    // Request cancellation (thread-safe).
    void cancel();

signals:
    void progress(const QString &msg);
    void iterationComplete(const AdaptiveIterResult &result);
    // Emitted after mesh data in the document is updated and safe to read.
    void meshUpdated();

private:
    // Compute per-element relative error using ZZ estimator.
    // Returns per-element relative error; sets globalEnergyNorm.
    std::vector<double> computeElementErrors(
        const ResultsDocument *results,
        int problemType,
        double &globalRelError) const;

    // Mark top fraction of elements for refinement.
    std::vector<bool> markElements(
        const std::vector<double> &errors,
        double markingFraction) const;

    // Compute per-region target element areas from error distribution.
    // Returns map of block-label-index → target maxArea.
    std::map<int, double> computeRegionTargetAreas(
        const ResultsDocument *results,
        const std::vector<double> &errors,
        double tolerance) const;

    // Compute area of element i from ResultsDocument node coordinates.
    double elementArea(const ResultsDocument *results, int elemIdx) const;

    // Save user's original maxArea per region label.
    std::map<int, double> captureOriginalAreas(FemmeDocument *doc) const;

    // Result from a solve-and-check cycle.
    struct SolveResult {
        double globalRelError = 0.0;
        int numElements = 0;
        int numNodes = 0;
        bool ok = false;
    };

    // Generate mesh with given per-region areas, solve, compute error.
    SolveResult solveWithAreas(
        FemmeDocument *doc,
        MeshGenerator &meshGen,
        const std::map<int, double> &regionAreas,
        std::vector<MeshEdge> &edges);

    // Phase 2: coarsening back-off to find minimum density mesh.
    bool runCoarseningPhase(
        FemmeDocument *doc,
        MeshGenerator &meshGen,
        const std::map<int, double> &fineBounds,
        const std::map<int, double> &coarseBounds,
        const AdaptiveConfig &config,
        std::vector<MeshEdge> &edges);

    std::unique_ptr<InProcessSolver> m_solver;
    std::vector<AdaptiveIterResult> m_history;
    std::map<int, double> m_finalRegionTargets;
    std::map<int, double> m_userOriginalAreas;
    QString m_lastError;
    std::atomic<bool> m_cancelled{false};
};

#endif // ADAPTIVEREFINE_H
