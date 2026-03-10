// FEMM Qt 6 GUI — Motor Optimizer
// 3-phase current computation and phase angle optimisation
#ifndef MOTOROPTIMIZER_H
#define MOTOROPTIMIZER_H

#include <QString>
#include <functional>

class FemmeDocument;
class MeshGenerator;
class SolverRunner;

class MotorOptimizer
{
public:
    struct ThreePhaseCurrents {
        double Ia = 0.0;
        double Ib = 0.0;
        double Ic = 0.0;
    };

    // Compute instantaneous 3-phase currents for a given electrical angle (degrees).
    // Returns peak (not RMS) instantaneous values.
    static ThreePhaseCurrents computeCurrents(double rmsCurrent,
                                               double elecAngleDeg);

    // Apply 3-phase currents to the document's circuit properties.
    // Looks up circuits by name and sets their amps.re field.
    // Returns false if any circuit name is not found.
    static bool applyCurrents(FemmeDocument *doc,
                              const ThreePhaseCurrents &currents,
                              const QString &phaseA,
                              const QString &phaseB,
                              const QString &phaseC);

    // Run the optimisation sweep.
    // Keeps rotor fixed, sweeps electrical angle of 3-phase currents
    // until torque reaches a maximum (i.e. decreases from previous step).
    // This is a BLOCKING call (uses QEventLoop internally).
    // progressCallback(step, angle, torque) — return false to cancel.
    // Returns true on success, filling outOptimalAngle.
    static bool optimize(
        FemmeDocument *doc,
        MeshGenerator *meshGen,
        SolverRunner *solver,
        double rmsCurrent,
        double initialAngle,    // degrees
        double angleStep,       // degrees
        int polePairs,
        const QString &phaseA,
        const QString &phaseB,
        const QString &phaseC,
        double &outOptimalAngle,
        QString &outError,
        std::function<bool(int step, double angle, double torque)> progressCallback = nullptr
    );

    // Run a single solve synchronously and return the total stored energy.
    static bool solveSynchronous(
        FemmeDocument *doc,
        MeshGenerator *meshGen,
        SolverRunner *solver,
        double &outEnergy,
        QString &outError
    );
};

#endif // MOTOROPTIMIZER_H
