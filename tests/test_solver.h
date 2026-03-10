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
};

#endif // TEST_SOLVER_H
