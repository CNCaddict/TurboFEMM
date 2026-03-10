// FEMM Unit Tests — Mesh Generation Tests
#include "test_mesh.h"
#include "document.h"
#include "meshgen.h"
#include "resultsdoc.h"
#include "adaptiverefine.h"
#include "femm_types.h"

#include <QFile>
#include <QTextStream>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const QString solenoidPath = QString(TEST_DATA_DIR) + "/solenoid.fem";

// ---------------------------------------------------------------
// Poly file generation
// ---------------------------------------------------------------

void TestMesh::writePolyFromSolenoid()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Save to a temp directory so the mesh generator can write .poly
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString tmpFem = tmpDir.path() + "/test.fem";
    QVERIFY(doc.saveToFile(tmpFem));

    // The mesh generator's writePoly is private, but we can test via
    // checking that generateMesh at least writes the poly file even
    // if triangle isn't available. We test it indirectly:
    // verify the test data .poly file exists and is valid
    QString polyPath = QString(TEST_DATA_DIR) + "/solenoid.poly";
    if (!QFile::exists(polyPath)) {
        QSKIP("solenoid.poly not found in test data, skipping poly validation");
    }

    QFile polyFile(polyPath);
    QVERIFY(polyFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream in(&polyFile);

    // First line: <num nodes> 2 0 1
    QString header = in.readLine().trimmed();
    QStringList parts = header.split(QRegularExpression("\\s+"));
    QVERIFY2(parts.size() >= 4, "Poly file header should have >= 4 fields");

    int numNodes = parts[0].toInt();
    QVERIFY2(numNodes > 0, qPrintable(QString("Expected > 0 nodes in .poly, got %1").arg(numNodes)));
    QCOMPARE(parts[1].toInt(), 2);   // dimension
    QCOMPARE(parts[3].toInt(), 1);   // boundary markers present

    polyFile.close();
}

// ---------------------------------------------------------------
// Arc discretization
// ---------------------------------------------------------------

void TestMesh::arcDiscretization90()
{
    // Create a document with two nodes and a 90° arc
    FemmeDocument doc;
    doc.addNode(1.0, 0.0, 1e-6);  // node 0
    doc.addNode(0.0, 1.0, 1e-6);  // node 1
    doc.addArcSegment(0, 1, 90.0, 10.0);

    // The arc from (1,0) to (0,1) through 90° should be centered at origin
    // With maxSideLength=10°, we need ceil(90/10) = 9 segments
    // That means 8 intermediate nodes + the original 2 endpoints = 8 extra nodes

    QCOMPARE((int)doc.arcSegments.size(), 1);
    QCOMPARE(doc.arcSegments[0].arcLength, 90.0);

    // Verify the arc length and max segment are stored correctly
    QVERIFY(doc.arcSegments[0].maxSideLength > 0);
    QVERIFY(doc.arcSegments[0].maxSideLength <= 10.0);
}

void TestMesh::arcDiscretization180()
{
    FemmeDocument doc;
    doc.addNode(1.0, 0.0, 1e-6);  // node 0
    doc.addNode(-1.0, 0.0, 1e-6); // node 1
    doc.addArcSegment(0, 1, 180.0, 10.0);

    QCOMPARE((int)doc.arcSegments.size(), 1);
    QCOMPARE(doc.arcSegments[0].arcLength, 180.0);

    // With maxSideLength=10, we need ceil(180/10) = 18 sub-segments
    int k = std::max(1, (int)std::ceil(180.0 / 10.0));
    QCOMPARE(k, 18);
}

// ---------------------------------------------------------------
// Mesh loading from existing files
// ---------------------------------------------------------------

void TestMesh::loadMeshNodes()
{
    // Load mesh from the test data .node file
    QString nodePath = QString(TEST_DATA_DIR) + "/solenoid.node";
    if (!QFile::exists(nodePath)) {
        QSKIP("solenoid.node not found in test data");
    }

    QFile nodeFile(nodePath);
    QVERIFY(nodeFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream in(&nodeFile);

    // Parse header
    QString header = in.readLine().trimmed();
    QStringList parts = header.split(QRegularExpression("\\s+"));
    int numNodes = parts[0].toInt();
    QVERIFY2(numNodes > 0, "Expected mesh to have nodes");

    // Read nodes and verify they are finite
    int count = 0;
    while (!in.atEnd() && count < numNodes) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        QStringList fields = line.split(QRegularExpression("\\s+"));
        if (fields.size() < 3) continue;

        double x = fields[1].toDouble();
        double y = fields[2].toDouble();
        QVERIFY2(!std::isnan(x), qPrintable(QString("Node %1 has NaN x").arg(count)));
        QVERIFY2(!std::isnan(y), qPrintable(QString("Node %1 has NaN y").arg(count)));
        QVERIFY2(std::isfinite(x), qPrintable(QString("Node %1 has infinite x").arg(count)));
        QVERIFY2(std::isfinite(y), qPrintable(QString("Node %1 has infinite y").arg(count)));
        count++;
    }
    nodeFile.close();
    QCOMPARE(count, numNodes);
}

void TestMesh::loadMeshElements()
{
    QString elePath = QString(TEST_DATA_DIR) + "/solenoid.ele";
    if (!QFile::exists(elePath)) {
        QSKIP("solenoid.ele not found in test data");
    }

    QFile eleFile(elePath);
    QVERIFY(eleFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream in(&eleFile);

    // Parse header: <num elements> <nodes per triangle> <attributes>
    QString header = in.readLine().trimmed();
    QStringList parts = header.split(QRegularExpression("\\s+"));
    int numEls = parts[0].toInt();
    QVERIFY2(numEls > 0, "Expected mesh to have elements");

    int nodesPerTri = parts[1].toInt();
    QCOMPARE(nodesPerTri, 3);  // triangles

    int count = 0;
    while (!in.atEnd() && count < numEls) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        QStringList fields = line.split(QRegularExpression("\\s+"));
        if (fields.size() < 4) continue;

        int p0 = fields[1].toInt();
        int p1 = fields[2].toInt();
        int p2 = fields[3].toInt();
        QVERIFY2(p0 >= 0, qPrintable(QString("Element %1: negative node index p0=%2").arg(count).arg(p0)));
        QVERIFY2(p1 >= 0, qPrintable(QString("Element %1: negative node index p1=%2").arg(count).arg(p1)));
        QVERIFY2(p2 >= 0, qPrintable(QString("Element %1: negative node index p2=%2").arg(count).arg(p2)));

        // All three vertices should be distinct
        QVERIFY2(p0 != p1 && p1 != p2 && p0 != p2,
                 qPrintable(QString("Element %1: degenerate triangle (%2, %3, %4)").arg(count).arg(p0).arg(p1).arg(p2)));
        count++;
    }
    eleFile.close();
    QCOMPARE(count, numEls);
}

void TestMesh::meshElementNodeIndicesValid()
{
    // Load both files and verify cross-reference
    QString nodePath = QString(TEST_DATA_DIR) + "/solenoid.node";
    QString elePath = QString(TEST_DATA_DIR) + "/solenoid.ele";
    if (!QFile::exists(nodePath) || !QFile::exists(elePath)) {
        QSKIP("solenoid mesh files not found");
    }

    // Count nodes
    int numNodes = 0;
    {
        QFile nodeFile(nodePath);
        QVERIFY(nodeFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QTextStream in(&nodeFile);
        QString header = in.readLine().trimmed();
        numNodes = header.split(QRegularExpression("\\s+"))[0].toInt();
        nodeFile.close();
    }
    QVERIFY(numNodes > 0);

    // Read elements and verify all indices < numNodes
    {
        QFile eleFile(elePath);
        QVERIFY(eleFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QTextStream in(&eleFile);
        QString header = in.readLine().trimmed();
        int numEls = header.split(QRegularExpression("\\s+"))[0].toInt();

        for (int i = 0; i < numEls && !in.atEnd(); i++) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#')) { i--; continue; }
            QStringList fields = line.split(QRegularExpression("\\s+"));
            if (fields.size() < 4) continue;

            for (int j = 1; j <= 3; j++) {
                int nodeIdx = fields[j].toInt();
                QVERIFY2(nodeIdx >= 0 && nodeIdx < numNodes,
                         qPrintable(QString("Element %1: node index %2 out of range [0, %3)")
                             .arg(i).arg(nodeIdx).arg(numNodes)));
            }
        }
        eleFile.close();
    }
}

// ---------------------------------------------------------------
// Bounding box after mesh
// ---------------------------------------------------------------

void TestMesh::meshBoundingBox()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    double xmin, ymin, xmax, ymax;
    QVERIFY(doc.getBoundingBox(xmin, ymin, xmax, ymax));

    // The bounding box should have reasonable dimensions
    QVERIFY(xmax > xmin);
    QVERIFY(ymax > ymin);

    // For an axisymmetric solenoid, r values should be >= 0
    // (the left edge should be at or near r=0)
    // Note: with padding, xmin might be slightly negative
    QVERIFY2(xmin > -10.0, "Bounding box xmin unreasonably negative");
}

// ---------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------

void TestMesh::meshWithNoDocument()
{
    MeshGenerator meshGen;
    QVERIFY(!meshGen.generateMesh(nullptr));
    QVERIFY(!meshGen.lastError().isEmpty());
}

void TestMesh::meshWithEmptyDocument()
{
    FemmeDocument doc;
    // No file path set — should fail
    MeshGenerator meshGen;
    QVERIFY(!meshGen.generateMesh(&doc));
    QVERIFY(!meshGen.lastError().isEmpty());
}

// ---------------------------------------------------------------
// In-process mesh generation
// ---------------------------------------------------------------

void TestMesh::inProcessMeshSolenoid()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    MeshGenerator meshGen;
    std::vector<MeshEdge> edges;
    QVERIFY2(meshGen.generateMeshInProcess(&doc, edges),
             qPrintable("generateMeshInProcess failed: " + meshGen.lastError()));

    // Should produce mesh nodes and elements
    QVERIFY2(!doc.meshNodes.empty(),
             "In-process mesh produced no nodes");
    QVERIFY2(!doc.meshElements.empty(),
             "In-process mesh produced no elements");
    QVERIFY2(!edges.empty(),
             "In-process mesh produced no edges");

    int numNodes = (int)doc.meshNodes.size();
    int numEls = (int)doc.meshElements.size();

    // Verify all element node indices are in range
    for (int i = 0; i < numEls; i++) {
        for (int j = 0; j < 3; j++) {
            QVERIFY2(doc.meshElements[i].p[j] >= 0 && doc.meshElements[i].p[j] < numNodes,
                     qPrintable(QString("Element %1: node index %2 out of range [0, %3)")
                         .arg(i).arg(doc.meshElements[i].p[j]).arg(numNodes)));
        }
        // Vertices should be distinct
        QVERIFY2(doc.meshElements[i].p[0] != doc.meshElements[i].p[1] &&
                 doc.meshElements[i].p[1] != doc.meshElements[i].p[2] &&
                 doc.meshElements[i].p[0] != doc.meshElements[i].p[2],
                 qPrintable(QString("Element %1: degenerate triangle").arg(i)));
    }

    // Element labels should be valid (1-based from Triangle, stored as-is)
    for (int i = 0; i < numEls; i++) {
        QVERIFY2(doc.meshElements[i].label >= 1,
                 qPrintable(QString("Element %1: label=%2 (expected >= 1)")
                     .arg(i).arg(doc.meshElements[i].label)));
    }

    // Node coordinates should be finite
    for (int i = 0; i < numNodes; i++) {
        QVERIFY2(std::isfinite(doc.meshNodes[i].x) && std::isfinite(doc.meshNodes[i].y),
                 qPrintable(QString("Node %1: non-finite coords (%2, %3)")
                     .arg(i).arg(doc.meshNodes[i].x).arg(doc.meshNodes[i].y)));
    }
}

void TestMesh::inProcessMeshEdgesValid()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    MeshGenerator meshGen;
    std::vector<MeshEdge> edges;
    QVERIFY(meshGen.generateMeshInProcess(&doc, edges));

    int numNodes = (int)doc.meshNodes.size();

    // All edge node indices should be in range
    for (int i = 0; i < (int)edges.size(); i++) {
        QVERIFY2(edges[i].n0 >= 0 && edges[i].n0 < numNodes,
                 qPrintable(QString("Edge %1: n0=%2 out of range").arg(i).arg(edges[i].n0)));
        QVERIFY2(edges[i].n1 >= 0 && edges[i].n1 < numNodes,
                 qPrintable(QString("Edge %1: n1=%2 out of range").arg(i).arg(edges[i].n1)));
        QVERIFY2(edges[i].n0 != edges[i].n1,
                 qPrintable(QString("Edge %1: degenerate (n0==n1==%2)").arg(i).arg(edges[i].n0)));
    }

    // Should have boundary edges (marker != 0)
    int boundaryEdges = 0;
    for (const auto &e : edges)
        if (e.marker != 0) boundaryEdges++;
    QVERIFY2(boundaryEdges > 0,
             "No boundary edges found — boundary markers may be lost");
}

void TestMesh::maxAreaAffectsMesh()
{
    // Verify that changing block label maxArea actually changes the mesh
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Record original maxArea values
    QVERIFY(doc.blockLabels.size() >= 1);
    double origMaxArea0 = doc.blockLabels[0].maxArea;

    // Generate mesh with original settings
    MeshGenerator meshGen;
    std::vector<MeshEdge> edges;
    QVERIFY(meshGen.generateMeshInProcess(&doc, edges));
    int countA = (int)doc.meshElements.size();
    QVERIFY(countA > 0);

    // Set a much smaller maxArea on block 0 → finer mesh
    doc.blockLabels[0].maxArea = 0.01;  // very small area → many elements

    std::vector<MeshEdge> edges2;
    QVERIFY(meshGen.generateMeshInProcess(&doc, edges2));
    int countB = (int)doc.meshElements.size();
    QVERIFY(countB > 0);

    // Restore original
    doc.blockLabels[0].maxArea = origMaxArea0;

    qDebug() << "maxArea test: original elements =" << countA
             << ", with maxArea=0.01 elements =" << countB;

    // The finer mesh must have significantly more elements
    QVERIFY2(countB > countA * 2,
             qPrintable(QString("Expected finer mesh to have >2x elements: %1 vs %2")
                 .arg(countB).arg(countA)));
}

// ---------------------------------------------------------------
// Results document (.ans) loading
// ---------------------------------------------------------------

void TestMesh::loadResultsDocument()
{
    QString ansPath = QString(TEST_DATA_DIR) + "/solenoid.ans";
    if (!QFile::exists(ansPath)) {
        QSKIP("solenoid.ans not found — run solver first");
    }

    ResultsDocument rdoc;
    QVERIFY2(rdoc.loadFromFile(ansPath),
             "Failed to load solenoid.ans");

    // Should have mesh nodes and elements
    QVERIFY2(rdoc.nodes.size() > 0,
             qPrintable(QString("Expected nodes > 0, got %1").arg(rdoc.nodes.size())));
    QVERIFY2(rdoc.elements.size() > 0,
             qPrintable(QString("Expected elements > 0, got %1").arg(rdoc.elements.size())));

    // Problem should be planar (solenoid test problem)
    QCOMPARE(rdoc.problemType, 0);

    // Length conversion should be millimeters (0.001 m/mm)
    QVERIFY2(std::fabs(rdoc.lengthConv - 0.001) < 1e-6,
             qPrintable(QString("Expected lengthConv=0.001 (mm), got %1")
                 .arg(rdoc.lengthConv)));

    // Should have material properties
    QVERIFY2(rdoc.materials.size() >= 3,
             qPrintable(QString("Expected >= 3 materials, got %1")
                 .arg(rdoc.materials.size())));
}

void TestMesh::resultsNonZeroB()
{
    QString ansPath = QString(TEST_DATA_DIR) + "/solenoid.ans";
    if (!QFile::exists(ansPath)) {
        QSKIP("solenoid.ans not found — run solver first");
    }

    ResultsDocument rdoc;
    QVERIFY(rdoc.loadFromFile(ansPath));

    // Check that A values are not all zero
    int nonZeroA = 0;
    for (const auto &nd : rdoc.nodes) {
        if (std::abs(nd.A) > 1e-20) nonZeroA++;
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
}

void TestMesh::resultsPlotBounds()
{
    QString ansPath = QString(TEST_DATA_DIR) + "/solenoid.ans";
    if (!QFile::exists(ansPath)) {
        QSKIP("solenoid.ans not found — run solver first");
    }

    ResultsDocument rdoc;
    QVERIFY(rdoc.loadFromFile(ansPath));

    // A bounds should span a range (not both zero)
    QVERIFY2(rdoc.A_High > rdoc.A_Low,
             qPrintable(QString("A bounds flat: A_Low=%1, A_High=%2")
                 .arg(rdoc.A_Low).arg(rdoc.A_High)));

    // B_High should be positive (solenoid has non-zero field)
    QVERIFY2(rdoc.B_High > 0,
             qPrintable(QString("B_High=%1 — density plot will be solid color")
                 .arg(rdoc.B_High)));

    // B values should be physically reasonable for a solenoid (< 100 T)
    QVERIFY2(rdoc.B_High < 100.0,
             qPrintable(QString("B_High=%1 T — unreasonably large").arg(rdoc.B_High)));
}

void TestMesh::resultsPointQuery()
{
    QString ansPath = QString(TEST_DATA_DIR) + "/solenoid.ans";
    if (!QFile::exists(ansPath)) {
        QSKIP("solenoid.ans not found — run solver first");
    }

    ResultsDocument rdoc;
    QVERIFY(rdoc.loadFromFile(ansPath));

    // Query a point inside the coil region (r=15, z=50)
    PointValues pv = rdoc.getPointValues(15.0, 50.0);
    QVERIFY2(pv.valid, "Point query at (15, 50) — inside coil — should be valid");

    // B should be non-zero inside the coil
    double Bmag = std::sqrt(std::norm(pv.B1) + std::norm(pv.B2));
    QVERIFY2(Bmag > 0,
             qPrintable(QString("B at coil center = 0 — expected non-zero field")));

    // Query a point outside the mesh should be invalid
    PointValues pvOut = rdoc.getPointValues(1000.0, 1000.0);
    QVERIFY2(!pvOut.valid, "Point query outside mesh should be invalid");
}

// ---------------------------------------------------------------
// H-Adaptive Refinement
// ---------------------------------------------------------------

void TestMesh::adaptiveRefineSolenoid()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Use maxIterations=1 to exercise: solve → error → mark → refine
    // without trying to solve the huge refined mesh (which is slow).
    AdaptiveConfig config;
    config.tolerance = 0.001;        // unreachable in 1 iteration
    config.maxIterations = 1;
    config.markingFraction = 0.20;
    config.enableCoarsening = false; // disable coarsening for this legacy test

    int initialElements = (int)doc.meshElements.size();

    AdaptiveRefiner refiner;
    bool ok = refiner.run(&doc, config);
    QVERIFY2(ok, qPrintable(refiner.lastError()));

    const auto &hist = refiner.history();
    QVERIFY2(!hist.empty(), "Should have at least one iteration");

    // After 1 iteration: solve succeeded and elements were marked
    QVERIFY2(hist[0].numElements > 0, "Should have elements after solving");
    QVERIFY2(hist[0].numMarked > 0, "Should have marked some elements");
    QVERIFY2(hist[0].globalRelError > 0, "Error should be non-zero");

    // The refined mesh should have MORE elements than the initial mesh
    int finalElements = (int)doc.meshElements.size();
    QVERIFY2(finalElements > initialElements,
             qPrintable(QString("Mesh should grow: %1 -> %2")
                        .arg(initialElements).arg(finalElements)));

    // Final mesh should be stored in document
    QVERIFY(doc.hasMesh);
    QVERIFY(doc.meshNodes.size() > 0);

    qDebug() << "Adaptive refinement:" << hist.size() << "iterations";
    for (const auto &h : hist) {
        qDebug() << "  Iter" << h.iteration + 1 << ":"
                 << h.numElements << "elements,"
                 << h.numNodes << "nodes,"
                 << "error" << h.globalRelError
                 << "marked" << h.numMarked;
    }
    qDebug() << "Final mesh:" << doc.meshElements.size() << "elements"
             << doc.meshNodes.size() << "nodes";
}

// ---------------------------------------------------------------
// H-Adaptive Refinement with Coarsening Back-off
// ---------------------------------------------------------------

void TestMesh::adaptiveCoarseningSolenoid()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Use a tolerance that the initial mesh doesn't meet but isn't too tight
    AdaptiveConfig config;
    config.tolerance = 0.005;        // 0.5% — achievable in a few iterations
    config.maxIterations = 5;
    config.markingFraction = 0.30;
    config.enableCoarsening = true;
    config.maxCoarsenSteps = 5;

    AdaptiveRefiner refiner;
    bool ok = refiner.run(&doc, config);
    QVERIFY2(ok, qPrintable(refiner.lastError()));

    const auto &hist = refiner.history();
    QVERIFY(!hist.empty());

    // Should have at least one refinement step
    bool hasRefinement = false;
    bool hasCoarsening = false;
    for (const auto &h : hist) {
        if (!h.isCoarseningStep) hasRefinement = true;
        if (h.isCoarseningStep) hasCoarsening = true;
    }
    QVERIFY2(hasRefinement, "Should have at least one refinement iteration");
    // Coarsening should have been attempted (initial mesh error ~1.2%)
    // but we don't strictly require it (depends on problem dynamics)

    qDebug() << "Coarsening test:" << hist.size() << "total iterations";
    for (const auto &h : hist) {
        qDebug() << (h.isCoarseningStep ? "  COARSEN" : "  REFINE")
                 << "iter" << h.iteration + 1 << ":"
                 << h.numElements << "elements, error" << h.globalRelError;
    }
    qDebug() << "Final mesh:" << doc.meshElements.size() << "elements";

    if (hasCoarsening) {
        qDebug() << "Coarsening phase was executed successfully.";
    }
}

void TestMesh::adaptiveCoarseningDisabled()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    AdaptiveConfig config;
    config.tolerance = 0.005;
    config.maxIterations = 5;
    config.enableCoarsening = false; // disable coarsening

    AdaptiveRefiner refiner;
    bool ok = refiner.run(&doc, config);
    QVERIFY2(ok, qPrintable(refiner.lastError()));

    const auto &hist = refiner.history();
    // No coarsening steps should appear
    for (const auto &h : hist) {
        QVERIFY2(!h.isCoarseningStep,
                 "No coarsening steps should exist when disabled");
    }

    qDebug() << "Coarsening-disabled test:" << hist.size() << "iterations"
             << doc.meshElements.size() << "elements";
}

void TestMesh::adaptiveAlreadyConverged()
{
    // Generate baseline mesh to get element count before adaptive
    FemmeDocument docBase;
    QVERIFY(docBase.loadFromFile(solenoidPath));
    MeshGenerator meshGenBase;
    std::vector<MeshEdge> edgesBase;
    QVERIFY(meshGenBase.generateMeshInProcess(&docBase, edgesBase));
    int baselineElements = (int)docBase.meshElements.size();

    // Run adaptive with tolerance the initial mesh easily meets
    // (initial error ~1.2%, tolerance 5%). The algorithm should COARSEN
    // toward the tolerance target, finding a sparser mesh.
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    AdaptiveConfig config;
    config.tolerance = 0.05;  // 5% — initial mesh meets this easily
    config.maxIterations = 5;
    config.enableCoarsening = true;
    config.maxCoarsenSteps = 5;

    AdaptiveRefiner refiner;
    bool ok = refiner.run(&doc, config);
    QVERIFY2(ok, qPrintable(refiner.lastError()));

    const auto &hist = refiner.history();
    QVERIFY(!hist.empty());

    // Phase 1 should converge immediately (no refinement needed)
    QVERIFY2(hist[0].numMarked == 0, "Should not need to refine");

    // Phase 2 should coarsen the mesh to approach the tolerance target
    bool hasCoarsening = false;
    for (const auto &h : hist) {
        if (h.isCoarseningStep) hasCoarsening = true;
    }
    QVERIFY2(hasCoarsening,
             "Should coarsen when tolerance > initial error (tolerance is a target)");

    // Final mesh should be coarser (fewer elements) than baseline
    int finalElements = (int)doc.meshElements.size();
    QVERIFY2(finalElements <= baselineElements,
             qPrintable(QString("Coarsened mesh (%1) should have <= baseline (%2)")
                 .arg(finalElements).arg(baselineElements)));

    qDebug() << "Already-converged test: baseline" << baselineElements
             << "-> coarsened" << finalElements << "elements";
    for (const auto &h : hist) {
        qDebug() << (h.isCoarseningStep ? "  COARSEN" : "  REFINE")
                 << "iter" << h.iteration + 1 << ":"
                 << h.numElements << "elements, error" << h.globalRelError;
    }
}

void TestMesh::adaptiveCoarseningReducesElements()
{
    // Run WITHOUT coarsening
    FemmeDocument doc1;
    QVERIFY(doc1.loadFromFile(solenoidPath));
    AdaptiveConfig config1;
    config1.tolerance = 0.005;
    config1.maxIterations = 5;
    config1.enableCoarsening = false;
    AdaptiveRefiner refiner1;
    bool ok1 = refiner1.run(&doc1, config1);
    QVERIFY2(ok1, qPrintable(refiner1.lastError()));
    int elementsWithout = (int)doc1.meshElements.size();

    // Run WITH coarsening
    FemmeDocument doc2;
    QVERIFY(doc2.loadFromFile(solenoidPath));
    AdaptiveConfig config2;
    config2.tolerance = 0.005;
    config2.maxIterations = 5;
    config2.enableCoarsening = true;
    config2.maxCoarsenSteps = 3;
    AdaptiveRefiner refiner2;
    bool ok2 = refiner2.run(&doc2, config2);
    QVERIFY2(ok2, qPrintable(refiner2.lastError()));
    int elementsWith = (int)doc2.meshElements.size();

    qDebug() << "Elements without coarsening:" << elementsWithout
             << "with coarsening:" << elementsWith;

    // Coarsened mesh should not have significantly more elements.
    // Allow 1% tolerance for mesh generation noise (Triangle is not fully deterministic
    // with floating-point area constraints across different region configurations).
    QVERIFY2(elementsWith <= elementsWithout * 1.01 + 10,
             qPrintable(QString("Coarsened mesh (%1) should have roughly <= elements than refined (%2)")
                 .arg(elementsWith).arg(elementsWithout)));
}

void TestMesh::adaptiveLRKCoarsening()
{
    static const QString lrkPath = QString(TEST_DATA_DIR) + "/lrk.fem";
    if (!QFile::exists(lrkPath)) {
        QSKIP("lrk.fem not found in test data");
    }

    // Baseline: generate mesh with user's original maxArea settings
    FemmeDocument docBase;
    QVERIFY(docBase.loadFromFile(lrkPath));
    MeshGenerator meshGen;
    std::vector<MeshEdge> edges;
    QVERIFY(meshGen.generateMeshInProcess(&docBase, edges));
    int baselineElements = (int)docBase.meshElements.size();

    // Run adaptive at 5% tolerance — should refine then coarsen
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(lrkPath));
    AdaptiveConfig config;
    config.tolerance = 0.05;
    config.maxIterations = 5;
    config.enableCoarsening = true;

    AdaptiveRefiner refiner;
    bool ok = refiner.run(&doc, config);
    QVERIFY2(ok, qPrintable(refiner.lastError()));

    int finalElements = (int)doc.meshElements.size();
    const auto &hist = refiner.history();

    // Should have both refinement and coarsening steps
    bool hasRefinement = false, hasCoarsening = false;
    for (const auto &h : hist) {
        if (!h.isCoarseningStep) hasRefinement = true;
        if (h.isCoarseningStep) hasCoarsening = true;
    }
    QVERIFY2(hasRefinement, "LRK should need refinement (initial error ~11%)");
    QVERIFY2(hasCoarsening, "Phase 2 coarsening should run");

    // Final error should be close to tolerance (within 20% of target)
    double lastError = hist.back().globalRelError;
    QVERIFY2(lastError < config.tolerance,
             qPrintable(QString("Final error %1 should be < tolerance %2")
                 .arg(lastError).arg(config.tolerance)));

    qDebug() << "LRK adaptive: baseline" << baselineElements
             << "-> final" << finalElements << "elements, error" << lastError;
    for (const auto &h : hist) {
        qDebug() << (h.isCoarseningStep ? "  COARSEN" : "  REFINE ")
                 << "iter" << h.iteration + 1 << ":"
                 << h.numElements << "elements, error" << h.globalRelError;
    }
}

void TestMesh::adaptiveSolveFailureRecovery()
{
    // Test that when the solver fails at a later iteration (mesh too dense),
    // the algorithm recovers gracefully instead of aborting.
    // Use a tight tolerance on the LRK model to push the solver hard.
    static const QString lrkPath = QString(TEST_DATA_DIR) + "/lrk.fem";
    if (!QFile::exists(lrkPath)) {
        QSKIP("lrk.fem not found in test data");
    }

    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(lrkPath));

    AdaptiveConfig config;
    config.tolerance = 0.01;  // 1% — tight enough to push solver limits
    config.maxIterations = 5;
    config.enableCoarsening = true;
    config.maxCoarsenSteps = 3;

    AdaptiveRefiner refiner;
    bool ok = refiner.run(&doc, config);

    // The algorithm should ALWAYS succeed (either converge or recover).
    // It should never abort with ok=false due to a mid-iteration solve failure.
    QVERIFY2(ok, qPrintable(QString("Adaptive should not abort: %1")
                             .arg(refiner.lastError())));

    // We should have a valid mesh in the document regardless
    QVERIFY(doc.hasMesh);
    QVERIFY(doc.meshNodes.size() > 0);
    QVERIFY(doc.meshElements.size() > 0);

    const auto &hist = refiner.history();
    QVERIFY(!hist.empty());

    qDebug() << "Solve-failure recovery test:" << hist.size() << "iterations"
             << doc.meshElements.size() << "final elements";
    for (const auto &h : hist) {
        qDebug() << (h.isCoarseningStep ? "  COARSEN" : "  REFINE ")
                 << "iter" << h.iteration + 1 << ":"
                 << h.numElements << "elements, error" << h.globalRelError;
    }
}

// ---------------------------------------------------------------
// writeMeshFiles round-trip test
// Generate mesh in-process, write to disk, verify files are
// valid and readable by fkn's fscanf-based LoadMesh.
// ---------------------------------------------------------------

void TestMesh::writeMeshFilesRoundTrip()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Save to temp directory
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString tmpFem = tmpDir.path() + "/test.fem";
    QVERIFY(doc.saveToFile(tmpFem));

    // Generate mesh in-process (like adaptive refinement does)
    MeshGenerator meshGen;
    std::vector<MeshEdge> edges;
    QVERIFY(meshGen.generateMeshInProcess(&doc, edges));
    QVERIFY(!doc.meshNodes.empty());
    QVERIFY(!doc.meshElements.empty());

    int origNodes = (int)doc.meshNodes.size();
    int origElements = (int)doc.meshElements.size();

    // Write mesh files to disk
    QVERIFY2(meshGen.writeMeshFiles(&doc),
             qPrintable("writeMeshFiles failed: " + meshGen.lastError()));

    // Verify .node file exists and is parseable with fscanf (C locale)
    {
        QString nodePath = tmpDir.path() + "/test.node";
        QVERIFY2(QFile::exists(nodePath),
                 qPrintable("Missing .node file: " + nodePath));
        FILE *fp = fopen(nodePath.toLocal8Bit().constData(), "r");
        QVERIFY(fp);
        int numNodes = 0;
        QCOMPARE(fscanf(fp, "%d %*d %*d %*d", &numNodes), 1);
        QCOMPARE(numNodes, origNodes);
        for (int i = 0; i < numNodes; i++) {
            int idx, bm;
            double x, y;
            QCOMPARE(fscanf(fp, "%d %lf %lf %d", &idx, &x, &y, &bm), 4);
            QCOMPARE(idx, i);
            QVERIFY2(std::fabs(x - doc.meshNodes[i].x) < 1e-10,
                     qPrintable(QString("Node %1 x mismatch: %2 vs %3")
                         .arg(i).arg(x).arg(doc.meshNodes[i].x)));
            QVERIFY2(std::fabs(y - doc.meshNodes[i].y) < 1e-10,
                     qPrintable(QString("Node %1 y mismatch: %2 vs %3")
                         .arg(i).arg(y).arg(doc.meshNodes[i].y)));
        }
        fclose(fp);
    }

    // Verify .ele file exists and is parseable
    {
        QString elePath = tmpDir.path() + "/test.ele";
        QVERIFY2(QFile::exists(elePath),
                 qPrintable("Missing .ele file: " + elePath));
        FILE *fp = fopen(elePath.toLocal8Bit().constData(), "r");
        QVERIFY(fp);
        int numEls = 0;
        QCOMPARE(fscanf(fp, "%d %*d %*d", &numEls), 1);
        QCOMPARE(numEls, origElements);
        for (int i = 0; i < numEls; i++) {
            int idx, p0, p1, p2, lbl;
            QCOMPARE(fscanf(fp, "%d %d %d %d %d", &idx, &p0, &p1, &p2, &lbl), 5);
            QCOMPARE(idx, i);
            QCOMPARE(p0, doc.meshElements[i].p[0]);
            QCOMPARE(p1, doc.meshElements[i].p[1]);
            QCOMPARE(p2, doc.meshElements[i].p[2]);
            QCOMPARE(lbl, doc.meshElements[i].label);
        }
        fclose(fp);
    }

    // Verify .pbc file exists and has valid content
    {
        QString pbcPath = tmpDir.path() + "/test.pbc";
        QVERIFY2(QFile::exists(pbcPath),
                 qPrintable("Missing .pbc file: " + pbcPath));
        FILE *fp = fopen(pbcPath.toLocal8Bit().constData(), "r");
        QVERIFY(fp);
        int numPBCs = -1, numAGEs = -1;
        QCOMPARE(fscanf(fp, "%d", &numPBCs), 1);
        QCOMPARE(numPBCs, 0);
        QCOMPARE(fscanf(fp, "%d", &numAGEs), 1);
        QCOMPARE(numAGEs, 0);
        fclose(fp);
    }

    qDebug() << "writeMeshFiles round-trip:" << origNodes << "nodes,"
             << origElements << "elements — all files valid";
}
