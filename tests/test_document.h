// FEMM Unit Tests — Document I/O Tests
#ifndef TEST_DOCUMENT_H
#define TEST_DOCUMENT_H

#include <QObject>
#include <QTest>

class TestDocument : public QObject
{
    Q_OBJECT

private slots:
    // .fem file loading
    void loadSolenoid();
    void loadSolenoidCheckNodes();
    void loadSolenoidCheckSegments();
    void loadSolenoidCheckBlockLabels();
    void loadSolenoidCheckMaterials();
    void loadSolenoidCheckBoundaries();
    void loadSolenoidCheckCircuits();
    void loadSolenoidCheckProblemDef();

    // .fem file round-trip (load → save → reload → compare)
    void roundTripSave();

    // LRK file round-trip — compares saved file content with original
    void lrkRoundTripFileDiff();

    // Non-existent file
    void loadNonExistent();

    // Property lookup
    void propertyIndexLookup();
};

#endif // TEST_DOCUMENT_H
