// FEMM 4.2 Performance Benchmark
// Measures key bottlenecks: GPU solver ops, post-processing, overlay rendering.
// Run: ./femm-bench
// Results are printed as a table for before/after comparison.

#include "document.h"
#include "meshgen.h"
#include "resultsdoc.h"
#include "resultsoverlay.h"
#include "inprocesssolver.h"

#include <QApplication>
#include <QPainter>
#include <QImage>
#include <QElapsedTimer>
#include <QDebug>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <random>

#ifdef FEMM_USE_METAL
#include "gpu_backend.h"
#endif

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

// ---------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------

struct BenchResult {
    std::string name;
    double ms;          // mean time in milliseconds
    int iterations;
    std::string detail; // extra info (e.g. "150234 elements")
};

static std::vector<BenchResult> g_results;

static double timeIt(int iters, std::function<void()> fn)
{
    // Warm-up
    fn();

    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < iters; i++) fn();
    double elapsed = timer.nsecsElapsed() / 1e6; // ms
    return elapsed / iters;
}

static void record(const char *name, double ms, int iters, const char *detail = "")
{
    g_results.push_back({std::string(name), ms, iters, std::string(detail)});
    printf("  %-42s %10.3f ms  (%d iters)  %s\n", name, ms, iters, detail);
    fflush(stdout);
}

// ---------------------------------------------------------------
// 1) GPU Backend Micro-Benchmarks
// ---------------------------------------------------------------

#ifdef FEMM_USE_METAL

// Build a 5-point Laplacian stencil on an NxN grid.
// Returns CSR upper triangle + diagonal (matching FEMM's format).
struct SyntheticMatrix {
    int n;
    int nnz;  // off-diagonal upper triangle entries
    std::vector<double> diag;
    std::vector<int> row_ptr;
    std::vector<int> col_idx;
    std::vector<double> values;
    std::vector<double> rhs;  // random RHS
};

static SyntheticMatrix buildLaplacian(int gridN)
{
    SyntheticMatrix m;
    m.n = gridN * gridN;
    m.diag.resize(m.n, 4.0);  // diagonal = 4 for interior nodes

    // Count upper-triangle off-diagonals
    std::vector<std::vector<std::pair<int, double>>> rows(m.n);
    for (int iy = 0; iy < gridN; iy++) {
        for (int ix = 0; ix < gridN; ix++) {
            int i = iy * gridN + ix;
            // Right neighbor (col > row)
            if (ix + 1 < gridN) {
                int j = iy * gridN + (ix + 1);
                rows[i].push_back({j, -1.0});
            }
            // Bottom neighbor (col > row)
            if (iy + 1 < gridN) {
                int j = (iy + 1) * gridN + ix;
                rows[i].push_back({j, -1.0});
            }
        }
    }

    // Build CSR
    m.row_ptr.resize(m.n + 1);
    m.row_ptr[0] = 0;
    for (int i = 0; i < m.n; i++)
        m.row_ptr[i + 1] = m.row_ptr[i] + (int)rows[i].size();
    m.nnz = m.row_ptr[m.n];

    m.col_idx.resize(m.nnz);
    m.values.resize(m.nnz);
    for (int i = 0; i < m.n; i++) {
        int k = m.row_ptr[i];
        for (auto &[col, val] : rows[i]) {
            m.col_idx[k] = col;
            m.values[k] = val;
            k++;
        }
    }

    // Random RHS
    m.rhs.resize(m.n);
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (int i = 0; i < m.n; i++) m.rhs[i] = dist(rng);

    return m;
}

static void benchGPU()
{
    printf("\n=== GPU Backend (Metal) ===\n\n");

    // Measure GPU init time (includes shader loading/compilation)
    {
        QElapsedTimer initTimer;
        initTimer.start();
        auto gpu = GPUBackend::create();
        double initMs = initTimer.nsecsElapsed() / 1e6;
        printf("  GPU init (1st):   %10.1f ms  %s\n", initMs,
               gpu ? "(success)" : "(FAILED)");

        // Measure second init (tests Metal system cache)
        initTimer.start();
        auto gpu2 = GPUBackend::create();
        double init2Ms = initTimer.nsecsElapsed() / 1e6;
        printf("  GPU init (2nd):   %10.1f ms  (cached?)\n", init2Ms);
        record("GPU init (1st create)", initMs, 1);
        record("GPU init (2nd create)", init2Ms, 1);
    }

    auto gpu = GPUBackend::create();
    if (!gpu) {
        printf("  Metal GPU backend not available — skipping.\n");
        return;
    }

    // Test with different matrix sizes
    int sizes[] = {32, 64, 128, 256};
    for (int gridN : sizes) {
        auto mat = buildLaplacian(gridN);
        int n = mat.n;
        char detail[128];
        snprintf(detail, sizeof(detail), "n=%d (grid %dx%d, nnz=%d)",
                 n, gridN, gridN, mat.nnz);

        // Allocate vectors
        std::vector<double> V(n, 0.0), P(n, 0.0), R(n, 0.0);
        std::vector<double> U(n, 0.0), Z(n, 0.0), b(n);
        std::copy(mat.rhs.begin(), mat.rhs.end(), b.begin());

        // Initialize P with random data for benchmarking
        std::mt19937 rng(123);
        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        for (int i = 0; i < n; i++) P[i] = dist(rng);

        gpu->uploadMatrixReal(n, mat.nnz,
                              mat.diag.data(), mat.row_ptr.data(),
                              mat.col_idx.data(), mat.values.data());
        gpu->uploadVectors(n, V.data(), P.data(), R.data(),
                           U.data(), Z.data(), b.data());

        // SpMV
        int iters = (n < 5000) ? 200 : 50;
        {
            char name[64]; snprintf(name, sizeof(name), "GPU SpMV (n=%d)", n);
            double ms = timeIt(iters, [&]() { gpu->spmv(P.data(), U.data()); });
            record(name, ms, iters, detail);
        }

        // Dot product
        {
            char name[64]; snprintf(name, sizeof(name), "GPU dot (n=%d)", n);
            double ms = timeIt(iters, [&]() { gpu->dot(P.data(), U.data()); });
            record(name, ms, iters, detail);
        }

        // Fused SpMV + dot
        {
            char name[64]; snprintf(name, sizeof(name), "GPU SpMV+dot (n=%d)", n);
            double ms = timeIt(iters, [&]() { gpu->spmvDot(P.data(), U.data()); });
            record(name, ms, iters, detail);
        }

        // AXPY
        {
            char name[64]; snprintf(name, sizeof(name), "GPU axpy (n=%d)", n);
            double ms = timeIt(iters, [&]() { gpu->axpy(0.5, P.data(), V.data()); });
            record(name, ms, iters, detail);
        }

        // Jacobi preconditioner
        {
            char name[64]; snprintf(name, sizeof(name), "GPU jacobi (n=%d)", n);
            double ms = timeIt(iters, [&]() { gpu->precondJacobi(R.data(), Z.data()); });
            record(name, ms, iters, detail);
        }

        // Fused Jacobi + dot
        {
            char name[64]; snprintf(name, sizeof(name), "GPU jacobi+dot (n=%d)", n);
            double ms = timeIt(iters, [&]() { gpu->precondJacobiDot(R.data(), Z.data()); });
            record(name, ms, iters, detail);
        }

        // Full PCG iteration body (simulates one iteration)
        {
            char name[64]; snprintf(name, sizeof(name), "GPU PCG iter (n=%d)", n);
            double ms = timeIt(iters, [&]() {
                double pAp = gpu->spmvDot(P.data(), U.data());
                double del = 0.001; // dummy
                gpu->axpy(del, P.data(), V.data());
                gpu->axpy(-del, U.data(), R.data());
                double res_new = gpu->precondJacobiDot(R.data(), Z.data());
                (void)res_new;
                (void)pAp;
                gpu->scal(0.5, P.data());
                gpu->axpy(1.0, Z.data(), P.data());
            });
            record(name, ms, iters, detail);
        }
    }
}

#else
static void benchGPU()
{
    printf("\n=== GPU Backend ===\n");
    printf("  Not compiled with FEMM_USE_METAL — skipping GPU benchmarks.\n");
}
#endif

// ---------------------------------------------------------------
// 2) Post-Processing Benchmarks
// ---------------------------------------------------------------

static void benchPostProcessing()
{
    printf("\n=== Post-Processing ===\n\n");

    const char *files[] = {
        TEST_DATA_DIR "/solenoid.ans",
        TEST_DATA_DIR "/lrk.ans",
    };
    const char *names[] = {"solenoid", "lrk"};

    for (int fi = 0; fi < 2; fi++) {
        ResultsDocument doc;
        if (!doc.loadFromFile(QString::fromLatin1(files[fi]))) {
            printf("  %s: could not load — skipping\n", files[fi]);
            continue;
        }

        int nElm = (int)doc.elements.size();
        int nNode = (int)doc.nodes.size();
        char detail[128];
        snprintf(detail, sizeof(detail), "%d elements, %d nodes", nElm, nNode);

        printf("  --- %s (%s) ---\n", names[fi], detail);

        // loadFromFile (includes all post-processing)
        {
            char name[64]; snprintf(name, sizeof(name), "loadFromFile [%s]", names[fi]);
            int iters = (nElm > 50000) ? 3 : 20;
            double ms = timeIt(iters, [&]() {
                ResultsDocument d;
                d.loadFromFile(QString::fromLatin1(files[fi]));
            });
            record(name, ms, iters, detail);
        }

        // computeSummary
        {
            char name[64]; snprintf(name, sizeof(name), "computeSummary [%s]", names[fi]);
            int iters = (nElm > 50000) ? 50 : 500;
            double ms = timeIt(iters, [&]() {
                doc.computeSummary();
            });
            record(name, ms, iters, detail);
        }

        // computeTorque (about origin, group 0 — will compute even if result is 0)
        {
            char name[64]; snprintf(name, sizeof(name), "computeTorque [%s]", names[fi]);
            int iters = (nElm > 50000) ? 50 : 500;
            double ms = timeIt(iters, [&]() {
                doc.computeTorque(0.0, 0.0, 1);
            });
            record(name, ms, iters, detail);
        }
    }
}

// ---------------------------------------------------------------
// 3) Overlay Rendering Benchmarks
// ---------------------------------------------------------------

static void benchRendering()
{
    printf("\n=== Overlay Rendering ===\n\n");

    // Load the larger problem for meaningful rendering benchmarks
    ResultsDocument doc;
    const char *ansFile = TEST_DATA_DIR "/lrk.ans";
    if (!doc.loadFromFile(QString::fromLatin1(ansFile))) {
        // Fall back to solenoid
        ansFile = TEST_DATA_DIR "/solenoid.ans";
        if (!doc.loadFromFile(QString::fromLatin1(ansFile))) {
            printf("  No .ans files available — skipping rendering benchmarks.\n");
            return;
        }
    }

    int nElm = (int)doc.elements.size();
    char detail[128];
    snprintf(detail, sizeof(detail), "%d elements", nElm);

    // Viewport size
    const int W = 800, H = 600;

    // Get bounding box for proper view transform
    double xmin, ymin, xmax, ymax;
    doc.getBoundingBox(xmin, ymin, xmax, ymax);
    double dx = xmax - xmin, dy = ymax - ymin;
    if (dx < 1e-10) dx = 1.0;
    if (dy < 1e-10) dy = 1.0;
    double mag = std::min((double)W / dx, (double)H / dy) * 0.9;
    double ox = xmin - 0.05 * dx;
    double oy = ymin - 0.05 * dy;

    struct AATest {
        const char *name;
        AAQuality quality;
    } tests[] = {
        {"AA None",    AAQuality::None},
        {"AA Low",     AAQuality::Low},
        {"AA High",    AAQuality::High},
        {"AA Ultra",   AAQuality::Ultra},
    };

    for (auto &t : tests) {
        ResultsOverlayRenderer renderer;
        renderer.setDocument(&doc);
        renderer.setShowDensity(DensityType::B_mag);
        renderer.setShowContours(true);
        renderer.setShowMesh(false);
        renderer.setShowLegend(false);
        renderer.setAAQuality(t.quality);

        QImage canvas(W, H, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::white);

        int iters = (nElm > 50000) ? 3 : 10;
        char name[64]; snprintf(name, sizeof(name), "render density+contours [%s]", t.name);
        double ms = timeIt(iters, [&]() {
            QPainter p(&canvas);
            renderer.render(p, ox, oy, mag, W, H);
            p.end();
        });
        record(name, ms, iters, detail);
    }

    // Mesh-only rendering benchmark
    {
        ResultsOverlayRenderer renderer;
        renderer.setDocument(&doc);
        renderer.setShowDensity(DensityType::None);
        renderer.setShowContours(false);
        renderer.setShowMesh(true);
        renderer.setShowLegend(false);
        renderer.setAAQuality(AAQuality::Low);

        QImage canvas(W, H, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::white);

        int iters = (nElm > 50000) ? 5 : 20;
        double ms = timeIt(iters, [&]() {
            QPainter p(&canvas);
            renderer.render(p, ox, oy, mag, W, H);
            p.end();
        });
        record("render mesh-only [AA Low]", ms, iters, detail);
    }
}

// ---------------------------------------------------------------
// 4) In-Process Solver Pipeline Benchmarks
// ---------------------------------------------------------------

static void benchInProcessSolver()
{
    printf("\n=== In-Process Solver Pipeline ===\n\n");

    const char *files[] = {
        TEST_DATA_DIR "/solenoid.fem",
        TEST_DATA_DIR "/lrk.fem",
    };
    const char *names[] = {"solenoid", "lrk"};

    for (int fi = 0; fi < 2; fi++) {
        FemmeDocument doc;
        if (!doc.loadFromFile(QString::fromLatin1(files[fi]))) {
            printf("  %s: could not load — skipping\n", files[fi]);
            continue;
        }

        printf("  --- %s ---\n", names[fi]);

        // Mesh generation only
        {
            char name[64]; snprintf(name, sizeof(name), "meshInProcess [%s]", names[fi]);
            int iters = 3;
            double ms = timeIt(iters, [&]() {
                FemmeDocument d;
                d.loadFromFile(QString::fromLatin1(files[fi]));
                MeshGenerator meshGen;
                std::vector<MeshEdge> edges;
                meshGen.generateMeshInProcess(&d, edges);
            });
            record(name, ms, iters);
        }

        // Full pipeline: mesh + solve + results
        {
            char name[64]; snprintf(name, sizeof(name), "fullSolve [%s]", names[fi]);
            int iters = 3;
            double ms = timeIt(iters, [&]() {
                FemmeDocument d;
                d.loadFromFile(QString::fromLatin1(files[fi]));
                InProcessSolver solver;
                ResultsDocument rdoc;
                solver.solve(&d, &rdoc);
            });
            record(name, ms, iters);
        }

        // Solve only (reuse pre-loaded doc, measures steady-state per-step cost)
        {
            char name[64]; snprintf(name, sizeof(name), "solveOnly [%s]", names[fi]);
            int iters = 5;
            double ms = timeIt(iters, [&]() {
                InProcessSolver solver;
                ResultsDocument rdoc;
                solver.solve(&doc, &rdoc);
            });
            record(name, ms, iters);
        }
    }
}

// ---------------------------------------------------------------
// Print summary table
// ---------------------------------------------------------------

static void printSummary()
{
    printf("\n");
    printf("================================================================\n");
    printf("  BENCHMARK SUMMARY\n");
    printf("================================================================\n");
    printf("  %-42s %10s\n", "Test", "Time (ms)");
    printf("  %-42s %10s\n", "----", "---------");
    for (auto &r : g_results)
        printf("  %-42s %10.3f\n", r.name.c_str(), r.ms);
    printf("================================================================\n\n");
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    printf("\n================================================================\n");
    printf("  FEMM 4.2 Performance Benchmark\n");
    printf("================================================================\n");

    benchGPU();
    benchInProcessSolver();
    benchPostProcessing();
    benchRendering();
    printSummary();

    return 0;
}
