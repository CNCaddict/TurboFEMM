// FEMM Qt 6 GUI — H-Adaptive Mesh Refinement implementation
#include "adaptiverefine.h"
#include "inprocesssolver.h"
#include "resultsdoc.h"
#include "document.h"
#include "meshgen.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AdaptiveRefiner::AdaptiveRefiner(QObject *parent)
    : QObject(parent)
{
}

AdaptiveRefiner::~AdaptiveRefiner() = default;

void AdaptiveRefiner::cancel()
{
    m_cancelled = true;
}

// ---------------------------------------------------------------
// Compute area of triangle element from node coordinates
// ---------------------------------------------------------------
double AdaptiveRefiner::elementArea(const ResultsDocument *results, int i) const
{
    const auto &elm = results->elements[i];
    double x0 = results->nodes[elm.p[0]].x;
    double y0 = results->nodes[elm.p[0]].y;
    double x1 = results->nodes[elm.p[1]].x;
    double y1 = results->nodes[elm.p[1]].y;
    double x2 = results->nodes[elm.p[2]].x;
    double y2 = results->nodes[elm.p[2]].y;
    return 0.5 * std::fabs((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0));
}

// ---------------------------------------------------------------
// Zienkiewicz-Zhu error estimator
// Compares element-constant B with smoothed (recovered) B.
// Returns per-element relative error.
// ---------------------------------------------------------------
std::vector<double> AdaptiveRefiner::computeElementErrors(
    const ResultsDocument *results,
    int problemType,
    double &globalRelError) const
{
    int nElm = (int)results->elements.size();
    std::vector<double> elemError(nElm, 0.0);
    double totalError = 0.0;
    double totalEnergy = 0.0;

    for (int i = 0; i < nElm; i++) {
        const auto &elm = results->elements[i];

        // Smoothed B at centroid = average of 3 nodal smoothed values
        CmplxF bSmooth_x = (elm.b1[0] + elm.b1[1] + elm.b1[2]) / 3.0;
        CmplxF bSmooth_y = (elm.b2[0] + elm.b2[1] + elm.b2[2]) / 3.0;

        // Error = ||B_element - B_smoothed||^2
        double errSq = std::norm(elm.B1 - bSmooth_x)
                      + std::norm(elm.B2 - bSmooth_y);

        // Element energy = ||B||^2
        double energySq = std::norm(elm.B1) + std::norm(elm.B2);

        // Element area
        double area = elementArea(results, i);

        // Axisymmetric weighting: multiply by 2*pi*r
        double weight = 1.0;
        if (problemType == 1) { // axisymmetric
            double r = (results->nodes[elm.p[0]].x
                      + results->nodes[elm.p[1]].x
                      + results->nodes[elm.p[2]].x) / 3.0;
            if (r > 1e-10)
                weight = 2.0 * M_PI * r;
        }

        elemError[i] = errSq * area * weight;
        totalError += elemError[i];
        totalEnergy += energySq * area * weight;
    }

    // Compute relative errors
    double safeEnergy = std::max(totalEnergy, 1e-30);
    globalRelError = std::sqrt(totalError / safeEnergy);

    std::vector<double> relError(nElm);
    for (int i = 0; i < nElm; i++) {
        relError[i] = std::sqrt(elemError[i] / safeEnergy);
    }

    return relError;
}

// ---------------------------------------------------------------
// Mark top fraction of elements for refinement
// ---------------------------------------------------------------
std::vector<bool> AdaptiveRefiner::markElements(
    const std::vector<double> &errors,
    double markingFraction) const
{
    int n = (int)errors.size();
    std::vector<bool> marked(n, false);

    // Sort indices by error (descending)
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return errors[a] > errors[b];
    });

    // Mark the top fraction
    int numToMark = std::max(1, (int)(n * markingFraction));
    for (int i = 0; i < numToMark && i < n; i++) {
        marked[idx[i]] = true;
    }

    return marked;
}

// ---------------------------------------------------------------
// Compute per-region target element areas from error distribution.
// Builds a mapping from block label index → target maxArea.
//
// Key insight: the per-iteration STEP SIZE is decoupled from the
// final TOLERANCE.  Each iteration targets an intermediate error
// level — halving the current global error — rather than jumping
// all the way to the (possibly distant) tolerance.  The tolerance
// only controls when the outer loop stops.
//
// This makes the refinement truly selective: regions whose error
// is already below the STEP target stay unchanged, while only
// high-error regions get refined.  When the global error is 11%
// and tolerance is 0.1%, the step target is 5.5% — most regions
// are already below that, so only the worst 2–3 get refined.
//
// Algorithm (Zienkiewicz–Zhu guided):
//
//   stepTarget = max(globalErr / 2, tolerance)
//
//   For region r:
//     budget_r  = stepTarget × sqrt(N_r / N)
//     θ_r       = budget_r / regionErr_r
//     θ < 1 → over budget → refine     θ ≥ 1 → within budget → keep
//
//   Controls:
//     Damping α = 0.6:    θ_d = θ^α  (60% toward optimum in log-space)
//     Per-region floor:   θ ≥ 1/3    (max 3× refinement per step)
//     Global growth cap:  ≤ 1.5× total elements per step
//     Selectivity:        only refine if θ_final < 0.8
// ---------------------------------------------------------------
std::map<int, double> AdaptiveRefiner::computeRegionTargetAreas(
    const ResultsDocument *results,
    const std::vector<double> &errors,
    double tolerance) const
{
    // Accumulate per-region statistics
    // SolnElement.lbl = 0-based index among non-hole block labels
    struct RegionStats {
        double sumErrSq = 0.0;   // sum of errors[i]^2
        double totalArea = 0.0;
        int count = 0;
    };
    std::map<int, RegionStats> stats;

    int nElm = (int)results->elements.size();
    double totalErrSq = 0.0;
    for (int i = 0; i < nElm; i++) {
        int lbl = results->elements[i].lbl;
        double area = elementArea(results, i);
        stats[lbl].sumErrSq += errors[i] * errors[i];
        stats[lbl].totalArea += area;
        stats[lbl].count++;
        totalErrSq += errors[i] * errors[i];
    }

    // Step target: aim to halve the global error each iteration,
    // but never set the intermediate target below the final tolerance.
    double globalErr = std::sqrt(totalErrSq);
    double stepTarget = std::max(globalErr / 2.0, tolerance);

    fprintf(stderr, "[ADAPTIVE] globalErr=%.5f, stepTarget=%.5f "
            "(halved, floored at tolerance=%.5f)\n",
            globalErr, stepTarget, tolerance);

    // Per-region ZZ-guided area ratios
    const double alpha = 0.6;          // damping exponent
    const double thetaFloor = 1.0 / 3.0;  // max 3× per region per step
    const double maxGrowth = 1.5;      // max 1.5× total elements per step

    struct RegionTarget {
        int lbl;
        double avgArea;
        double theta;         // raw optimal ratio (budget / regionErr)
        double thetaDamped;   // after damping + floor
        int count;
    };
    std::vector<RegionTarget> targets;

    for (auto &[lbl, s] : stats) {
        if (s.count == 0) continue;

        double regionErr = std::sqrt(s.sumErrSq);
        double budget = stepTarget * std::sqrt((double)s.count / (double)nElm);
        double theta = budget / std::max(regionErr, 1e-20);
        double avgArea = s.totalArea / s.count;

        // Damp: move α of the way in log-space toward the target
        double td = std::pow(std::max(theta, 1e-6), alpha);

        // Floor: at most 3× refinement per region per step
        td = std::max(td, thetaFloor);

        targets.push_back({lbl, avgArea, theta, td, s.count});
    }

    // Predict total element count if all refinements are applied.
    double predictedCount = 0;
    for (const auto &t : targets) {
        double effTheta = std::min(t.thetaDamped, 1.0);
        predictedCount += t.count / effTheta;
    }

    // Global growth cap: scale all θ values up if predicted growth
    // exceeds the budget.
    double growthRatio = predictedCount / (double)nElm;
    double scale = 1.0;
    if (growthRatio > maxGrowth) {
        scale = growthRatio / maxGrowth;
    }

    // Build target areas — ONLY for regions that need significant refinement.
    // Regions with θ_final ≥ 0.8 keep their user-set block label maxArea,
    // preserving baseline density in low-error areas.
    std::map<int, double> targetAreas;
    int numRefined = 0;
    for (const auto &t : targets) {
        double finalTheta = std::min(t.thetaDamped * scale, 1.0);

        if (finalTheta < 0.8) {
            targetAreas[t.lbl] = t.avgArea * finalTheta;
            numRefined++;
            fprintf(stderr, "[ADAPTIVE]   Region %d (%d elems): REFINE — "
                    "θ=%.3f → damped=%.3f → final=%.3f, "
                    "area %g → %g\n",
                    t.lbl, t.count, t.theta, t.thetaDamped, finalTheta,
                    t.avgArea, t.avgArea * finalTheta);
        } else {
            fprintf(stderr, "[ADAPTIVE]   Region %d (%d elems): KEEP   — "
                    "θ=%.3f (within step budget)\n",
                    t.lbl, t.count, t.theta);
        }
    }

    fprintf(stderr, "[ADAPTIVE] Step: growth=%.2fx (cap %.1fx), "
            "%d of %d regions refined\n",
            growthRatio, maxGrowth, numRefined, (int)targets.size());

    return targetAreas;
}

// ---------------------------------------------------------------
// Save user's original maxArea per region label (0-based non-hole index).
// ---------------------------------------------------------------
std::map<int, double> AdaptiveRefiner::captureOriginalAreas(FemmeDocument *doc) const
{
    std::map<int, double> areas;
    int lbl = 0;
    for (int bi = 0; bi < (int)doc->blockLabels.size(); bi++) {
        if (doc->blockLabels[bi].blockType == "<No Mesh>" || doc->blockLabels[bi].isExternal) continue;
        double a = doc->blockLabels[bi].maxArea;
        // If user set maxArea=0 (no constraint), estimate from geometry
        if (a <= 0) {
            double xmin, ymin, xmax, ymax;
            if (doc->getBoundingBox(xmin, ymin, xmax, ymax)) {
                double totalArea = (xmax - xmin) * (ymax - ymin);
                a = totalArea / 50.0;
            } else {
                a = 1.0;
            }
        }
        areas[lbl] = a;
        lbl++;
    }
    return areas;
}

// ---------------------------------------------------------------
// Generate mesh with given per-region areas, solve, compute error.
// ---------------------------------------------------------------
AdaptiveRefiner::SolveResult AdaptiveRefiner::solveWithAreas(
    FemmeDocument *doc,
    MeshGenerator &meshGen,
    const std::map<int, double> &regionAreas,
    std::vector<MeshEdge> &edges)
{
    SolveResult result;

    // Build lbl -> blockLabels index mapping
    std::vector<int> lblToBlockIdx;
    for (int bi = 0; bi < (int)doc->blockLabels.size(); bi++) {
        if (doc->blockLabels[bi].blockType == "<No Mesh>" || doc->blockLabels[bi].isExternal) continue;
        lblToBlockIdx.push_back(bi);
    }

    // Save and override maxArea
    std::vector<double> origMaxArea(doc->blockLabels.size());
    for (int bi = 0; bi < (int)doc->blockLabels.size(); bi++)
        origMaxArea[bi] = doc->blockLabels[bi].maxArea;

    for (const auto &[lbl, area] : regionAreas) {
        if (lbl >= 0 && lbl < (int)lblToBlockIdx.size()) {
            int bi = lblToBlockIdx[lbl];
            doc->blockLabels[bi].maxArea = area;
        }
    }

    // Generate mesh
    std::vector<MeshEdge> newEdges;
    bool meshOk = meshGen.generateMeshInProcess(doc, newEdges);

    // Restore originals immediately
    for (int bi = 0; bi < (int)doc->blockLabels.size(); bi++)
        doc->blockLabels[bi].maxArea = origMaxArea[bi];

    if (!meshOk) {
        m_lastError = "Mesh generation failed: " + meshGen.lastError();
        return result;
    }

    edges = newEdges;

    // Solve
    auto results = std::make_unique<ResultsDocument>();
    if (!m_solver->solveExistingMesh(doc, edges, results.get())) {
        m_lastError = "Solve failed: " + m_solver->lastError();
        return result;
    }

    // Check for NaN
    for (const auto &nd : results->nodes) {
        if (std::isnan(nd.A.real()) || std::isnan(nd.A.imag())) {
            m_lastError = "Solver produced NaN.";
            return result;
        }
    }

    // Compute error
    results->ensureSmoothedB();
    double globalRelErr;
    computeElementErrors(results.get(), results->problemType, globalRelErr);

    if (std::isnan(globalRelErr)) {
        m_lastError = "Error estimator produced NaN.";
        return result;
    }

    result.globalRelError = globalRelErr;
    result.numElements = (int)results->elements.size();
    result.numNodes = (int)results->nodes.size();
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------
// Phase 2: Find minimum density mesh that meets tolerance.
// Tolerance is treated as a TARGET — the algorithm actively coarsens
// toward it, finding the coarsest mesh that still meets the requirement.
//
// Two sub-phases:
//   2a. Probe outward: expand coarse bound until error exceeds tolerance
//   2b. Bisect: narrow the bracket to find the coarsest acceptable mesh
// ---------------------------------------------------------------
bool AdaptiveRefiner::runCoarseningPhase(
    FemmeDocument *doc,
    MeshGenerator &meshGen,
    const std::map<int, double> &fineBounds,
    const std::map<int, double> &coarseBounds,
    const AdaptiveConfig &config,
    std::vector<MeshEdge> &edges)
{
    emit progress("Phase 2: Finding minimum density mesh...");

    if (fineBounds.empty()) {
        emit progress("No regions to coarsen. Phase 2 skipped.");
        return true;
    }

    // lo = known-good areas (meets tolerance), hi = candidate coarse areas.
    // Larger area values = fewer elements = coarser mesh.
    std::map<int, double> lo = fineBounds;
    std::map<int, double> hi;

    // Initialize hi: start at user's original areas, but ensure hi >= lo * 4
    // so there's meaningful room to search.
    for (const auto &[lbl, loArea] : lo) {
        double coarseArea = loArea;
        auto it = coarseBounds.find(lbl);
        if (it != coarseBounds.end() && it->second > loArea) {
            coarseArea = it->second;
        }
        hi[lbl] = std::max(coarseArea, loArea * 4.0);
    }

    fprintf(stderr, "[COARSEN] Starting Phase 2: %d regions\n", (int)lo.size());
    for (const auto &[lbl, loArea] : lo) {
        fprintf(stderr, "[COARSEN]   Region %d: lo=%g, hi=%g\n",
                lbl, loArea, hi[lbl]);
    }

    // Phase 2a: Probe — expand hi until it exceeds tolerance.
    // This handles the case where even the user's original mesh meets
    // tolerance (e.g., initial error 1.2% with 5% tolerance).
    const int maxProbeSteps = 4;  // 4^4 = 256x expansion
    bool bracketFound = false;

    for (int probe = 0; probe < maxProbeSteps; probe++) {
        if (m_cancelled) {
            emit progress("Cancelled by user during coarsening.");
            return true;
        }

        emit progress(QString("Probing coarser mesh (step %1)...").arg(probe + 1));
        auto result = solveWithAreas(doc, meshGen, hi, edges);

        if (!result.ok) {
            // Solve failed — this coarseness is too extreme, use as upper bound
            emit progress("Solve failed at coarse probe — using as upper bound.");
            bracketFound = true;
            break;
        }

        // Record in history
        AdaptiveIterResult iterResult;
        iterResult.iteration = (int)m_history.size();
        iterResult.numElements = result.numElements;
        iterResult.numNodes = result.numNodes;
        iterResult.globalRelError = result.globalRelError;
        iterResult.isCoarseningStep = true;
        m_history.push_back(iterResult);
        emit iterationComplete(iterResult);
        emit meshUpdated();

        fprintf(stderr, "[COARSEN] Probe %d: %d elements, error=%g (tolerance=%g)\n",
                probe + 1, result.numElements, result.globalRelError, config.tolerance);

        if (result.globalRelError >= config.tolerance) {
            bracketFound = true;
            emit progress(QString("Probe %1: error %2 >= tolerance %3 — bracket found")
                          .arg(probe + 1)
                          .arg(result.globalRelError, 0, 'e', 3)
                          .arg(config.tolerance, 0, 'e', 3));
            break;
        }

        // hi also meets tolerance — move lo up to hi and expand further.
        lo = hi;
        emit progress(QString("Probe %1: error %2 < tolerance — trying coarser")
                      .arg(probe + 1)
                      .arg(result.globalRelError, 0, 'e', 3));

        // Expand hi further (4x coarser per step)
        for (auto &[lbl, hiArea] : hi) {
            hiArea *= 4.0;
        }
    }

    if (!bracketFound) {
        // Even very coarse meshes meet tolerance — use the coarsest tested.
        emit progress("All probe meshes met tolerance. Using coarsest tested mesh.");
        m_finalRegionTargets = lo;
        auto finalResult = solveWithAreas(doc, meshGen, lo, edges);
        if (!finalResult.ok) {
            solveWithAreas(doc, meshGen, fineBounds, edges);
        }
        emit meshUpdated();
        fprintf(stderr, "[COARSEN] Phase 2 complete (no bracket): %d elements\n",
                (int)doc->meshElements.size());
        emit progress(QString("Phase 2 complete: %1 elements")
                      .arg((int)doc->meshElements.size()));
        return true;
    }

    // Phase 2b: Bisect within [lo, hi] to find the coarsest mesh
    // that still meets tolerance.
    std::map<int, double> bestAreas = lo;

    for (int step = 0; step < config.maxCoarsenSteps; step++) {
        if (m_cancelled) {
            emit progress("Cancelled by user during coarsening.");
            return true;
        }

        // Compute trial areas (geometric midpoint per region)
        std::map<int, double> trial;
        bool anyChanged = false;

        for (const auto &[lbl, loArea] : lo) {
            double hiArea = hi[lbl];
            if (hiArea < loArea * 1.1) {
                trial[lbl] = loArea;
                continue;
            }
            trial[lbl] = std::sqrt(loArea * hiArea);
            anyChanged = true;
            fprintf(stderr, "[COARSEN] Bisect %d, Region %d: [%g, %g] -> %g\n",
                    step + 1, lbl, loArea, hiArea, trial[lbl]);
        }

        if (!anyChanged) {
            emit progress("All regions converged during bisection.");
            break;
        }

        emit progress(QString("Bisection step %1/%2: testing mesh...")
                      .arg(step + 1).arg(config.maxCoarsenSteps));

        auto result = solveWithAreas(doc, meshGen, trial, edges);

        if (!result.ok) {
            emit progress("Solve failed during bisection. Using last known good mesh.");
            auto revert = solveWithAreas(doc, meshGen, bestAreas, edges);
            m_finalRegionTargets = bestAreas;
            emit meshUpdated();
            return revert.ok;
        }

        // Record in history
        AdaptiveIterResult iterResult;
        iterResult.iteration = (int)m_history.size();
        iterResult.numElements = result.numElements;
        iterResult.numNodes = result.numNodes;
        iterResult.globalRelError = result.globalRelError;
        iterResult.isCoarseningStep = true;
        m_history.push_back(iterResult);
        emit iterationComplete(iterResult);
        emit meshUpdated();

        fprintf(stderr, "[COARSEN] Bisect %d: %d elements, error=%g (tolerance=%g)\n",
                step + 1, result.numElements, result.globalRelError, config.tolerance);

        if (result.globalRelError < config.tolerance) {
            // Good: this mesh meets tolerance. Try even coarser.
            for (auto &[lbl, loArea] : lo) {
                loArea = trial[lbl];
            }
            bestAreas = trial;
            emit progress(QString("Bisection %1: error %2 < tolerance — can try coarser (%3 elements)")
                          .arg(step + 1)
                          .arg(result.globalRelError, 0, 'e', 3)
                          .arg(result.numElements));
        } else {
            // Too coarse: narrow down.
            for (auto &[lbl, hiArea] : hi) {
                hiArea = trial[lbl];
            }
            emit progress(QString("Bisection %1: error %2 >= tolerance — too coarse, narrowing")
                          .arg(step + 1)
                          .arg(result.globalRelError, 0, 'e', 3));
        }
    }

    // Regenerate final mesh at the best known good areas
    emit progress("Regenerating final optimized mesh...");
    auto finalResult = solveWithAreas(doc, meshGen, bestAreas, edges);
    if (!finalResult.ok) {
        finalResult = solveWithAreas(doc, meshGen, fineBounds, edges);
    }
    emit meshUpdated();

    // Record the final mesh state so history().back() reflects the actual result
    if (finalResult.ok) {
        AdaptiveIterResult fr;
        fr.iteration = (int)m_history.size();
        fr.numElements = finalResult.numElements;
        fr.numNodes = finalResult.numNodes;
        fr.globalRelError = finalResult.globalRelError;
        fr.isCoarseningStep = true;
        m_history.push_back(fr);
    }

    m_finalRegionTargets = bestAreas;

    int fineElements = 0;
    for (const auto &h : m_history) {
        if (!h.isCoarseningStep) fineElements = std::max(fineElements, h.numElements);
    }
    fprintf(stderr, "[COARSEN] Phase 2 complete: %d elements (Phase 1 peak: %d)\n",
            (int)doc->meshElements.size(), fineElements);
    emit progress(QString("Phase 2 complete: %1 elements (optimized from %2)")
                  .arg((int)doc->meshElements.size()).arg(fineElements));

    return true;
}

// ---------------------------------------------------------------
// Main refinement loop
// ---------------------------------------------------------------
bool AdaptiveRefiner::run(FemmeDocument *doc, const AdaptiveConfig &config)
{
    if (!doc) {
        m_lastError = "No document";
        m_solver.reset();
        return false;
    }

    m_history.clear();
    m_cancelled = false;
    m_finalRegionTargets.clear();

    // Create persistent solver (GPU + Lua reused across iterations)
    m_solver = std::make_unique<InProcessSolver>(nullptr);
    connect(m_solver.get(), &InProcessSolver::progress,
            this, &AdaptiveRefiner::progress);

    MeshGenerator meshGen;
    connect(&meshGen, &MeshGenerator::progress,
            this, &AdaptiveRefiner::progress);

    // Save user's original maxArea per region (for coarsening bounds)
    m_userOriginalAreas = captureOriginalAreas(doc);

    // Step 1: Generate initial mesh from user's block-label settings.
    // We start from the user's mesh and selectively refine only high-error
    // regions — the tolerance slider controls how much additional density
    // is added beyond the user's baseline.
    emit progress("Generating initial mesh...");
    std::vector<MeshEdge> edges;
    if (!meshGen.generateMeshInProcess(doc, edges)) {
        m_lastError = "Initial mesh generation failed: " + meshGen.lastError();
        m_solver.reset();
        return false;
    }

    doc->hasMesh = true;

    // Extract boundary/constrained segment edges (non-zero markers).
    // These must be preserved through all refinement iterations.
    std::vector<MeshEdge> segmentEdges;
    for (const auto &e : edges) {
        if (e.marker != 0)
            segmentEdges.push_back(e);
    }

    fprintf(stderr, "[ADAPTIVE] Initial mesh: %d nodes, %d elements\n",
            (int)doc->meshNodes.size(), (int)doc->meshElements.size());
    fprintf(stderr, "[ADAPTIVE] Config: tolerance=%g, maxIterations=%d, markingFraction=%g\n",
            config.tolerance, config.maxIterations, config.markingFraction);

    emit progress(QString("Initial mesh: %1 nodes, %2 elements")
                  .arg(doc->meshNodes.size())
                  .arg(doc->meshElements.size()));

    // Notify UI to display the initial mesh
    fprintf(stderr, "[ADAPTIVE] Emitting meshUpdated() for initial mesh...\n");
    emit meshUpdated();
    fprintf(stderr, "[ADAPTIVE] meshUpdated() returned (main thread repainted)\n");

    // Track the area targets that produced the most recently SUCCESSFULLY
    // SOLVED mesh, so we can revert to a known-good state on failure.
    // Iteration 0 solves the user's original mesh → those are the baseline.
    std::map<int, double> lastSolvableTargets = m_userOriginalAreas;
    bool solveFailedRecovery = false;

    // Step 2: Iterative solve-refine loop
    for (int iter = 0; iter < config.maxIterations; iter++) {
        if (m_cancelled) {
            emit progress("Cancelled by user.");
            m_solver.reset();
            return true;
        }

        emit progress(QString("Iteration %1/%2: solving...")
                      .arg(iter + 1).arg(config.maxIterations));

        // Solve current mesh (using existing mesh + edges, skip re-meshing)
        auto results = std::make_unique<ResultsDocument>();
        if (!m_solver->solveExistingMesh(doc, edges, results.get())) {
            if (iter == 0) {
                // Can't solve even the initial (user's) mesh — genuine error.
                m_lastError = "Solve failed at iteration 1: "
                            + m_solver->lastError();
                m_solver.reset();
                return false;
            }

            // Later iteration: the mesh got too dense / ill-conditioned.
            // Revert to the last successfully-solved mesh and stop refining.
            fprintf(stderr, "[ADAPTIVE] Solve failed at iteration %d: %s\n"
                            "[ADAPTIVE] Reverting to last solvable mesh (targets from iter %d)\n",
                    iter + 1, qPrintable(m_solver->lastError()), iter);

            emit progress(QString("Solve failed at iteration %1 (%2). "
                                  "Reverting to last successful mesh.")
                          .arg(iter + 1).arg(m_solver->lastError()));

            // Regenerate the mesh at the targets that last produced a solvable mesh.
            m_finalRegionTargets = lastSolvableTargets;
            auto revert = solveWithAreas(doc, meshGen, lastSolvableTargets, edges);
            if (!revert.ok) {
                // Even the revert failed — fall back to user's original mesh.
                fprintf(stderr, "[ADAPTIVE] Revert also failed! Falling back to original mesh.\n");
                m_finalRegionTargets = m_userOriginalAreas;
                solveWithAreas(doc, meshGen, m_userOriginalAreas, edges);
            }
            emit meshUpdated();
            solveFailedRecovery = true;
            break; // proceed to Phase 2 (which will assess what's possible)
        }

        // Check for NaN in solver output (PCG divergence)
        {
            int nanCount = 0;
            for (const auto &nd : results->nodes) {
                if (std::isnan(nd.A.real()) || std::isnan(nd.A.imag()))
                    nanCount++;
            }
            if (nanCount > 0) {
                if (iter == 0) {
                    m_lastError = QString("Solver produced NaN at %1 of %2 nodes "
                                          "(iteration %3). Try a coarser initial mesh.")
                                  .arg(nanCount).arg(results->nodes.size()).arg(iter + 1);
                    m_solver.reset();
                    return false;
                }

                // Later iteration: mesh too dense, revert to last good.
                fprintf(stderr, "[ADAPTIVE] NaN at iteration %d (%d/%d nodes). "
                                "Reverting to last solvable mesh.\n",
                        iter + 1, nanCount, (int)results->nodes.size());
                emit progress(QString("Solver produced NaN at iteration %1. "
                                      "Reverting to last successful mesh.")
                              .arg(iter + 1));
                m_finalRegionTargets = lastSolvableTargets;
                auto revert = solveWithAreas(doc, meshGen, lastSolvableTargets, edges);
                if (!revert.ok) {
                    m_finalRegionTargets = m_userOriginalAreas;
                    solveWithAreas(doc, meshGen, m_userOriginalAreas, edges);
                }
                emit meshUpdated();
                solveFailedRecovery = true;
                break;
            }
        }

        // This mesh solved successfully. Record the targets that produced it
        // so we can revert if a FUTURE iteration's mesh fails to solve.
        // iter 0: user's original areas. iter N>0: m_finalRegionTargets (set at end of iter N-1).
        if (iter == 0)
            lastSolvableTargets = m_userOriginalAreas;
        else if (!m_finalRegionTargets.empty())
            lastSolvableTargets = m_finalRegionTargets;

        // Compute smoothed B (needed for ZZ error estimator)
        results->ensureSmoothedB();

        // Compute per-element error
        double globalRelErr;
        auto errors = computeElementErrors(results.get(),
                                            results->problemType,
                                            globalRelErr);

        double maxErr = *std::max_element(errors.begin(), errors.end());

        // Guard against NaN in error computation
        if (std::isnan(globalRelErr) || std::isnan(maxErr)) {
            if (iter == 0) {
                m_lastError = QString("Error estimator produced NaN at iteration %1.")
                              .arg(iter + 1);
                m_solver.reset();
                return false;
            }
            fprintf(stderr, "[ADAPTIVE] NaN in error estimator at iteration %d. "
                            "Reverting to last solvable mesh.\n", iter + 1);
            emit progress(QString("Error estimator produced NaN at iteration %1. "
                                  "Reverting to last successful mesh.").arg(iter + 1));
            m_finalRegionTargets = lastSolvableTargets;
            auto revert = solveWithAreas(doc, meshGen, lastSolvableTargets, edges);
            if (!revert.ok) {
                m_finalRegionTargets = m_userOriginalAreas;
                solveWithAreas(doc, meshGen, m_userOriginalAreas, edges);
            }
            emit meshUpdated();
            solveFailedRecovery = true;
            break;
        }

        AdaptiveIterResult iterResult;
        iterResult.iteration = iter;
        iterResult.numElements = (int)results->elements.size();
        iterResult.numNodes = (int)results->nodes.size();
        iterResult.maxRelError = maxErr;
        iterResult.globalRelError = globalRelErr;

        emit progress(QString("Iteration %1: %2 elements, global error %3, max element error %4")
                      .arg(iter + 1)
                      .arg(iterResult.numElements)
                      .arg(globalRelErr, 0, 'e', 3)
                      .arg(maxErr, 0, 'e', 3));

        fprintf(stderr, "[ADAPTIVE] Iter %d: %d elements, globalRelErr=%g, maxElemErr=%g, tolerance=%g\n",
                iter + 1, iterResult.numElements, globalRelErr, maxErr, config.tolerance);

        // Check convergence: global relative error below tolerance
        if (globalRelErr < config.tolerance) {
            iterResult.numMarked = 0;
            m_history.push_back(iterResult);
            emit iterationComplete(iterResult);
            fprintf(stderr, "[ADAPTIVE] CONVERGED at iter %d: error %g < tolerance %g (NO refinement done)\n",
                    iter + 1, globalRelErr, config.tolerance);
            emit progress(QString("Converged at iteration %1: global error %2 < tolerance %3")
                          .arg(iter + 1)
                          .arg(globalRelErr, 0, 'e', 3)
                          .arg(config.tolerance, 0, 'e', 3));
            break; // fall through to Phase 2 coarsening
        }

        // Check element count change stall (works for both growth and shrinkage)
        if (iter > 0) {
            int prevCount = m_history.back().numElements;
            double change = std::fabs((double)(iterResult.numElements - prevCount)) / (double)prevCount;
            double errChange = std::fabs(m_history.back().globalRelError - globalRelErr)
                             / std::max(m_history.back().globalRelError, 1e-15);
            // Stalled if mesh barely changed AND error barely improved
            if (change < 0.02 && errChange < 0.05) {
                iterResult.numMarked = 0;
                m_history.push_back(iterResult);
                emit iterationComplete(iterResult);
                emit progress(QString("Mesh and error converged (mesh change %1%, error change %2%). Stopping.")
                              .arg(change * 100, 0, 'f', 1)
                              .arg(errChange * 100, 0, 'f', 1));
                break; // fall through to Phase 2 coarsening
            }
        }

        // Compute per-region target areas from error distribution
        auto regionTargets = computeRegionTargetAreas(results.get(),
                                                       errors,
                                                       config.tolerance);

        // Mark elements for refinement (still used for counting)
        auto marked = markElements(errors, config.markingFraction);
        int numMarked = (int)std::count(marked.begin(), marked.end(), true);
        iterResult.numMarked = numMarked;
        m_history.push_back(iterResult);
        emit iterationComplete(iterResult);

        if (regionTargets.empty()) {
            emit progress("All regions within step budget. Stopping refinement.");
            break; // fall through to Phase 2 coarsening
        }

        // Store as the latest target areas (only when non-empty)
        m_finalRegionTargets = regionTargets;

        // ---- Regenerate mesh from PSLG with per-region target areas ----
        // We use full PSLG regeneration (not Triangle's -r mode) because PSLG
        // mode preserves the complete geometry: all segments, arcs, holes, and
        // region seeds.  Triangle -r only has the mesh + boundary segments,
        // missing geometry constraints that the solver needs.
        {
            // Build mapping: lbl (0-based non-hole index) → blockLabels array index
            std::vector<int> lblToBlockIdx;
            for (int bi = 0; bi < (int)doc->blockLabels.size(); bi++) {
                if (doc->blockLabels[bi].blockType == "<No Mesh>" || doc->blockLabels[bi].isExternal) continue;
                lblToBlockIdx.push_back(bi);
            }

            // Save original maxArea values
            std::vector<double> origMaxArea(doc->blockLabels.size());
            for (int bi = 0; bi < (int)doc->blockLabels.size(); bi++)
                origMaxArea[bi] = doc->blockLabels[bi].maxArea;

            // Update block label maxArea from region targets
            for (auto &[lbl, targetArea] : regionTargets) {
                if (lbl >= 0 && lbl < (int)lblToBlockIdx.size()) {
                    int bi = lblToBlockIdx[lbl];
                    doc->blockLabels[bi].maxArea = targetArea;
                    fprintf(stderr, "[ADAPTIVE]   Region %d: target area %g (was %g)\n",
                            lbl, targetArea, origMaxArea[bi]);
                }
            }

            emit progress(QString("Regenerating mesh: %1 elements marked, %2 regions updated...")
                          .arg(numMarked).arg((int)regionTargets.size()));

            // Regenerate mesh from full PSLG with updated per-region areas
            std::vector<MeshEdge> newEdges;
            bool meshOk = meshGen.generateMeshInProcess(doc, newEdges);

            // Restore original maxArea values immediately
            for (int bi = 0; bi < (int)doc->blockLabels.size(); bi++)
                doc->blockLabels[bi].maxArea = origMaxArea[bi];

            if (!meshOk) {
                m_lastError = "Mesh regeneration failed at iteration "
                            + QString::number(iter + 1) + ": "
                            + meshGen.lastError();
                m_solver.reset();
                return false;
            }

            edges = newEdges;

            // Update segment edges for next iteration
            segmentEdges.clear();
            for (const auto &e : edges) {
                if (e.marker != 0)
                    segmentEdges.push_back(e);
            }
        }

        fprintf(stderr, "[ADAPTIVE] After refinement: %d nodes, %d elements\n",
                (int)doc->meshNodes.size(), (int)doc->meshElements.size());

        emit progress(QString("Adapted mesh: %1 nodes, %2 elements")
                      .arg(doc->meshNodes.size())
                      .arg(doc->meshElements.size()));

        // Notify UI to display the adapted mesh
        fprintf(stderr, "[ADAPTIVE] Emitting meshUpdated() after iteration %d refinement...\n", iter + 1);
        emit meshUpdated();
        fprintf(stderr, "[ADAPTIVE] meshUpdated() returned after iteration %d\n", iter + 1);

        // Last iteration — mesh is adapted but we don't re-solve
        if (iter == config.maxIterations - 1) {
            emit progress(QString("Reached max iterations (%1). Final error: %2")
                          .arg(config.maxIterations)
                          .arg(globalRelErr, 0, 'e', 3));
            break; // fall through to Phase 2 coarsening
        }
    }

    // Step 3: Phase 2 — Coarsening back-off (tolerance is a TARGET, not just a ceiling)
    if (config.enableCoarsening && !m_cancelled) {
        // Build fine bounds for ALL regions — not just those refined in Phase 1.
        // For refined regions: use the refined area (known to meet tolerance).
        // For non-refined regions: use user's original maxArea (also meets tolerance
        // as part of the overall mesh). This allows Phase 2 to coarsen ANY region.
        std::map<int, double> fineBounds = m_userOriginalAreas;
        for (const auto &[lbl, area] : m_finalRegionTargets) {
            fineBounds[lbl] = area;
        }
        bool ok = runCoarseningPhase(doc, meshGen,
                                      fineBounds,
                                      m_userOriginalAreas,
                                      config, edges);
        if (!ok) {
            m_solver.reset();
            return false;
        }
    }

    m_solver.reset();
    return true;
}

// ---------------------------------------------------------------
// Apply computed target areas to document block labels.
// This makes the adaptive mesh sizes persist when saving .fem.
// ---------------------------------------------------------------
void AdaptiveRefiner::applyTargetsToDocument(FemmeDocument *doc) const
{
    if (!doc || m_finalRegionTargets.empty()) return;

    // Build mapping: lbl (0-based non-hole index) → blockLabels array index
    std::vector<int> lblToBlockIdx;
    for (int bi = 0; bi < (int)doc->blockLabels.size(); bi++) {
        if (doc->blockLabels[bi].blockType == "<No Mesh>" || doc->blockLabels[bi].isExternal) continue;
        lblToBlockIdx.push_back(bi);
    }

    for (const auto &[lbl, targetArea] : m_finalRegionTargets) {
        if (lbl >= 0 && lbl < (int)lblToBlockIdx.size()) {
            int bi = lblToBlockIdx[lbl];
            doc->blockLabels[bi].maxArea = targetArea;
        }
    }

    doc->isModified = true;
}
