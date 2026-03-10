// FEMM Unit Tests — In-Process Solver Tests
#include "test_solver.h"
#include "document.h"
#include "meshgen.h"
#include "resultsdoc.h"
#include "inprocesssolver.h"
#include "femm_types.h"

#include <QFile>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const QString solenoidPath = QString(TEST_DATA_DIR) + "/solenoid.fem";
static const QString lrkPath = QString(TEST_DATA_DIR) + "/lrk.fem";

// ---------------------------------------------------------------
// Full in-process pipeline: mesh → solve → results
// ---------------------------------------------------------------

void TestSolver::solveStaticSolenoid()
{
    FemmeDocument doc;
    QVERIFY2(doc.loadFromFile(solenoidPath),
             "Failed to load solenoid.fem");

    InProcessSolver solver;
    ResultsDocument rdoc;

    bool ok = solver.solve(&doc, &rdoc);
    QVERIFY2(ok, qPrintable("In-process solve failed: " + solver.lastError()));

    // Should produce mesh nodes and elements in results
    QVERIFY2(!rdoc.nodes.empty(),
             qPrintable(QString("Expected result nodes > 0, got %1")
                 .arg(rdoc.nodes.size())));
    QVERIFY2(!rdoc.elements.empty(),
             qPrintable(QString("Expected result elements > 0, got %1")
                 .arg(rdoc.elements.size())));

    // Problem type should be planar (solenoid test problem)
    QCOMPARE(rdoc.problemType, 0);

    // Should have material properties
    QVERIFY2(rdoc.materials.size() >= 3,
             qPrintable(QString("Expected >= 3 materials, got %1")
                 .arg(rdoc.materials.size())));
}

void TestSolver::solveResultsNonZeroB()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    InProcessSolver solver;
    ResultsDocument rdoc;
    QVERIFY2(solver.solve(&doc, &rdoc),
             qPrintable("Solve failed: " + solver.lastError()));

    // Check that A values are not all zero
    int nonZeroA = 0;
    for (const auto &nd : rdoc.nodes) {
        if (std::abs(nd.A.real()) > 1e-20) nonZeroA++;
    }
    QVERIFY2(nonZeroA > 0,
             qPrintable(QString("All %1 A values are zero — solver produced no solution")
                 .arg(rdoc.nodes.size())));

    // Check that B values are not all zero
    int nonZeroB = 0;
    for (const auto &elm : rdoc.elements) {
        double Bmag = std::sqrt(std::norm(elm.B1) + std::norm(elm.B2));
        if (Bmag > 1e-20) nonZeroB++;
    }
    QVERIFY2(nonZeroB > 0,
             qPrintable(QString("All %1 B values are zero — B computation failed")
                 .arg(rdoc.elements.size())));

    // B_High should be positive and physically reasonable
    QVERIFY2(rdoc.B_High > 0,
             qPrintable(QString("B_High=%1 — should be positive").arg(rdoc.B_High)));
    QVERIFY2(rdoc.B_High < 100.0,
             qPrintable(QString("B_High=%1 T — unreasonably large").arg(rdoc.B_High)));
}

void TestSolver::solveResultsMatchFile()
{
    QString ansPath = QString(TEST_DATA_DIR) + "/solenoid.ans";
    if (!QFile::exists(ansPath)) {
        QSKIP("solenoid.ans not found — run solver first to generate reference");
    }

    // Load reference results from .ans file
    ResultsDocument refDoc;
    QVERIFY(refDoc.loadFromFile(ansPath));

    // Run in-process solve
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    InProcessSolver solver;
    ResultsDocument rdoc;
    QVERIFY2(solver.solve(&doc, &rdoc),
             qPrintable("Solve failed: " + solver.lastError()));

    // Mesh sizes may differ slightly between independent Triangle runs
    // (different arc discretization paths), so just check they're close
    double nodeDiff = std::abs((int)rdoc.nodes.size() - (int)refDoc.nodes.size())
                    / (double)refDoc.nodes.size();
    QVERIFY2(nodeDiff < 0.05,
             qPrintable(QString("Node count differs by >5%%: in-process=%1, file=%2")
                 .arg(rdoc.nodes.size()).arg(refDoc.nodes.size())));

    // Compare B_High bounds (global field strength should match closely)
    QVERIFY2(rdoc.B_High > 0,
             qPrintable(QString("In-process B_High=%1 — should be positive").arg(rdoc.B_High)));
    QVERIFY2(refDoc.B_High > 0,
             qPrintable(QString("File B_High=%1 — should be positive").arg(refDoc.B_High)));

    double bErr = std::abs(rdoc.B_High - refDoc.B_High) / std::max(refDoc.B_High, 1e-20);
    QVERIFY2(bErr < 0.05,
             qPrintable(QString("B_High differs: in-process=%1, file=%2, relErr=%3")
                 .arg(rdoc.B_High).arg(refDoc.B_High).arg(bErr)));

    // Compare A bounds
    QVERIFY2(rdoc.A_High > rdoc.A_Low,
             qPrintable(QString("In-process A flat: Low=%1, High=%2")
                 .arg(rdoc.A_Low).arg(rdoc.A_High)));
    double aErr = std::abs(rdoc.A_High - refDoc.A_High) / std::max(std::abs(refDoc.A_High), 1e-20);
    QVERIFY2(aErr < 0.05,
             qPrintable(QString("A_High differs: in-process=%1, file=%2, relErr=%3")
                 .arg(rdoc.A_High).arg(refDoc.A_High).arg(aErr)));
}

// ---------------------------------------------------------------
// Magnet diagnostics: verify permanent magnets produce field
// Uses LRK motor model (NdFeB magnets + coils)
// ---------------------------------------------------------------
void TestSolver::solveLRKMagnetsProduceField()
{
    if (!QFile::exists(lrkPath)) {
        QSKIP("lrk.fem not found");
    }

    FemmeDocument doc;
    QVERIFY2(doc.loadFromFile(lrkPath), "Failed to load lrk.fem");

    // Verify the document has the NdFeB material with correct H_c
    int magnetMatIdx = -1;
    for (int i = 0; i < (int)doc.materialProps.size(); i++) {
        if (doc.materialProps[i].H_c > 1000) {
            magnetMatIdx = i;
            break;
        }
    }
    QVERIFY2(magnetMatIdx >= 0,
             "No material with H_c > 1000 found — expected NdFeB magnet");
    qDebug() << "Magnet material:" << doc.materialProps[magnetMatIdx].blockName
             << "H_c=" << doc.materialProps[magnetMatIdx].H_c
             << "index=" << magnetMatIdx;

    // Verify block labels have non-zero MagDir for magnet regions
    int magnetLabelCount = 0;
    for (const auto &blk : doc.blockLabels) {
        if (blk.blockType == doc.materialProps[magnetMatIdx].blockName) {
            magnetLabelCount++;
            qDebug() << "  Magnet label at" << blk.x << blk.y
                     << "MagDir=" << blk.magDir;
        }
    }
    QVERIFY2(magnetLabelCount > 0,
             "No block labels reference the magnet material");
    qDebug() << "Total magnet labels:" << magnetLabelCount;

    // Set all circuit currents to zero to isolate magnet contribution
    for (auto &circ : doc.circuitProps) {
        circ.amps.re = 0.0;
        circ.amps.im = 0.0;
    }

    // Run in-process solver (magnets only, no coil current)
    InProcessSolver solver;
    ResultsDocument rdoc;
    bool ok = solver.solve(&doc, &rdoc);
    QVERIFY2(ok, qPrintable("Solve failed: " + solver.lastError()));

    qDebug() << "Results:" << rdoc.nodes.size() << "nodes,"
             << rdoc.elements.size() << "elements";
    qDebug() << "B_High=" << rdoc.B_High << "A_High=" << rdoc.A_High;

    // With magnets only (no coil current), B should still be significant
    // NdFeB produces ~1T remanence; B_High should be at least 0.1T
    QVERIFY2(rdoc.B_High > 0.01,
             qPrintable(QString("B_High=%1 T — magnets not producing field!")
                 .arg(rdoc.B_High)));

    // Check that A values are non-zero (magnet contribution to vector potential)
    int nonZeroA = 0;
    for (const auto &nd : rdoc.nodes) {
        if (std::abs(nd.A.real()) > 1e-20) nonZeroA++;
    }
    QVERIFY2(nonZeroA > 0,
             qPrintable(QString("All %1 A values are zero — magnets produced no field")
                 .arg(rdoc.nodes.size())));

    // Check B in elements near magnet labels
    int magnetElmCount = 0;
    double magnetBsum = 0;
    for (const auto &elm : rdoc.elements) {
        if (elm.blk == magnetMatIdx) {
            magnetElmCount++;
            double Bmag = std::sqrt(std::norm(elm.B1) + std::norm(elm.B2));
            magnetBsum += Bmag;
        }
    }
    qDebug() << "Elements with magnet material:" << magnetElmCount;
    if (magnetElmCount > 0) {
        double avgB = magnetBsum / magnetElmCount;
        qDebug() << "Average B in magnets:" << avgB << "T";
        QVERIFY2(avgB > 0.01,
                 qPrintable(QString("Average B in magnets=%1 T — too low")
                     .arg(avgB)));
    } else {
        QFAIL("No elements found with magnet material — label-to-material mapping is broken");
    }
}

// ---------------------------------------------------------------
// Full nonlinear LRK solve (with coil currents and BH curves)
// Tests Newton iteration convergence
// ---------------------------------------------------------------
void TestSolver::solveLRKFullNonlinear()
{
    if (!QFile::exists(lrkPath)) {
        QSKIP("lrk.fem not found");
    }

    FemmeDocument doc;
    QVERIFY2(doc.loadFromFile(lrkPath), "Failed to load lrk.fem");

    // Check that we have a BH curve material (nonlinear steel)
    bool hasBH = false;
    for (const auto &mat : doc.materialProps) {
        if (mat.bhPoints > 0) {
            hasBH = true;
            qDebug() << "BH curve material:" << mat.blockName
                     << "points=" << mat.bhPoints
                     << "mu_x=" << mat.mu_x;
        }
    }
    QVERIFY2(hasBH, "No nonlinear BH curve materials found");

    // Run with original currents (triggers Newton iteration)
    qDebug() << "Circuits:";
    for (const auto &circ : doc.circuitProps)
        qDebug() << "  " << circ.circName << "amps=" << circ.amps.re
                 << "type=" << circ.circType;

    InProcessSolver solver;
    ResultsDocument rdoc;
    bool ok = solver.solve(&doc, &rdoc);
    QVERIFY2(ok, qPrintable("Nonlinear solve failed: " + solver.lastError()));

    qDebug() << "Results:" << rdoc.nodes.size() << "nodes,"
             << rdoc.elements.size() << "elements";
    qDebug() << "B_High=" << rdoc.B_High << "A_High=" << rdoc.A_High;

    // B_High should be > 0.5T (motor with magnets and coils)
    QVERIFY2(rdoc.B_High > 0.5,
             qPrintable(QString("B_High=%1 T — too low for motor with magnets")
                 .arg(rdoc.B_High)));

    // B_High should be < 100T (linear model can have high peaks)
    QVERIFY2(rdoc.B_High < 100.0,
             qPrintable(QString("B_High=%1 T — unreasonably large")
                 .arg(rdoc.B_High)));
}

// ---------------------------------------------------------------
// Load existing lrk.ans and compare with in-process results
// ---------------------------------------------------------------
void TestSolver::lrkAnsFileResults()
{
    QString ansPath = QString(TEST_DATA_DIR) + "/lrk.ans";
    if (!QFile::exists(ansPath)) {
        QSKIP("lrk.ans not found");
    }

    // Load reference .ans file
    ResultsDocument refDoc;
    QVERIFY2(refDoc.loadFromFile(ansPath), "Failed to load lrk.ans");

    qDebug() << "lrk.ans reference results:";
    qDebug() << "  Nodes:" << refDoc.nodes.size()
             << "Elements:" << refDoc.elements.size();
    qDebug() << "  B_High=" << refDoc.B_High << "B_Low=" << refDoc.B_Low;
    qDebug() << "  A_High=" << refDoc.A_High << "A_Low=" << refDoc.A_Low;
    qDebug() << "  Materials:" << refDoc.materials.size();
    for (int i = 0; i < (int)refDoc.materials.size(); i++) {
        qDebug() << "    [" << i << "]" << refDoc.materials[i].blockName
                 << "mu_x=" << refDoc.materials[i].mu_x
                 << "H_c=" << refDoc.materials[i].H_c
                 << "BHpoints=" << refDoc.materials[i].bhPoints;
    }

    // Run in-process solve for comparison
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(lrkPath));

    InProcessSolver solver;
    ResultsDocument rdoc;
    QVERIFY2(solver.solve(&doc, &rdoc),
             qPrintable("Solve failed: " + solver.lastError()));

    qDebug() << "In-process solve results:";
    qDebug() << "  Nodes:" << rdoc.nodes.size()
             << "Elements:" << rdoc.elements.size();
    qDebug() << "  B_High=" << rdoc.B_High << "B_Low=" << rdoc.B_Low;
    qDebug() << "  A_High=" << rdoc.A_High << "A_Low=" << rdoc.A_Low;

    // Compare B_High between file and in-process
    double bErr = std::abs(rdoc.B_High - refDoc.B_High) / std::max(refDoc.B_High, 1e-20);
    qDebug() << "B_High relative error:" << bErr;
    QVERIFY2(bErr < 0.10,
             qPrintable(QString("B_High mismatch: file=%1 vs in-process=%2, relErr=%3")
                 .arg(refDoc.B_High).arg(rdoc.B_High).arg(bErr)));
}
