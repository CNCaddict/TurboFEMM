// FEMM Unit Tests — Geometry Operations Tests
#include "test_geometry.h"
#include "document.h"
#include "femm_types.h"

#include <cmath>

// ---------------------------------------------------------------
// Node operations
// ---------------------------------------------------------------

void TestGeometry::addNode()
{
    FemmeDocument doc;
    QVERIFY(doc.addNode(1.0, 2.0, 1e-6));
    QCOMPARE((int)doc.nodes.size(), 1);
    QCOMPARE(doc.nodes[0].x, 1.0);
    QCOMPARE(doc.nodes[0].y, 2.0);
    QVERIFY(doc.isModified);
}

void TestGeometry::addNodeDuplicate()
{
    FemmeDocument doc;
    QVERIFY(doc.addNode(1.0, 2.0, 1e-6));
    // Adding at exact same position should fail
    QVERIFY(!doc.addNode(1.0, 2.0, 1e-6));
    QCOMPARE((int)doc.nodes.size(), 1);
}

void TestGeometry::addNodeTolerance()
{
    FemmeDocument doc;
    QVERIFY(doc.addNode(1.0, 2.0, 0.01));
    // Inside tolerance — should be rejected
    QVERIFY(!doc.addNode(1.005, 2.005, 0.01));
    QCOMPARE((int)doc.nodes.size(), 1);

    // Outside tolerance — should succeed
    QVERIFY(doc.addNode(1.02, 2.02, 0.01));
    QCOMPARE((int)doc.nodes.size(), 2);
}

// ---------------------------------------------------------------
// Segment operations
// ---------------------------------------------------------------

void TestGeometry::addSegment()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    doc.addNode(1, 0, 1e-6);
    QVERIFY(doc.addSegment(0, 1));
    QCOMPARE((int)doc.segments.size(), 1);
    QCOMPARE(doc.segments[0].n0, 0);
    QCOMPARE(doc.segments[0].n1, 1);
}

void TestGeometry::addSegmentDuplicate()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    doc.addNode(1, 0, 1e-6);
    QVERIFY(doc.addSegment(0, 1));
    // Same segment (same direction)
    QVERIFY(!doc.addSegment(0, 1));
    // Same segment (reversed direction)
    QVERIFY(!doc.addSegment(1, 0));
    QCOMPARE((int)doc.segments.size(), 1);
}

void TestGeometry::addSegmentSameNode()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    QVERIFY(!doc.addSegment(0, 0));
    QCOMPARE((int)doc.segments.size(), 0);
}

void TestGeometry::addSegmentOutOfRange()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    QVERIFY(!doc.addSegment(0, 5));   // node 5 doesn't exist
    QVERIFY(!doc.addSegment(-1, 0));  // negative index
    QCOMPARE((int)doc.segments.size(), 0);
}

// ---------------------------------------------------------------
// Arc segment operations
// ---------------------------------------------------------------

void TestGeometry::addArcSegment()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    doc.addNode(1, 0, 1e-6);
    QVERIFY(doc.addArcSegment(0, 1, 90.0, 10.0));
    QCOMPARE((int)doc.arcSegments.size(), 1);
    QCOMPARE(doc.arcSegments[0].arcLength, 90.0);
    QCOMPARE(doc.arcSegments[0].maxSideLength, 10.0);
}

void TestGeometry::addArcSegmentSameNode()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    QVERIFY(!doc.addArcSegment(0, 0, 90.0, 10.0));
}

void TestGeometry::addArcSegmentOutOfRange()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    QVERIFY(!doc.addArcSegment(0, 99, 90.0, 10.0));
    QVERIFY(!doc.addArcSegment(-1, 0, 90.0, 10.0));
}

// ---------------------------------------------------------------
// Block label operations
// ---------------------------------------------------------------

void TestGeometry::addBlockLabel()
{
    FemmeDocument doc;
    QVERIFY(doc.addBlockLabel(5.0, 5.0, 1e-6));
    QCOMPARE((int)doc.blockLabels.size(), 1);
    QCOMPARE(doc.blockLabels[0].x, 5.0);
    QCOMPARE(doc.blockLabels[0].y, 5.0);
}

void TestGeometry::addBlockLabelDuplicate()
{
    FemmeDocument doc;
    QVERIFY(doc.addBlockLabel(5.0, 5.0, 0.01));
    QVERIFY(!doc.addBlockLabel(5.005, 5.005, 0.01));
    QCOMPARE((int)doc.blockLabels.size(), 1);
}

// ---------------------------------------------------------------
// Closest item queries
// ---------------------------------------------------------------

void TestGeometry::closestNode()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    doc.addNode(10, 0, 1e-6);
    doc.addNode(10, 10, 1e-6);

    QCOMPARE(doc.closestNode(0.1, 0.1), 0);
    QCOMPARE(doc.closestNode(9.9, 0.1), 1);
    QCOMPARE(doc.closestNode(9.9, 9.9), 2);
}

void TestGeometry::closestSegment()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);   // 0
    doc.addNode(10, 0, 1e-6);  // 1
    doc.addNode(10, 10, 1e-6); // 2
    doc.addSegment(0, 1);  // seg 0: midpoint at (5, 0)
    doc.addSegment(1, 2);  // seg 1: midpoint at (10, 5)

    QCOMPARE(doc.closestSegment(4.0, 0.5), 0);
    QCOMPARE(doc.closestSegment(9.5, 5.0), 1);
}

void TestGeometry::closestBlockLabel()
{
    FemmeDocument doc;
    doc.addBlockLabel(1.0, 1.0, 1e-6);
    doc.addBlockLabel(5.0, 5.0, 1e-6);

    QCOMPARE(doc.closestBlockLabel(1.1, 1.1), 0);
    QCOMPARE(doc.closestBlockLabel(4.9, 4.9), 1);
}

// ---------------------------------------------------------------
// Bounding box
// ---------------------------------------------------------------

void TestGeometry::boundingBoxEmpty()
{
    FemmeDocument doc;
    double xmin, ymin, xmax, ymax;
    QVERIFY(!doc.getBoundingBox(xmin, ymin, xmax, ymax));
}

void TestGeometry::boundingBoxSingle()
{
    FemmeDocument doc;
    doc.addNode(5.0, 5.0, 1e-6);
    double xmin, ymin, xmax, ymax;
    QVERIFY(doc.getBoundingBox(xmin, ymin, xmax, ymax));
    // Single point should get default padding (±1.0)
    QVERIFY(xmin < 5.0);
    QVERIFY(xmax > 5.0);
    QVERIFY(ymin < 5.0);
    QVERIFY(ymax > 5.0);
}

void TestGeometry::boundingBoxMultiple()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    doc.addNode(10, 20, 1e-6);
    double xmin, ymin, xmax, ymax;
    QVERIFY(doc.getBoundingBox(xmin, ymin, xmax, ymax));
    // Bounding box should contain all nodes (with some padding)
    QVERIFY(xmin <= 0.0);
    QVERIFY(xmax >= 10.0);
    QVERIFY(ymin <= 0.0);
    QVERIFY(ymax >= 20.0);
}

// ---------------------------------------------------------------
// Delete operations
// ---------------------------------------------------------------

void TestGeometry::deleteNode()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    doc.addNode(5, 5, 1e-6);
    QCOMPARE((int)doc.nodes.size(), 2);

    doc.nodes[0].isSelected = true;
    int removed = doc.deleteSelectedNodes();
    QCOMPARE(removed, 1);
    QCOMPARE((int)doc.nodes.size(), 1);
    QCOMPARE(doc.nodes[0].x, 5.0);
    QCOMPARE(doc.nodes[0].y, 5.0);
}

void TestGeometry::deleteNodeCascadesSegments()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);  // 0
    doc.addNode(5, 0, 1e-6);  // 1
    doc.addNode(5, 5, 1e-6);  // 2
    doc.addSegment(0, 1);
    doc.addSegment(1, 2);
    doc.addArcSegment(0, 2, 90.0, 10.0);
    QCOMPARE((int)doc.segments.size(), 2);
    QCOMPARE((int)doc.arcSegments.size(), 1);

    // Delete node 0 — should cascade to segments/arcs referencing it
    doc.nodes[0].isSelected = true;
    doc.deleteSelectedNodes();

    QCOMPARE((int)doc.nodes.size(), 2);
    // Segment 0->1 should be removed; segment 1->2 should survive
    // but indices get decremented so it becomes 0->1
    QCOMPARE((int)doc.segments.size(), 1);
    QCOMPARE(doc.segments[0].n0, 0);  // was node 1, now 0
    QCOMPARE(doc.segments[0].n1, 1);  // was node 2, now 1

    // Arc 0->2 should be removed (referenced node 0)
    QCOMPARE((int)doc.arcSegments.size(), 0);
}

void TestGeometry::deleteSegment()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    doc.addNode(1, 0, 1e-6);
    doc.addNode(1, 1, 1e-6);
    doc.addSegment(0, 1);
    doc.addSegment(1, 2);
    QCOMPARE((int)doc.segments.size(), 2);

    doc.segments[0].isSelected = true;
    int removed = doc.deleteSelectedSegments();
    QCOMPARE(removed, 1);
    QCOMPARE((int)doc.segments.size(), 1);
    // Nodes should remain untouched
    QCOMPARE((int)doc.nodes.size(), 3);
}

void TestGeometry::deleteBlockLabel()
{
    FemmeDocument doc;
    doc.addBlockLabel(1, 1, 1e-6);
    doc.addBlockLabel(5, 5, 1e-6);
    QCOMPARE((int)doc.blockLabels.size(), 2);

    doc.blockLabels[0].isSelected = true;
    int removed = doc.deleteSelectedBlockLabels();
    QCOMPARE(removed, 1);
    QCOMPARE((int)doc.blockLabels.size(), 1);
    QCOMPARE(doc.blockLabels[0].x, 5.0);
}

void TestGeometry::deleteSelected()
{
    FemmeDocument doc;
    doc.addNode(0, 0, 1e-6);
    doc.addNode(1, 0, 1e-6);
    doc.addNode(1, 1, 1e-6);
    doc.addSegment(0, 1);
    doc.addSegment(1, 2);
    doc.addBlockLabel(0.5, 0.5, 1e-6);

    // Select one of each type
    doc.nodes[2].isSelected = true;
    doc.segments[0].isSelected = true;
    doc.blockLabels[0].isSelected = true;

    int removed = doc.deleteSelected();
    QVERIFY(removed >= 3);
    // Should be: 1 block label + 1 segment + 1 node (+ cascade seg 1->2)
}

// ---------------------------------------------------------------
// Property assignment
// ---------------------------------------------------------------

void TestGeometry::materialPropertyAssignment()
{
    FemmeDocument doc;

    // Add materials
    FMaterialProp mat1;
    mat1.blockName = "Iron";
    mat1.mu_x = 5000.0;
    mat1.mu_y = 5000.0;
    doc.materialProps.push_back(mat1);

    FMaterialProp mat2;
    mat2.blockName = "Air";
    mat2.mu_x = 1.0;
    mat2.mu_y = 1.0;
    doc.materialProps.push_back(mat2);

    // Find by name
    QCOMPARE(doc.findMaterialPropIndex("Iron"), 0);
    QCOMPARE(doc.findMaterialPropIndex("Air"), 1);
    QCOMPARE(doc.findMaterialPropIndex("NonExistent"), -1);

    // Assign to block label
    doc.addBlockLabel(1.0, 1.0, 1e-6);
    doc.blockLabels[0].blockType = "Iron";

    int idx = doc.findMaterialPropIndex(doc.blockLabels[0].blockType);
    QCOMPARE(idx, 0);
    QCOMPARE(doc.materialProps[idx].mu_x, 5000.0);
}

void TestGeometry::boundaryPropertyAssignment()
{
    FemmeDocument doc;

    FBoundaryProp bp;
    bp.bdryName = "A=0";
    bp.bdryFormat = 0;  // prescribed A
    bp.A0 = 0.0;
    doc.boundaryProps.push_back(bp);

    QCOMPARE(doc.findBoundaryPropIndex("A=0"), 0);
    QCOMPARE(doc.findBoundaryPropIndex("Missing"), -1);

    // Assign to segment
    doc.addNode(0, 0, 1e-6);
    doc.addNode(1, 0, 1e-6);
    doc.addSegment(0, 1);
    doc.segments[0].boundaryMarker = "A=0";

    int idx = doc.findBoundaryPropIndex(doc.segments[0].boundaryMarker);
    QCOMPARE(idx, 0);
}

void TestGeometry::circuitPropertyAssignment()
{
    FemmeDocument doc;

    FCircuit circ;
    circ.circName = "Coil";
    circ.amps = FemmComplex(10.0, 0.0);
    circ.circType = 1;
    doc.circuitProps.push_back(circ);

    QCOMPARE(doc.findCircuitPropIndex("Coil"), 0);
    QCOMPARE(doc.findCircuitPropIndex("Unknown"), -1);

    // Assign to block label
    doc.addBlockLabel(1.0, 1.0, 1e-6);
    doc.blockLabels[0].inCircuit = "Coil";

    int idx = doc.findCircuitPropIndex(doc.blockLabels[0].inCircuit);
    QCOMPARE(idx, 0);
    QCOMPARE(doc.circuitProps[idx].amps.re, 10.0);
}
