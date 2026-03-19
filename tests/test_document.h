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

    // Steinmetz iron loss serialization
    void steinmetzRoundTrip();
    void blockLabelLossRoundTrip();
    void backwardCompatNoLossFields();

    // B-field history spatial index
    void bHistoryIndexLookup();
    void bHistoryIndexEmpty();

    // Iron loss computation
    void steinmetzLossFormula();
    void ironLossFromBHistory();
    void rotorLossInverseTransform();
    void rotorLossLabelAwareLookup();
    void conductiveEddyLossAutoCompute();
    void ferromagneticSolidAutoThicknessLoss();
    void rotorSynchronousFieldRemoved();
    void rotorBackironAnnularProfileBias();
    void accurateRotorBackironDiffusionMode();
    void azBasedSolidConductorLoss();
    void azBasedSolidConductorLossSeparateBlocks();
    void steinmetzM19at60Hz();

    // 3-phase commutation
    void threePhaseCommutationTracking();
};

#endif // TEST_DOCUMENT_H
