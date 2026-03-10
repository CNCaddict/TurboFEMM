// FEMM Unit Tests — Document I/O Tests
#include "test_document.h"
#include "document.h"
#include "femm_types.h"

#include <QTemporaryFile>
#include <QFile>
#include <cmath>

static const QString solenoidPath = QString(TEST_DATA_DIR) + "/solenoid.fem";

// ---------------------------------------------------------------
// Loading tests
// ---------------------------------------------------------------

void TestDocument::loadSolenoid()
{
    FemmeDocument doc;
    QVERIFY2(QFile::exists(solenoidPath),
             qPrintable("Test file not found: " + solenoidPath));
    QVERIFY(doc.loadFromFile(solenoidPath));
}

void TestDocument::loadSolenoidCheckNodes()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Solenoid should have nodes
    QVERIFY(doc.nodes.size() > 0);

    // Verify node coordinates are reasonable (solenoid is typically small)
    for (const auto &nd : doc.nodes) {
        QVERIFY2(!std::isnan(nd.x) && !std::isnan(nd.y),
                 "Node has NaN coordinates");
        QVERIFY2(std::fabs(nd.x) < 1000 && std::fabs(nd.y) < 1000,
                 "Node coordinates unreasonably large");
    }
}

void TestDocument::loadSolenoidCheckSegments()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    QVERIFY(doc.segments.size() > 0);

    // All segment node indices should be valid
    for (const auto &seg : doc.segments) {
        QVERIFY2(seg.n0 >= 0 && seg.n0 < (int)doc.nodes.size(),
                 qPrintable(QString("Segment n0=%1 out of range [0,%2)")
                     .arg(seg.n0).arg(doc.nodes.size())));
        QVERIFY2(seg.n1 >= 0 && seg.n1 < (int)doc.nodes.size(),
                 qPrintable(QString("Segment n1=%1 out of range [0,%2)")
                     .arg(seg.n1).arg(doc.nodes.size())));
        QVERIFY2(seg.n0 != seg.n1, "Degenerate segment (same start/end node)");
    }
}

void TestDocument::loadSolenoidCheckBlockLabels()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    QVERIFY(doc.blockLabels.size() > 0);

    // Each block label should reference a valid material or <None>
    for (const auto &bl : doc.blockLabels) {
        if (bl.blockType != "<None>") {
            int idx = doc.findMaterialPropIndex(bl.blockType);
            QVERIFY2(idx >= 0,
                     qPrintable(QString("Block label references unknown material: %1")
                         .arg(bl.blockType)));
        }
    }
}

void TestDocument::loadSolenoidCheckMaterials()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Solenoid should have at least Air, Coil, Iron
    QVERIFY2(doc.materialProps.size() >= 3,
             qPrintable(QString("Expected >= 3 materials, got %1")
                 .arg(doc.materialProps.size())));

    // Check material names are non-empty
    for (const auto &mp : doc.materialProps) {
        QVERIFY2(!mp.blockName.isEmpty(), "Material has empty name");
    }

    // Check permeabilities are positive
    for (const auto &mp : doc.materialProps) {
        QVERIFY2(mp.mu_x > 0, qPrintable(QString("Material %1 has mu_x=%2")
            .arg(mp.blockName).arg(mp.mu_x)));
        QVERIFY2(mp.mu_y > 0, qPrintable(QString("Material %1 has mu_y=%2")
            .arg(mp.blockName).arg(mp.mu_y)));
    }
}

void TestDocument::loadSolenoidCheckBoundaries()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Should have at least one boundary condition (A=0)
    QVERIFY(doc.boundaryProps.size() >= 1);

    for (const auto &bp : doc.boundaryProps) {
        QVERIFY2(!bp.bdryName.isEmpty(), "Boundary has empty name");
        QVERIFY2(bp.bdryFormat >= 0 && bp.bdryFormat <= 7,
                 qPrintable(QString("Invalid boundary format: %1").arg(bp.bdryFormat)));
    }
}

void TestDocument::loadSolenoidCheckCircuits()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Solenoid should have a "Coil" circuit
    QVERIFY(doc.circuitProps.size() >= 1);

    bool foundCoil = false;
    for (const auto &cp : doc.circuitProps) {
        QVERIFY2(!cp.circName.isEmpty(), "Circuit has empty name");
        if (cp.circName.contains("Coil", Qt::CaseInsensitive))
            foundCoil = true;
    }
    QVERIFY2(foundCoil, "Expected a coil circuit in solenoid problem");
}

void TestDocument::loadSolenoidCheckProblemDef()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Solenoid is planar
    QCOMPARE(doc.problemType, ProblemType::Planar);

    // Precision should be reasonable
    QVERIFY2(doc.precision > 0 && doc.precision < 1.0,
             qPrintable(QString("Unreasonable precision: %1").arg(doc.precision)));

    // Frequency should be >= 0
    QVERIFY(doc.frequency >= 0.0);
}

// ---------------------------------------------------------------
// Round-trip save test
// ---------------------------------------------------------------

void TestDocument::roundTripSave()
{
    // Load the solenoid
    FemmeDocument doc1;
    QVERIFY(doc1.loadFromFile(solenoidPath));

    // Save to temp file
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(QDir::tempPath() + "/femm_test_XXXXXX.fem");
    QVERIFY(tmpFile.open());
    QString tmpPath = tmpFile.fileName();
    tmpFile.close();

    QVERIFY(doc1.saveToFile(tmpPath));

    // Reload from temp file
    FemmeDocument doc2;
    QVERIFY(doc2.loadFromFile(tmpPath));

    // Compare key properties
    QCOMPARE(doc2.nodes.size(), doc1.nodes.size());
    QCOMPARE(doc2.segments.size(), doc1.segments.size());
    QCOMPARE(doc2.arcSegments.size(), doc1.arcSegments.size());
    QCOMPARE(doc2.blockLabels.size(), doc1.blockLabels.size());
    QCOMPARE(doc2.materialProps.size(), doc1.materialProps.size());
    QCOMPARE(doc2.boundaryProps.size(), doc1.boundaryProps.size());
    QCOMPARE(doc2.circuitProps.size(), doc1.circuitProps.size());

    // Compare node positions (should be exact with %.17g precision)
    for (size_t i = 0; i < doc1.nodes.size(); i++) {
        QVERIFY2(std::fabs(doc1.nodes[i].x - doc2.nodes[i].x) < 1e-12,
                 qPrintable(QString("Node %1 x mismatch: %2 vs %3")
                     .arg(i).arg(doc1.nodes[i].x).arg(doc2.nodes[i].x)));
        QVERIFY2(std::fabs(doc1.nodes[i].y - doc2.nodes[i].y) < 1e-12,
                 qPrintable(QString("Node %1 y mismatch: %2 vs %3")
                     .arg(i).arg(doc1.nodes[i].y).arg(doc2.nodes[i].y)));
    }

    // Compare material names
    for (size_t i = 0; i < doc1.materialProps.size(); i++) {
        QCOMPARE(doc2.materialProps[i].blockName, doc1.materialProps[i].blockName);
    }

    // Compare problem definition
    QCOMPARE(doc2.problemType, doc1.problemType);
    QVERIFY(std::fabs(doc2.frequency - doc1.frequency) < 1e-10);
    QVERIFY(std::fabs(doc2.precision - doc1.precision) < 1e-16);

    // Clean up
    QFile::remove(tmpPath);
}

// ---------------------------------------------------------------
// LRK round-trip: save .fem and compare with original file
// ---------------------------------------------------------------

void TestDocument::lrkRoundTripFileDiff()
{
    static const QString lrkPath = QString(TEST_DATA_DIR) + "/lrk.fem";
    QVERIFY2(QFile::exists(lrkPath), "lrk.fem not found");

    // Load the original
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(lrkPath));

    // Save to a temp file
    QString tmpPath = QDir::tempPath() + "/lrk_roundtrip_test.fem";
    QVERIFY(doc.saveToFile(tmpPath));

    // Read both files and compare line by line
    QFile origFile(lrkPath);
    QFile savedFile(tmpPath);
    QVERIFY(origFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(savedFile.open(QIODevice::ReadOnly | QIODevice::Text));

    QTextStream origIn(&origFile);
    QTextStream savedIn(&savedFile);

    int lineNum = 0;
    int diffCount = 0;
    while (!origIn.atEnd() && !savedIn.atEnd()) {
        lineNum++;
        QString origLine = origIn.readLine();
        QString savedLine = savedIn.readLine();
        if (origLine != savedLine) {
            diffCount++;
            if (diffCount <= 30) {
                qWarning("LINE %d DIFFERS:", lineNum);
                qWarning("  ORIG:  %s", qPrintable(origLine));
                qWarning("  SAVED: %s", qPrintable(savedLine));
            }
        }
    }
    // Check for extra lines
    while (!origIn.atEnd()) {
        lineNum++;
        origIn.readLine();
        diffCount++;
        if (diffCount <= 30)
            qWarning("LINE %d: ORIG has extra line", lineNum);
    }
    while (!savedIn.atEnd()) {
        lineNum++;
        savedIn.readLine();
        diffCount++;
        if (diffCount <= 30)
            qWarning("LINE %d: SAVED has extra line", lineNum);
    }
    origFile.close();
    savedFile.close();

    qWarning("Total lines compared: %d, differences: %d", lineNum, diffCount);

    // Now also verify that fkn-critical fields survive the roundtrip.
    // Load the saved file and compare key fields with original doc.
    FemmeDocument doc2;
    QVERIFY(doc2.loadFromFile(tmpPath));

    // Compare material properties (BH curves are critical for solver)
    QCOMPARE((int)doc2.materialProps.size(), (int)doc.materialProps.size());
    for (size_t i = 0; i < doc.materialProps.size(); i++) {
        const auto &m1 = doc.materialProps[i];
        const auto &m2 = doc2.materialProps[i];
        QCOMPARE(m2.blockName, m1.blockName);
        QVERIFY2(std::fabs(m2.mu_x - m1.mu_x) < 1e-10,
                 qPrintable(QString("Material %1 mu_x: %2 vs %3")
                     .arg(m1.blockName).arg(m1.mu_x).arg(m2.mu_x)));
        QVERIFY2(std::fabs(m2.mu_y - m1.mu_y) < 1e-10,
                 qPrintable(QString("Material %1 mu_y: %2 vs %3")
                     .arg(m1.blockName).arg(m1.mu_y).arg(m2.mu_y)));
        QVERIFY2(std::fabs(m2.H_c - m1.H_c) < 1e-6,
                 qPrintable(QString("Material %1 H_c: %2 vs %3")
                     .arg(m1.blockName).arg(m1.H_c).arg(m2.H_c)));
        QCOMPARE(m2.bhPoints, m1.bhPoints);
        QCOMPARE((int)m2.bhData.size(), (int)m1.bhData.size());
        for (size_t j = 0; j < m1.bhData.size(); j++) {
            QVERIFY2(std::fabs(m2.bhData[j].first - m1.bhData[j].first) < 1e-10,
                     qPrintable(QString("Material %1 BH[%2].B: %3 vs %4")
                         .arg(m1.blockName).arg(j)
                         .arg(m1.bhData[j].first).arg(m2.bhData[j].first)));
            QVERIFY2(std::fabs(m2.bhData[j].second - m1.bhData[j].second) < 1e-6,
                     qPrintable(QString("Material %1 BH[%2].H: %3 vs %4")
                         .arg(m1.blockName).arg(j)
                         .arg(m1.bhData[j].second).arg(m2.bhData[j].second)));
        }
    }

    // Compare block labels (material assignment and circuit connections)
    QCOMPARE((int)doc2.blockLabels.size(), (int)doc.blockLabels.size());
    for (size_t i = 0; i < doc.blockLabels.size(); i++) {
        const auto &b1 = doc.blockLabels[i];
        const auto &b2 = doc2.blockLabels[i];
        QVERIFY2(b2.blockType == b1.blockType,
                 qPrintable(QString("Block %1 type: '%2' vs '%3'")
                     .arg(i).arg(b1.blockType).arg(b2.blockType)));
        QVERIFY2(b2.inCircuit == b1.inCircuit,
                 qPrintable(QString("Block %1 circuit: '%2' vs '%3'")
                     .arg(i).arg(b1.inCircuit).arg(b2.inCircuit)));
        QVERIFY2(b2.turns == b1.turns,
                 qPrintable(QString("Block %1 turns: %2 vs %3")
                     .arg(i).arg(b1.turns).arg(b2.turns)));
        QVERIFY2(std::fabs(b2.magDir - b1.magDir) < 1e-10,
                 qPrintable(QString("Block %1 magDir: %2 vs %3")
                     .arg(i).arg(b1.magDir).arg(b2.magDir)));
    }

    // Compare boundary properties
    QCOMPARE((int)doc2.boundaryProps.size(), (int)doc.boundaryProps.size());
    for (size_t i = 0; i < doc.boundaryProps.size(); i++) {
        QCOMPARE(doc2.boundaryProps[i].bdryName, doc.boundaryProps[i].bdryName);
        QCOMPARE(doc2.boundaryProps[i].bdryFormat, doc.boundaryProps[i].bdryFormat);
    }

    // Compare circuit properties
    QCOMPARE((int)doc2.circuitProps.size(), (int)doc.circuitProps.size());
    for (size_t i = 0; i < doc.circuitProps.size(); i++) {
        const auto &c1 = doc.circuitProps[i];
        const auto &c2 = doc2.circuitProps[i];
        QCOMPARE(c2.circName, c1.circName);
        QVERIFY2(std::fabs(c2.amps.re - c1.amps.re) < 1e-10,
                 qPrintable(QString("Circuit %1 amps.re: %2 vs %3")
                     .arg(c1.circName).arg(c1.amps.re).arg(c2.amps.re)));
        QCOMPARE(c2.circType, c1.circType);
    }

    QFile::remove(tmpPath);
    if (diffCount > 0) {
        qWarning("WARNING: %d line differences found between original and saved .fem", diffCount);
    }
}

// ---------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------

void TestDocument::loadNonExistent()
{
    FemmeDocument doc;
    QVERIFY(!doc.loadFromFile("/nonexistent/path/to/file.fem"));
}

// ---------------------------------------------------------------
// Property lookup
// ---------------------------------------------------------------

void TestDocument::propertyIndexLookup()
{
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // Materials should be findable by name
    for (size_t i = 0; i < doc.materialProps.size(); i++) {
        int idx = doc.findMaterialPropIndex(doc.materialProps[i].blockName);
        QCOMPARE(idx, (int)i);
    }

    // Non-existent material should return -1
    QCOMPARE(doc.findMaterialPropIndex("NonExistentMaterial"), -1);

    // <None> should return -1
    QCOMPARE(doc.findMaterialPropIndex("<None>"), -1);
}
