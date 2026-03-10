// FEMM Unit Tests — Mesh Generation Tests
#ifndef TEST_MESH_H
#define TEST_MESH_H

#include <QObject>
#include <QTest>

class TestMesh : public QObject
{
    Q_OBJECT

private slots:
    // Poly file generation
    void writePolyFromSolenoid();

    // Arc discretization
    void arcDiscretization90();
    void arcDiscretization180();

    // Mesh loading from existing files
    void loadMeshNodes();
    void loadMeshElements();
    void meshElementNodeIndicesValid();

    // Bounding box after mesh
    void meshBoundingBox();

    // Edge cases
    void meshWithNoDocument();
    void meshWithEmptyDocument();

    // In-process mesh generation
    void inProcessMeshSolenoid();
    void inProcessMeshEdgesValid();
    void maxAreaAffectsMesh();

    // Results document (.ans) loading
    void loadResultsDocument();
    void resultsNonZeroB();
    void resultsPlotBounds();
    void resultsPointQuery();

    // H-adaptive refinement
    void adaptiveRefineSolenoid();

    // H-adaptive refinement with coarsening back-off
    void adaptiveCoarseningSolenoid();
    void adaptiveCoarseningDisabled();
    void adaptiveAlreadyConverged();
    void adaptiveCoarseningReducesElements();

    // LRK adaptive refinement
    void adaptiveLRKCoarsening();

    // Solve failure recovery
    void adaptiveSolveFailureRecovery();

    // writeMeshFiles round-trip (in-memory mesh → disk → fkn-compatible)
    void writeMeshFilesRoundTrip();
};

#endif // TEST_MESH_H
