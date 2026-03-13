// FEMM Unit Tests — In-Process Solver Tests
#ifndef TEST_SOLVER_H
#define TEST_SOLVER_H

#include <QObject>
#include <QTest>

class TestSolver : public QObject
{
    Q_OBJECT

private slots:
    // Full in-process pipeline: mesh + solve + results
    void solveStaticSolenoid();
    void solveResultsNonZeroB();
    void solveResultsMatchFile();

    // Magnet diagnostics: verify permanent magnets produce field
    void solveLRKMagnetsProduceField();
    void solveLRKFullNonlinear();

    // Load existing .ans file and compare B_High with in-process solve
    void lrkAnsFileResults();

    // Motor torque diagnostic: solve LRK at multiple rotor positions
    // with 3-phase commutation and verify torque consistency
    void lrkMotorTorqueDiagnostic();

    // Torque scaling: verify torque at 1A vs 10A scales roughly linearly
    void lrkTorqueScaling();
};

#endif // TEST_SOLVER_H
