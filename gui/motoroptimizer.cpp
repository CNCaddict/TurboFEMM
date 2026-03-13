// FEMM Qt 6 GUI — Motor Optimizer implementation
#include "motoroptimizer.h"
#include "document.h"
#include "meshgen.h"
#include "solverrunner.h"
#include "resultsdoc.h"

#include <QEventLoop>
#include <QObject>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------
// 3-phase current computation
// ---------------------------------------------------------------

MotorOptimizer::ThreePhaseCurrents
MotorOptimizer::computeCurrents(double rmsCurrent, double elecAngleDeg)
{
    double Ipeak = rmsCurrent * std::sqrt(2.0);
    double theta = elecAngleDeg * M_PI / 180.0;

    ThreePhaseCurrents c;
    c.Ia = Ipeak * std::cos(theta);
    c.Ib = Ipeak * std::cos(theta - 2.0 * M_PI / 3.0);
    c.Ic = Ipeak * std::cos(theta + 2.0 * M_PI / 3.0);
    return c;
}

// ---------------------------------------------------------------
// Apply currents to document circuits
// ---------------------------------------------------------------

bool MotorOptimizer::applyCurrents(FemmeDocument *doc,
                                    const ThreePhaseCurrents &currents,
                                    const QString &phaseA,
                                    const QString &phaseB,
                                    const QString &phaseC)
{
    int idxA = doc->findCircuitPropIndex(phaseA);
    int idxB = doc->findCircuitPropIndex(phaseB);
    int idxC = doc->findCircuitPropIndex(phaseC);
    if (idxA < 0 || idxB < 0 || idxC < 0)
        return false;

    doc->circuitProps[idxA].amps = FemmComplex(currents.Ia, 0.0);
    doc->circuitProps[idxB].amps = FemmComplex(currents.Ib, 0.0);
    doc->circuitProps[idxC].amps = FemmComplex(currents.Ic, 0.0);
    return true;
}

// ---------------------------------------------------------------
// Synchronous single-solve wrapper
// ---------------------------------------------------------------

bool MotorOptimizer::solveSynchronous(
    FemmeDocument *doc, MeshGenerator *meshGen, SolverRunner *solver,
    double &outEnergy, QString &outError)
{
    // Save document to disk
    if (!doc->saveToFile(doc->filePath())) {
        outError = QObject::tr("Failed to save document");
        return false;
    }

    // Generate mesh
    if (!meshGen->generateMesh(doc)) {
        outError = QObject::tr("Mesh generation failed: ") + meshGen->lastError();
        return false;
    }

    // Solve synchronously using QEventLoop
    QEventLoop loop;
    bool solveOk = false;
    auto conn = QObject::connect(solver, &SolverRunner::finished,
        [&](bool success) {
            solveOk = success;
            loop.quit();
        });

    solver->runSolver(doc);
    loop.exec();
    QObject::disconnect(conn);

    if (!solveOk) {
        outError = QObject::tr("Solver failed: ") + solver->lastError();
        return false;
    }

    // Load results and extract energy
    QString ansPath = doc->filePath();
    if (ansPath.endsWith(".fem", Qt::CaseInsensitive))
        ansPath = ansPath.left(ansPath.length() - 4) + ".ans";
    else
        ansPath += ".ans";

    ResultsDocument rdoc;
    if (!rdoc.loadFromFile(ansPath)) {
        outError = QObject::tr("Failed to load results from ") + ansPath;
        return false;
    }

    ResultsSummary summary = rdoc.computeSummary();
    outEnergy = summary.totalEnergy;
    return true;
}

// ---------------------------------------------------------------
// Optimisation sweep
// ---------------------------------------------------------------

// Internal helper: sweep in one direction until torque decreases.
// Returns true if a peak was found, filling outOptimalAngle.
// Returns false with outError empty if no peak (caller should try other direction).
// Returns false with outError non-empty on hard failure (solver error, cancel).
static bool sweepDirection(
    FemmeDocument *doc, MeshGenerator *meshGen, SolverRunner *solver,
    double rmsCurrent, double startAngle, double signedStep,
    const QString &phaseA, const QString &phaseB, const QString &phaseC,
    double &outOptimalAngle, QString &outError,
    std::function<bool(int, double, double)> progressCallback,
    int stepOffset)
{
    std::vector<double> energies;
    std::vector<double> angles;
    double dTheta_rad = std::fabs(signedStep) * M_PI / 180.0;

    int maxSteps = (int)(360.0 / std::fabs(signedStep)) + 2;
    double angle = startAngle;

    for (int step = 0; step <= maxSteps; step++) {
        auto currents = MotorOptimizer::computeCurrents(rmsCurrent, angle);
        if (!MotorOptimizer::applyCurrents(doc, currents, phaseA, phaseB, phaseC)) {
            outError = QObject::tr("Failed to apply currents — circuit not found");
            return false;
        }

        double energy = 0.0;
        if (!MotorOptimizer::solveSynchronous(doc, meshGen, solver, energy, outError))
            return false;

        energies.push_back(energy);
        angles.push_back(angle);

        if (energies.size() >= 2) {
            int i = (int)energies.size() - 1;
            // NOTE: We sweep the *stator* electrical angle at fixed rotor.
            // Increasing stator angle toward the "attracting" direction
            // *decreases* stored energy.  Physical rotor torque (from MST)
            // is T = -dW/dθ_mech, but here dθ is the stator angle, so the
            // sign is inverted: positive dE/dθ_stator corresponds to
            // positive physical torque.  Use +dE/dθ (no minus sign) so
            // the optimizer finds the angle of maximum *positive* torque.
            double torque = (energies[i] - energies[i - 1]) / dTheta_rad;

            if (progressCallback) {
                if (!progressCallback(stepOffset + step, angle, torque)) {
                    outError = QObject::tr("Cancelled by user");
                    return false;
                }
            }

            // Check if torque decreased (we passed the peak).
            // Only accept peaks where the torque is POSITIVE — a peak
            // in the negative region corresponds to maximum braking
            // torque, not motoring.  Skip those and keep sweeping until
            // we find the positive (motoring) peak.
            if (energies.size() >= 3) {
                double prevTorque = (energies[i - 1] - energies[i - 2]) / dTheta_rad;
                if (torque < prevTorque && prevTorque > 0.0) {
                    outOptimalAngle = angles[i - 1];
                    return true;
                }
            }
        }

        angle += signedStep;
    }

    // No peak in this direction — not a hard error
    outError.clear();
    return false;
}

bool MotorOptimizer::optimize(
    FemmeDocument *doc, MeshGenerator *meshGen, SolverRunner *solver,
    double rmsCurrent, double initialAngle, double angleStep,
    int /*polePairs*/, const QString &phaseA, const QString &phaseB,
    const QString &phaseC, double &outOptimalAngle, QString &outError,
    std::function<bool(int, double, double)> progressCallback)
{
    // Backup original circuit properties
    std::vector<FCircuit> backupCircuits = doc->circuitProps;

    // Try sweeping in the positive direction first
    if (sweepDirection(doc, meshGen, solver, rmsCurrent,
                       initialAngle, angleStep,
                       phaseA, phaseB, phaseC,
                       outOptimalAngle, outError,
                       progressCallback, 0))
    {
        doc->circuitProps = backupCircuits;
        return true;
    }

    // If it was a hard failure (solver error or cancellation), stop
    if (!outError.isEmpty()) {
        doc->circuitProps = backupCircuits;
        return false;
    }

    // No peak in positive direction — reverse and try negative
    if (sweepDirection(doc, meshGen, solver, rmsCurrent,
                       initialAngle - angleStep, -angleStep,
                       phaseA, phaseB, phaseC,
                       outOptimalAngle, outError,
                       progressCallback, 0))
    {
        doc->circuitProps = backupCircuits;
        return true;
    }

    if (!outError.isEmpty()) {
        doc->circuitProps = backupCircuits;
        return false;
    }

    outError = QObject::tr("No torque peak found in either direction");
    doc->circuitProps = backupCircuits;
    return false;
}
