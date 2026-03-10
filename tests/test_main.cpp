// FEMM Unit Tests — Main runner
// Runs all test classes sequentially

#include <QApplication>
#include <QTest>
#include <QDebug>

#include "test_document.h"
#include "test_geometry.h"
#include "test_mesh.h"
#include "test_solver.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    int status = 0;

    qDebug() << "\n=== FEMM 4.2 Unit Tests ===\n";

    {
        TestDocument test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        TestGeometry test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        TestMesh test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        TestSolver test;
        status |= QTest::qExec(&test, argc, argv);
    }

    if (status == 0)
        qDebug() << "\n=== ALL TESTS PASSED ===\n";
    else
        qDebug() << "\n=== SOME TESTS FAILED ===\n";

    return status;
}
