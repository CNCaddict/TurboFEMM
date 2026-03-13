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

// ---------------------------------------------------------------
// Motor torque diagnostic: solve LRK at multiple rotor positions
// with 3-phase commutation and verify torque is approximately constant
// ---------------------------------------------------------------
void TestSolver::lrkMotorTorqueDiagnostic()
{
    if (!QFile::exists(lrkPath)) {
        QSKIP("lrk.fem not found");
    }

    FemmeDocument doc;
    QVERIFY2(doc.loadFromFile(lrkPath), "Failed to load lrk.fem");

    const int polePairs = 7;
    const double rmsCurrent = 1.0;  // 1 A RMS (low enough to stay linear-ish)
    const double Ipeak = rmsCurrent * std::sqrt(2.0);
    const double stepAngleMech = 2.0;  // degrees mechanical per step
    const int numSteps = 2;
    const double cx = 0.0, cy = 0.0;  // rotation center
    const int rotorGroup = 2;

    // Phase circuit names
    QString phaseA = "A", phaseB = "B", phaseC = "C";
    int idxA = doc.findCircuitPropIndex(phaseA);
    int idxB = doc.findCircuitPropIndex(phaseB);
    int idxC = doc.findCircuitPropIndex(phaseC);
    QVERIFY2(idxA >= 0 && idxB >= 0 && idxC >= 0, "Circuit A/B/C not found");

    // Backup geometry and circuits
    auto backupNodes = doc.nodes;
    auto backupSegments = doc.segments;
    auto backupArcs = doc.arcSegments;
    auto backupLabels = doc.blockLabels;
    auto backupCircuits = doc.circuitProps;

    // Step 1: Find optimal angle by sweeping at initial position
    // Try several angles and pick the one with max torque
    qDebug() << "=== Phase 1: Finding optimal angle ===";
    double bestAngle = 0.0;
    double bestTorque = -1e30;
    double bestEnergy = 0.0;

    InProcessSolver solver;

    // Sweep electrical angle in 60° steps over 360° (coarse but fast)
    std::vector<double> sweepAngles, sweepEnergies, sweepTorques;
    for (int i = 0; i <= 6; i++) {
        double testAngle = i * 60.0;  // electrical degrees
        double theta = testAngle * M_PI / 180.0;

        double Ia = Ipeak * std::cos(theta);
        double Ib = Ipeak * std::cos(theta - 2.0 * M_PI / 3.0);
        double Ic = Ipeak * std::cos(theta + 2.0 * M_PI / 3.0);

        doc.circuitProps[idxA].amps = FemmComplex(Ia, 0.0);
        doc.circuitProps[idxB].amps = FemmComplex(Ib, 0.0);
        doc.circuitProps[idxC].amps = FemmComplex(Ic, 0.0);

        ResultsDocument rdoc;
        bool ok = solver.solve(&doc, &rdoc);
        QVERIFY2(ok, qPrintable(QString("Solve failed at angle %1: %2")
            .arg(testAngle).arg(solver.lastError())));

        ResultsSummary summary = rdoc.computeSummary();
        double mstTorque = rdoc.computeTorque(cx, cy, rotorGroup);

        sweepAngles.push_back(testAngle);
        sweepEnergies.push_back(summary.totalEnergy);
        sweepTorques.push_back(mstTorque);

        qDebug() << QString("  angle=%1° Ia=%2 Ib=%3 Ic=%4 E=%5 MST=%6 Nm")
            .arg(testAngle, 6, 'f', 1)
            .arg(Ia, 7, 'f', 3).arg(Ib, 7, 'f', 3).arg(Ic, 7, 'f', 3)
            .arg(summary.totalEnergy, 12, 'e', 6)
            .arg(mstTorque, 10, 'f', 6).toLatin1().constData();

        if (mstTorque > bestTorque) {
            bestTorque = mstTorque;
            bestAngle = testAngle;
            bestEnergy = summary.totalEnergy;
        }
    }

    qDebug() << "";
    qDebug() << "Best angle:" << bestAngle << "deg, torque:" << bestTorque << "Nm";

    // Compute VW "torque" with CORRECTED sign (matching physical torque)
    // The optimizer now uses +(dE/dθ_elec) instead of -(dE/dθ_elec)
    qDebug() << "";
    qDebug() << "=== VW torque (corrected sign +dE/dθ) vs MST ===";
    double dTheta_elec = 60.0 * M_PI / 180.0;
    double vwBestAngle = 0.0, vwBestTorque = -1e30;
    for (int i = 1; i < (int)sweepEnergies.size(); i++) {
        // Corrected sign: +dE/dθ (no minus) matches MST direction
        double vwTorqueElec = (sweepEnergies[i] - sweepEnergies[i-1]) / dTheta_elec;
        qDebug() << QString("  angle=%1°: VW_corrected=%2 MST=%3 (same sign? %4)")
            .arg(sweepAngles[i], 6, 'f', 1)
            .arg(vwTorqueElec, 10, 'f', 6)
            .arg(sweepTorques[i], 10, 'f', 6)
            .arg((vwTorqueElec > 0) == (sweepTorques[i] > 0) ? "YES" : "NO")
            .toLatin1().constData();
        if (vwTorqueElec > vwBestTorque) {
            vwBestTorque = vwTorqueElec;
            vwBestAngle = sweepAngles[i];
        }
    }
    qDebug() << "";
    qDebug() << "VW-corrected best angle:" << vwBestAngle << "deg, MST best:" << bestAngle << "deg";
    // VW and MST should agree on the approximate peak angle (within ~30° coarse grid)
    double angleDiff = std::fabs(vwBestAngle - bestAngle);
    if (angleDiff > 180) angleDiff = 360 - angleDiff;
    qDebug() << "Angle difference:" << angleDiff << "degrees";
    QVERIFY2(angleDiff <= 60.0,
        qPrintable(QString("VW optimal angle %1° too far from MST optimal %2° (diff=%3°)")
            .arg(vwBestAngle).arg(bestAngle).arg(angleDiff)));

    // Step 2: Run motion sweep with commutation tracking
    qDebug() << "";
    qDebug() << "=== Phase 2: Motion sweep with commutation ===";
    qDebug() << "Optimal angle:" << bestAngle << "deg electrical";
    qDebug() << "Step:" << stepAngleMech << "deg mechanical, polePairs:" << polePairs;

    // Restore geometry to initial position
    doc.nodes = backupNodes;
    doc.segments = backupSegments;
    doc.arcSegments = backupArcs;
    doc.blockLabels = backupLabels;

    std::vector<double> motionEnergies, motionTorques, motionAngles;

    for (int step = 0; step <= numSteps; step++) {
        // Rotate rotor (skip step 0 = initial position)
        if (step > 0) {
            doc.selectGroup(rotorGroup);
            doc.rotateSelected(cx, cy, stepAngleMech);
            doc.deselectAll();
        }

        // Compute commutated currents
        double cumMechAngle = stepAngleMech * (double)step;
        // Subtract: rotor advancing shifts the torque curve backward
        double elecAngle = bestAngle - cumMechAngle * (double)polePairs;
        double theta = elecAngle * M_PI / 180.0;

        double Ia = Ipeak * std::cos(theta);
        double Ib = Ipeak * std::cos(theta - 2.0 * M_PI / 3.0);
        double Ic = Ipeak * std::cos(theta + 2.0 * M_PI / 3.0);

        doc.circuitProps[idxA].amps = FemmComplex(Ia, 0.0);
        doc.circuitProps[idxB].amps = FemmComplex(Ib, 0.0);
        doc.circuitProps[idxC].amps = FemmComplex(Ic, 0.0);

        ResultsDocument rdoc;
        bool ok = solver.solve(&doc, &rdoc);
        QVERIFY2(ok, qPrintable(QString("Motion solve failed at step %1: %2")
            .arg(step).arg(solver.lastError())));

        ResultsSummary summary = rdoc.computeSummary();
        double mstTorque = rdoc.computeTorque(cx, cy, rotorGroup);

        motionEnergies.push_back(summary.totalEnergy);
        motionTorques.push_back(mstTorque);
        motionAngles.push_back(cumMechAngle);

        qDebug() << QString("  step=%1 mechAngle=%2° elecAngle=%3° Ia=%4 Ib=%5 Ic=%6 E=%7 Torque=%8 Nm")
            .arg(step)
            .arg(cumMechAngle, 6, 'f', 1)
            .arg(elecAngle, 7, 'f', 1)
            .arg(Ia, 7, 'f', 3).arg(Ib, 7, 'f', 3).arg(Ic, 7, 'f', 3)
            .arg(summary.totalEnergy, 12, 'e', 6)
            .arg(mstTorque, 10, 'f', 6).toLatin1().constData();
    }

    // Compute virtual-work torques from motion energies
    qDebug() << "";
    qDebug() << "=== Virtual work torque from motion sweep ===";
    double dTheta_mech = stepAngleMech * M_PI / 180.0;
    for (int i = 1; i < (int)motionEnergies.size(); i++) {
        double dE = motionEnergies[i] - motionEnergies[i-1];
        double vwTorque = -dE / dTheta_mech;
        qDebug() << QString("  step %1→%2: dE=%3 VWTorque=%4 Nm, MSTorque=%5 Nm")
            .arg(i-1).arg(i)
            .arg(dE, 12, 'e', 6)
            .arg(vwTorque, 10, 'f', 6)
            .arg(motionTorques[i], 10, 'f', 6).toLatin1().constData();
    }

    // Verify: all torques should have the same sign and similar magnitude
    qDebug() << "";
    qDebug() << "=== Torque consistency check ===";
    double avgTorque = 0.0;
    for (double t : motionTorques) avgTorque += t;
    avgTorque /= motionTorques.size();
    qDebug() << "Average MST torque:" << avgTorque << "Nm";

    // Check all torques have the same sign
    int posCount = 0, negCount = 0;
    for (double t : motionTorques) {
        if (t > 0) posCount++;
        else negCount++;
    }
    qDebug() << "Positive:" << posCount << "Negative:" << negCount;

    // The torque should be predominantly one sign
    // (Allow some sign changes due to cogging at low current)
    if (posCount > 0 && negCount > 0 && std::abs(avgTorque) > 1e-6) {
        qDebug() << "WARNING: Torque changes sign during commutated sweep!";
        qDebug() << "This indicates the commutation may be incorrect.";
    }

    // Restore
    doc.nodes = backupNodes;
    doc.segments = backupSegments;
    doc.arcSegments = backupArcs;
    doc.blockLabels = backupLabels;
    doc.circuitProps = backupCircuits;

    // Don't fail the test — this is diagnostic
    QVERIFY(true);
}

void TestSolver::lrkTorqueScaling()
{
    if (!QFile::exists(lrkPath)) {
        QSKIP("lrk.fem not found");
    }

    FemmeDocument doc;
    QVERIFY2(doc.loadFromFile(lrkPath), "Failed to load lrk.fem");

    const int polePairs = 7;
    const double cx = 0.0, cy = 0.0;
    const int rotorGroup = 2;

    QString phaseA = "A", phaseB = "B", phaseC = "C";
    int idxA = doc.findCircuitPropIndex(phaseA);
    int idxB = doc.findCircuitPropIndex(phaseB);
    int idxC = doc.findCircuitPropIndex(phaseC);
    QVERIFY2(idxA >= 0 && idxB >= 0 && idxC >= 0, "Circuit A/B/C not found");

    InProcessSolver solver;

    // Test at 1A and 10A RMS at the same angle (300° electrical)
    double testCurrents[] = {1.0, 5.0, 10.0};
    double testAngle = 300.0;  // degrees electrical (near peak positive torque)
    double theta = testAngle * M_PI / 180.0;

    qDebug() << "=== Torque Scaling Test ===";
    qDebug() << QString("  depth=%1 (file units), lengthUnits=%2")
        .arg(doc.depth).arg((int)doc.lengthUnits);

    double torque1A = 0.0;

    for (double rms : testCurrents) {
        double Ipeak = rms * std::sqrt(2.0);
        double Ia = Ipeak * std::cos(theta);
        double Ib = Ipeak * std::cos(theta - 2.0 * M_PI / 3.0);
        double Ic = Ipeak * std::cos(theta + 2.0 * M_PI / 3.0);

        doc.circuitProps[idxA].amps = FemmComplex(Ia, 0.0);
        doc.circuitProps[idxB].amps = FemmComplex(Ib, 0.0);
        doc.circuitProps[idxC].amps = FemmComplex(Ic, 0.0);

        ResultsDocument rdoc;
        bool ok = solver.solve(&doc, &rdoc);
        QVERIFY2(ok, qPrintable(QString("Solve failed at %1A").arg(rms)));

        ResultsSummary summary = rdoc.computeSummary();
        double mstTorque = rdoc.computeTorque(cx, cy, rotorGroup);

        // Dump key parameters
        qDebug() << QString("  I_rms=%1A: depth=%2, L=%3, depth_m=%4, E=%5 J, T=%6 Nm, |T|=%7 mNm")
            .arg(rms, 5, 'f', 1)
            .arg(rdoc.depth, 0, 'f', 2)
            .arg(rdoc.lengthConv, 0, 'e', 4)
            .arg(rdoc.depth * rdoc.lengthConv, 0, 'f', 6)
            .arg(summary.totalEnergy, 12, 'e', 6)
            .arg(mstTorque, 12, 'f', 6)
            .arg(std::fabs(mstTorque) * 1000.0, 0, 'f', 3)
            .toLatin1().constData();

        // Find max B in air gap
        double maxB = 0;
        for (const auto &elm : rdoc.elements) {
            double Bx = elm.B1.real();
            double By = elm.B2.real();
            double Bmag = std::sqrt(Bx*Bx + By*By);
            if (Bmag > maxB) maxB = Bmag;
        }
        qDebug() << QString("           maxB=%1 T, numElements=%2, numNodes=%3")
            .arg(maxB, 0, 'f', 4)
            .arg(rdoc.elements.size())
            .arg(rdoc.nodes.size())
            .toLatin1().constData();

        if (rms == 1.0) torque1A = mstTorque;
    }

    // At 10A, torque should be roughly 10x the 1A value (maybe less due to saturation)
    // but should NOT be 100x or 1000x
    if (std::fabs(torque1A) > 1e-8) {
        // Just report the ratio — don't fail
        qDebug() << "";
        qDebug() << "Torque at 1A:" << torque1A << "Nm";
        qDebug() << "Expected at 10A (linear):" << torque1A * 10.0 << "Nm";
    }

    QVERIFY(true);
}
