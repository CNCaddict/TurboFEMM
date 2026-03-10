// FEMM Unit Tests — Geometry Operations Tests
#ifndef TEST_GEOMETRY_H
#define TEST_GEOMETRY_H

#include <QObject>
#include <QTest>

class TestGeometry : public QObject
{
    Q_OBJECT

private slots:
    // Node operations
    void addNode();
    void addNodeDuplicate();
    void addNodeTolerance();

    // Segment operations
    void addSegment();
    void addSegmentDuplicate();
    void addSegmentSameNode();
    void addSegmentOutOfRange();

    // Arc segment operations
    void addArcSegment();
    void addArcSegmentSameNode();
    void addArcSegmentOutOfRange();

    // Block label operations
    void addBlockLabel();
    void addBlockLabelDuplicate();

    // Closest item queries
    void closestNode();
    void closestSegment();
    void closestBlockLabel();

    // Bounding box
    void boundingBoxEmpty();
    void boundingBoxSingle();
    void boundingBoxMultiple();

    // Delete operations
    void deleteNode();
    void deleteNodeCascadesSegments();
    void deleteSegment();
    void deleteBlockLabel();
    void deleteSelected();

    // Property assignment
    void materialPropertyAssignment();
    void boundaryPropertyAssignment();
    void circuitPropertyAssignment();
};

#endif // TEST_GEOMETRY_H
