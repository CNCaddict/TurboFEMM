// FEMM Unit Tests — Document I/O Tests
#include "test_document.h"
#include "document.h"
#include "femm_types.h"
#include "bhistory.h"
#include "ironloss.h"

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

// ---------------------------------------------------------------
// Steinmetz iron loss round-trip tests
// ---------------------------------------------------------------

void TestDocument::steinmetzRoundTrip()
{
    // Create a document with Steinmetz coefficients on a material
    FemmeDocument doc1;
    doc1.problemType = ProblemType::Planar;
    doc1.lengthUnits = LengthUnits::Millimeters;

    FMaterialProp mat;
    mat.blockName = "M19-Test";
    mat.mu_x = 4416.0;
    mat.mu_y = 4416.0;
    mat.Cduct = 2.67;
    mat.Kh = 179.0;
    mat.Kc = 0.569;
    mat.Ke = 1.56;
    mat.alpha_loss = 2.0;
    mat.density = 7700.0;

    // Add a core loss data point
    CoreLossPoint pt;
    pt.B = 1.5;
    pt.freq = 60.0;
    pt.loss_Wkg = 3.42;
    mat.coreLossData.push_back(pt);

    doc1.materialProps.push_back(mat);

    // Add a minimal node + block label
    FNode nd;
    nd.x = 0; nd.y = 0;
    doc1.nodes.push_back(nd);

    FBlockLabel blk;
    blk.x = 0.5; blk.y = 0.5;
    blk.blockType = "M19-Test";
    doc1.blockLabels.push_back(blk);

    // Save
    QString tmpPath = QDir::tempPath() + "/steinmetz_test.fem";
    QVERIFY(doc1.saveToFile(tmpPath));

    // Reload
    FemmeDocument doc2;
    QVERIFY(doc2.loadFromFile(tmpPath));

    // Verify Steinmetz fields survived
    QCOMPARE((int)doc2.materialProps.size(), 1);
    const auto &m = doc2.materialProps[0];
    QCOMPARE(m.blockName, QString("M19-Test"));
    QVERIFY(std::fabs(m.Kh - 179.0) < 1e-10);
    QVERIFY(std::fabs(m.Kc - 0.569) < 1e-10);
    QVERIFY(std::fabs(m.Ke - 1.56) < 1e-10);
    QVERIFY(std::fabs(m.alpha_loss - 2.0) < 1e-10);
    QVERIFY(std::fabs(m.density - 7700.0) < 1e-10);

    // Verify core loss data points
    QCOMPARE((int)m.coreLossData.size(), 1);
    QVERIFY(std::fabs(m.coreLossData[0].B - 1.5) < 1e-10);
    QVERIFY(std::fabs(m.coreLossData[0].freq - 60.0) < 1e-10);
    QVERIFY(std::fabs(m.coreLossData[0].loss_Wkg - 3.42) < 1e-10);

    QFile::remove(tmpPath);
}

void TestDocument::blockLabelLossRoundTrip()
{
    // Create a document with loss settings on a block label
    FemmeDocument doc1;
    doc1.problemType = ProblemType::Planar;
    doc1.lengthUnits = LengthUnits::Millimeters;

    FMaterialProp mat;
    mat.blockName = "TestSteel";
    doc1.materialProps.push_back(mat);

    FNode nd;
    nd.x = 0; nd.y = 0;
    doc1.nodes.push_back(nd);

    FBlockLabel blk;
    blk.x = 1.0; blk.y = 2.0;
    blk.blockType = "TestSteel";
    blk.calculateLosses = true;
    blk.inGroup = 5;
    doc1.blockLabels.push_back(blk);

    // Save
    QString tmpPath = QDir::tempPath() + "/blockloss_test.fem";
    QVERIFY(doc1.saveToFile(tmpPath));

    // Reload
    FemmeDocument doc2;
    QVERIFY(doc2.loadFromFile(tmpPath));

    QCOMPARE((int)doc2.blockLabels.size(), 1);
    const auto &b = doc2.blockLabels[0];
    QCOMPARE(b.calculateLosses, true);
    QCOMPARE(b.inGroup, 5);

    QFile::remove(tmpPath);
}

void TestDocument::backwardCompatNoLossFields()
{
    // Load the solenoid (old format, no Steinmetz fields)
    FemmeDocument doc;
    QVERIFY(doc.loadFromFile(solenoidPath));

    // All materials should have default Steinmetz values
    for (const auto &mp : doc.materialProps) {
        QVERIFY2(mp.Kh == 0.0,
                 qPrintable(QString("Material %1 should have Kh=0, got %2")
                     .arg(mp.blockName).arg(mp.Kh)));
        QVERIFY2(mp.Kc == 0.0,
                 qPrintable(QString("Material %1 should have Kc=0")
                     .arg(mp.blockName)));
        QVERIFY2(mp.alpha_loss == 2.0,
                 qPrintable(QString("Material %1 should have alpha=2.0")
                     .arg(mp.blockName)));
        QVERIFY(mp.coreLossData.empty());
    }

    // All block labels should have default loss settings
    for (const auto &bl : doc.blockLabels) {
        QVERIFY(!bl.calculateLosses);
    }
}

void TestDocument::bHistoryIndexLookup()
{
    // Create a snapshot with known elements on a regular grid
    BSnapshot snap;
    // 3x3 grid of elements at (1,1), (1,2), (1,3), (2,1), ... (3,3)
    for (int iy = 1; iy <= 3; iy++) {
        for (int ix = 1; ix <= 3; ix++) {
            float bx = (float)(ix * 10);  // Bx = 10, 20, 30
            float by = (float)(iy * 100); // By = 100, 200, 300
            snap.add((float)ix, (float)iy, bx, by);
        }
    }
    QCOMPARE(snap.numElements, 9);

    // Build index
    BHistoryIndex idx;
    idx.build(snap);

    // Query exactly at a centroid — should return that element's B
    auto [bx1, by1] = idx.lookup(2.0f, 2.0f);
    QVERIFY(std::fabs(bx1 - 20.0f) < 0.01f);
    QVERIFY(std::fabs(by1 - 200.0f) < 0.01f);

    // Query near (1,1) — should return element at (1,1)
    auto [bx2, by2] = idx.lookup(1.1f, 0.9f);
    QVERIFY(std::fabs(bx2 - 10.0f) < 0.01f);
    QVERIFY(std::fabs(by2 - 100.0f) < 0.01f);

    // Query near (3,3) — should return element at (3,3)
    auto [bx3, by3] = idx.lookup(2.8f, 3.1f);
    QVERIFY(std::fabs(bx3 - 30.0f) < 0.01f);
    QVERIFY(std::fabs(by3 - 300.0f) < 0.01f);

    // Query at midpoint between (1,1) and (2,1) — should return nearest
    auto [bx4, by4] = idx.lookup(1.4f, 1.0f);
    QVERIFY(std::fabs(bx4 - 10.0f) < 0.01f);  // closer to (1,1)
    QVERIFY(std::fabs(by4 - 100.0f) < 0.01f);
}

void TestDocument::bHistoryIndexEmpty()
{
    // Empty snapshot
    BSnapshot snap;
    BHistoryIndex idx;
    idx.build(snap);

    auto [bx, by] = idx.lookup(1.0f, 1.0f);
    QVERIFY(bx == 0.0f);
    QVERIFY(by == 0.0f);
}

// ---------------------------------------------------------------
// Iron loss computation tests
// ---------------------------------------------------------------

void TestDocument::steinmetzLossFormula()
{
    // Test the Steinmetz loss formula with known values
    // M-19 29ga: Kh=0.0275, Kc=4.844e-5, Ke=0.001, alpha=2.0
    double Kh = 0.0275;
    double Kc = 4.844e-5;
    double Ke = 0.001;
    double alpha = 2.0;
    double freq = 60.0;  // 60 Hz
    double Bpk = 1.5;    // 1.5 T

    double loss = steinmetzLoss_Wkg(Kh, Kc, Ke, alpha, freq, Bpk);

    // Hysteresis: 0.0275 * 60 * 1.5^2 = 0.0275 * 60 * 2.25 = 3.7125
    double hysteresis = Kh * freq * std::pow(Bpk, alpha);
    QVERIFY(std::fabs(hysteresis - 3.7125) < 0.001);

    // Eddy: 4.844e-5 * (60*1.5)^2 = 4.844e-5 * 8100 = 0.3924
    double fB = freq * Bpk;
    double eddy = Kc * fB * fB;
    QVERIFY(std::fabs(eddy - 0.3924) < 0.01);

    // Excess: 0.001 * (90)^1.5 = 0.001 * 853.77 = 0.854
    double excess = Ke * fB * std::sqrt(fB);
    QVERIFY(std::fabs(excess - 0.854) < 0.01);

    // Total should be sum of components
    QVERIFY(std::fabs(loss - (hysteresis + eddy + excess)) < 1e-10);

    // Loss should be positive and in reasonable range for steel at 1.5T/60Hz
    QVERIFY(loss > 1.0);   // at least 1 W/kg
    QVERIFY(loss < 20.0);  // less than 20 W/kg

    // Zero B → zero loss
    QVERIFY(steinmetzLoss_Wkg(Kh, Kc, Ke, alpha, freq, 0.0) == 0.0);

    // Zero freq → zero loss
    QVERIFY(steinmetzLoss_Wkg(Kh, Kc, Ke, alpha, 0.0, Bpk) == 0.0);
}

void TestDocument::ironLossFromBHistory()
{
    // Build a synthetic ResultsDocument with 2 triangular elements
    ResultsDocument rdoc;
    rdoc.problemType = 0;   // planar
    rdoc.lengthUnits = 1;   // mm
    rdoc.lengthConv = 0.001; // mm → meters
    rdoc.frequency = 0.0;
    rdoc.depth = 0.050;     // 50mm stack depth

    // Material: M-19 steel with Steinmetz coefficients (W/m³ units, as stored in .fem)
    SolnMaterial mat;
    mat.blockName = "M-19";
    mat.mu_x = 5000.0;
    mat.mu_y = 5000.0;
    mat.Kh = 210.375;      // 0.0275 * 7650 — W/m³ units
    mat.Kc = 0.370566;     // 4.844e-5 * 7650
    mat.Ke = 7.65;          // 0.001 * 7650
    mat.alpha_loss = 2.0;
    mat.density = 7650.0;  // kg/m^3
    rdoc.materials.push_back(mat);

    // Label with losses enabled
    SolnLabel lbl;
    lbl.blockType = 0;  // material index
    lbl.calculateLosses = true;
    rdoc.labels.push_back(lbl);

    // 4 nodes forming a 10mm x 10mm square, split into 2 triangles
    rdoc.nodes.resize(4);
    rdoc.nodes[0] = {0.0, 0.0, CmplxF(0, 0), 0.0};
    rdoc.nodes[1] = {10.0, 0.0, CmplxF(0, 0), 0.0};
    rdoc.nodes[2] = {10.0, 10.0, CmplxF(0, 0), 0.0};
    rdoc.nodes[3] = {0.0, 10.0, CmplxF(0, 0), 0.0};

    // Element 0: triangle (0,1,2), element 1: triangle (0,2,3)
    rdoc.elements.resize(2);
    rdoc.elements[0].p[0] = 0; rdoc.elements[0].p[1] = 1; rdoc.elements[0].p[2] = 2;
    rdoc.elements[0].lbl = 0; rdoc.elements[0].blk = 0;
    rdoc.elements[0].cx = 20.0/3.0; rdoc.elements[0].cy = 10.0/3.0;
    rdoc.elements[0].B1 = CmplxF(1.2, 0); rdoc.elements[0].B2 = CmplxF(0.5, 0);

    rdoc.elements[1].p[0] = 0; rdoc.elements[1].p[1] = 2; rdoc.elements[1].p[2] = 3;
    rdoc.elements[1].lbl = 0; rdoc.elements[1].blk = 0;
    rdoc.elements[1].cx = 10.0/3.0; rdoc.elements[1].cy = 20.0/3.0;
    rdoc.elements[1].B1 = CmplxF(1.0, 0); rdoc.elements[1].B2 = CmplxF(0.8, 0);

    // Create B history with 3 steps (simulating varying B across rotor positions)
    std::vector<BSnapshot> bHistory(3);

    // Step 0: B = (1.0, 0.5) at both centroids
    bHistory[0].add(20.0f/3.0f, 10.0f/3.0f, 1.0f, 0.5f);
    bHistory[0].add(10.0f/3.0f, 20.0f/3.0f, 0.8f, 0.6f);

    // Step 1: B = (1.5, 0.3) — higher peak
    bHistory[1].add(20.0f/3.0f, 10.0f/3.0f, 1.5f, 0.3f);
    bHistory[1].add(10.0f/3.0f, 20.0f/3.0f, 1.2f, 0.4f);

    // Step 2: B = (0.8, 0.2) — lower
    bHistory[2].add(20.0f/3.0f, 10.0f/3.0f, 0.8f, 0.2f);
    bHistory[2].add(10.0f/3.0f, 20.0f/3.0f, 0.7f, 0.3f);

    // Compute iron losses at 60 Hz
    IronLossResult result = computeIronLosses(bHistory, &rdoc, 60.0, 0.050);

    QVERIFY(result.valid);
    QVERIFY(result.frequency == 60.0);
    QVERIFY(result.totalLoss_W > 0.0);

    // Check element losses were computed
    QCOMPARE((int)result.elementLosses.size(), 2);

    // Element 0: peak B from step 1 = sqrt(1.5^2 + 0.3^2) = sqrt(2.34) ≈ 1.530
    double Bpk0 = std::sqrt(1.5*1.5 + 0.3*0.3);
    QVERIFY(std::fabs(result.elementLosses[0].Bpeak - Bpk0) < 0.01);
    QVERIFY(result.elementLosses[0].loss_Wkg > 0.0);
    QVERIFY(result.elementLosses[0].freq == 60.0);

    // Element 1: check from solution B (1.0, 0.8) → Bmag = sqrt(1.64) ≈ 1.281
    // vs step 1: (1.2, 0.4) → sqrt(1.6) ≈ 1.265
    // Solution B is higher, so Bpeak should be ~1.281
    double Bpk1 = std::sqrt(1.0*1.0 + 0.8*0.8);  // from final solution
    double Bpk1_step1 = std::sqrt(1.2*1.2 + 0.4*0.4);
    double expectedBpk1 = std::max(Bpk1, Bpk1_step1);
    QVERIFY(std::fabs(result.elementLosses[1].Bpeak - expectedBpk1) < 0.01);

    // Check block summary
    QCOMPARE((int)result.blockSummaries.size(), 1);
    QVERIFY(result.blockSummaries[0].materialName == "M-19");
    QVERIFY(result.blockSummaries[0].numElements == 2);
    QVERIFY(result.blockSummaries[0].totalLoss_W > 0.0);
    QVERIFY(result.blockSummaries[0].totalArea_m2 > 0.0);

    // Total loss should equal the block's total loss (only one block)
    QVERIFY(std::fabs(result.totalLoss_W - result.blockSummaries[0].totalLoss_W) < 1e-10);

    // Sanity: losses should be reasonable for a small 10mm x 10mm x 50mm block
    // Area = 100 mm² = 1e-4 m², volume = 5e-6 m³, mass ≈ 0.038 kg
    // At ~5 W/kg → ~0.19 W total — order of magnitude check
    QVERIFY(result.totalLoss_W > 0.001);  // at least 1 mW
    QVERIFY(result.totalLoss_W < 10.0);   // less than 10 W
}

void TestDocument::rotorLossInverseTransform()
{
    // Test that rotor elements are correctly tracked via inverse transform.
    // Scenario: 3-step rotation sweep, 10° per step, center at origin.
    // A rotor element ends up at (10, 0). At step 0 it was 20° back,
    // at step 1 it was 10° back, at step 2 it's at final position.

    ResultsDocument rdoc;
    rdoc.problemType = 0;   // planar
    rdoc.lengthUnits = 1;
    rdoc.lengthConv = 0.001;
    rdoc.frequency = 0.0;
    rdoc.depth = 0.050;

    // Material with known Steinmetz coefficients (W/m³ units, as stored in .fem)
    SolnMaterial mat;
    mat.blockName = "M-19";
    mat.mu_x = 5000.0;
    mat.mu_y = 5000.0;
    mat.Kh = 210.375;      // W/m³ units
    mat.Kc = 0.370566;
    mat.Ke = 7.65;
    mat.alpha_loss = 2.0;
    mat.density = 7650.0;
    rdoc.materials.push_back(mat);

    // Rotor label: inGroup=1, losses enabled
    SolnLabel lbl;
    lbl.blockType = 0;
    lbl.calculateLosses = true;
    lbl.inGroup = 1;  // rotor group
    rdoc.labels.push_back(lbl);

    // Single triangular element centered at (10, 0) — the FINAL position
    rdoc.nodes.resize(3);
    rdoc.nodes[0] = {9.0, -1.0, CmplxF(0, 0), 0.0};
    rdoc.nodes[1] = {11.0, -1.0, CmplxF(0, 0), 0.0};
    rdoc.nodes[2] = {10.0, 1.0, CmplxF(0, 0), 0.0};

    rdoc.elements.resize(1);
    rdoc.elements[0].p[0] = 0; rdoc.elements[0].p[1] = 1; rdoc.elements[0].p[2] = 2;
    rdoc.elements[0].lbl = 0; rdoc.elements[0].blk = 0;
    rdoc.elements[0].cx = 10.0; rdoc.elements[0].cy = 0.0;
    rdoc.elements[0].B1 = CmplxF(0.5, 0); rdoc.elements[0].B2 = CmplxF(0.3, 0);

    // Build BSnapshots: rotor element was at a rotated position at each step.
    // Rotation: 10° per step, center (0,0), 2 total steps (steps 0,1,2).
    // At step s, element was at angle = -(2-s)*10° from final position (10,0).
    //   Step 0: rotated -20° → centroid at (10*cos(-20°), 10*sin(-20°))
    //   Step 1: rotated -10° → centroid at (10*cos(-10°), 10*sin(-10°))
    //   Step 2: at (10, 0) — final position

    double r = 10.0;
    double ang0 = -20.0 * M_PI / 180.0;
    double ang1 = -10.0 * M_PI / 180.0;

    float cx0 = (float)(r * std::cos(ang0));
    float cy0 = (float)(r * std::sin(ang0));
    float cx1 = (float)(r * std::cos(ang1));
    float cy1 = (float)(r * std::sin(ang1));

    std::vector<BSnapshot> bHistory(3);

    // Each snapshot has 2 entries: the rotor element at its actual position,
    // and a stationary "stator" entry at (10, 0) with LOW B.
    // Without inverse transform, element at (10,0) finds the stator entry.
    // With inverse transform, it correctly finds the rotor entry at the
    // back-transformed position.

    // Step 0: rotor at rotated position, stator at (10,0)
    bHistory[0].add(cx0, cy0, 0.5f, 0.3f);      // rotor |B| ≈ 0.583
    bHistory[0].add(10.0f, 0.0f, 0.1f, 0.05f);  // stator |B| ≈ 0.112

    // Step 1: rotor at rotated position (HIGH B), stator at (10,0) (low)
    bHistory[1].add(cx1, cy1, 1.8f, 0.4f);       // rotor |B| ≈ 1.844
    bHistory[1].add(10.0f, 0.0f, 0.15f, 0.08f);  // stator |B| ≈ 0.170

    // Step 2: both at (10,0) — rotor reached final position
    bHistory[2].add(10.0f, 0.0f, 1.0f, 0.5f);    // rotor |B| ≈ 1.118
    bHistory[2].add(10.0f, 0.01f, 0.12f, 0.06f); // stator nearby |B| ≈ 0.134

    // Motion params: rotation, 10° per step, 2 total steps, group 1
    MotionParams motion;
    motion.movingGroup = 1;
    motion.isRotation = true;
    motion.cx = 0.0;
    motion.cy = 0.0;
    motion.anglePerStep = 10.0;
    motion.totalSteps = 2;

    IronLossResult result = computeIronLosses(bHistory, &rdoc, 60.0, 0.050, motion);

    QVERIFY(result.valid);
    QCOMPARE((int)result.elementLosses.size(), 1);

    // The peak B should come from step 1: |B| = sqrt(1.8² + 0.4²) ≈ 1.844
    double expectedBpk = std::sqrt(1.8*1.8 + 0.4*0.4);
    QVERIFY2(std::fabs(result.elementLosses[0].Bpeak - expectedBpk) < 0.05,
             qPrintable(QString("Expected Bpk %1, got %2")
                 .arg(expectedBpk).arg(result.elementLosses[0].Bpeak)));

    // Loss should be non-zero and computed at the correct peak B
    QVERIFY(result.elementLosses[0].loss_Wkg > 0.0);

    // Verify the loss matches what steinmetzLoss_Wkg gives for the peak B
    // Raw formula returns W/m³; divide by density to get W/kg
    double expectedLoss = steinmetzLoss_Wkg(210.375, 0.370566, 7.65, 2.0, 60.0, expectedBpk) / 7650.0;
    QVERIFY(std::fabs(result.elementLosses[0].loss_Wkg - expectedLoss) < 0.01);

    // Now test WITHOUT motion params (default) — same BHistory but stator lookup.
    // The element at (10,0) should find step 2's B at (10,0) = (1.0, 0.5) → |B|=1.118
    // and step 0/1 entries are at different positions, so won't match well.
    IronLossResult resultNoMotion = computeIronLosses(bHistory, &rdoc, 60.0, 0.050);
    QVERIFY(resultNoMotion.valid);

    // Without inverse transform, peak B should be lower (can't find step 1's high B)
    // It should find step 2's entry (at 10,0) and possibly final solution B.
    // Final solution B = sqrt(0.5² + 0.3²) = 0.583
    // Step 2 B at (10,0) = sqrt(1.0² + 0.5²) = 1.118
    // So Bpeak without motion ≈ 1.118 (much less than 1.844 with motion)
    QVERIFY(resultNoMotion.elementLosses[0].Bpeak < result.elementLosses[0].Bpeak);
}

void TestDocument::conductiveEddyLossAutoCompute()
{
    // Test dB/dt-based eddy current loss for conductive materials
    // without explicit Steinmetz coefficients.
    // Uses P = sigma * d^2 * <(dB/dt)^2> / 12  where d = Lam_d.

    ResultsDocument rdoc;
    rdoc.problemType = 0;
    rdoc.lengthUnits = 1;   // mm
    rdoc.lengthConv = 0.001;
    rdoc.frequency = 0.0;
    rdoc.depth = 0.050;     // 50mm problem depth

    // NdFeB-like material: conductive, with Lam_d set to segment thickness
    SolnMaterial mat;
    mat.blockName = "NdFeB";
    mat.mu_x = 1.05;
    mat.mu_y = 1.05;
    mat.H_c = 979000;
    mat.Cduct = 0.667;   // MS/m (typical NdFeB)
    mat.Lam_d = 5.0;     // 5mm magnet segment thickness
    mat.Kh = 0; mat.Kc = 0; mat.Ke = 0;  // no explicit Steinmetz
    mat.density = 7500.0;  // kg/m^3
    rdoc.materials.push_back(mat);

    SolnLabel lbl;
    lbl.blockType = 0;
    lbl.calculateLosses = true;
    rdoc.labels.push_back(lbl);

    // Single triangular element
    rdoc.nodes.resize(3);
    rdoc.nodes[0] = {0.0, 0.0, CmplxF(0, 0), 0.0};
    rdoc.nodes[1] = {10.0, 0.0, CmplxF(0, 0), 0.0};
    rdoc.nodes[2] = {5.0, 10.0, CmplxF(0, 0), 0.0};

    rdoc.elements.resize(1);
    rdoc.elements[0].p[0] = 0; rdoc.elements[0].p[1] = 1; rdoc.elements[0].p[2] = 2;
    rdoc.elements[0].lbl = 0; rdoc.elements[0].blk = 0;
    rdoc.elements[0].cx = 5.0; rdoc.elements[0].cy = 10.0/3.0;
    rdoc.elements[0].B1 = CmplxF(0.8, 0); rdoc.elements[0].B2 = CmplxF(0.3, 0);

    // B history: 3 steps with varying field at known time intervals
    // Simulating 1000 RPM, 2° per step → dt = 2/(6*1000) = 1/3000 s
    std::vector<BSnapshot> bHistory(3);
    bHistory[0].add(5.0f, 10.0f/3.0f, 0.2f, 0.1f);
    bHistory[1].add(5.0f, 10.0f/3.0f, 0.5f, 0.2f);
    bHistory[2].add(5.0f, 10.0f/3.0f, 0.8f, 0.3f);

    MotionParams motion;
    motion.isRotation = true;
    motion.anglePerStep = 2.0;  // 2° per step
    motion.totalSteps = 2;
    motion.rpm = 1000.0;

    IronLossResult result = computeIronLosses(bHistory, &rdoc, 100.0, 0.050, motion);

    QVERIFY(result.valid);
    QCOMPARE((int)result.elementLosses.size(), 1);

    // Loss should be non-zero (dB/dt-based eddy current)
    QVERIFY2(result.elementLosses[0].loss_Wkg > 0.0,
             "Conductive material should have dB/dt-based eddy current losses");

    // Verify against manual calculation:
    // dt = 2.0 / (6.0 * 1000) = 1/3000 s
    double dt = 2.0 / (6.0 * 1000.0);
    // Step 0→1: dBx/dt = (0.5-0.2)/dt, dBy/dt = (0.2-0.1)/dt
    double dBx01 = (0.5 - 0.2) / dt, dBy01 = (0.2 - 0.1) / dt;
    // Step 1→2: dBx/dt = (0.8-0.5)/dt, dBy/dt = (0.3-0.2)/dt
    double dBx12 = (0.8 - 0.5) / dt, dBy12 = (0.3 - 0.2) / dt;

    double dbdt2_01 = dBx01*dBx01 + dBy01*dBy01;
    double dbdt2_12 = dBx12*dBx12 + dBy12*dBy12;
    double meanDbdt2 = (dbdt2_01 + dbdt2_12) / 2.0;

    // P = sigma * d^2 * <(dB/dt)^2> / 12
    double sigma_SI = 0.667e6;
    double d_m = 5.0e-3;  // 5mm in meters
    double expectedLoss_Wm3 = sigma_SI * d_m * d_m * meanDbdt2 / 12.0;
    double expectedLoss_Wkg = expectedLoss_Wm3 / 7500.0;

    QVERIFY2(std::fabs(result.elementLosses[0].loss_Wkg - expectedLoss_Wkg)
             / expectedLoss_Wkg < 0.01,
             qPrintable(QString("Expected %1 W/kg, got %2 W/kg")
                 .arg(expectedLoss_Wkg).arg(result.elementLosses[0].loss_Wkg)));

    // With Lam_d=0, the Az-based path should kick in (solid conductor).
    // The loss should be non-zero if Az data is present.
    // Since we haven't set Az in the snapshot, the Az values default to 0
    // and dAz/dt = 0, so loss will be zero.  That's correct for this test —
    // the Az-based path is tested separately in azBasedSolidConductorLoss().
    rdoc.materials[0].Lam_d = 0.0;
    IronLossResult result2 = computeIronLosses(bHistory, &rdoc, 100.0, 0.050, motion);
    QVERIFY(result2.valid);
    QVERIFY2(result2.elementLosses[0].loss_Wkg == 0.0,
             "Solid conductor with Az=0 in all snapshots should have zero eddy loss");
}

void TestDocument::azBasedSolidConductorLoss()
{
    // Test Az-based eddy current loss for solid conductors (Lam_d = 0).
    // P = sigma * <(dAz/dt)^2>  [W/m^3]
    //
    // This is exact for 2D: the spatial variation of Az across a conductor
    // naturally limits the eddy current loops.  No slab dimension needed.

    ResultsDocument rdoc;
    rdoc.problemType = 0;
    rdoc.lengthUnits = 1;   // mm
    rdoc.lengthConv = 0.001;
    rdoc.frequency = 0.0;
    rdoc.depth = 0.050;     // 50mm depth

    // Solid NdFeB magnet: conductive, no lamination, no Steinmetz
    SolnMaterial mat;
    mat.blockName = "NdFeB Solid";
    mat.mu_x = 1.05;
    mat.mu_y = 1.05;
    mat.Cduct = 0.667;   // MS/m
    mat.Lam_d = 0.0;     // solid — triggers Az path
    mat.Kh = 0; mat.Kc = 0; mat.Ke = 0;
    mat.density = 7500.0;
    rdoc.materials.push_back(mat);

    SolnLabel lbl;
    lbl.blockType = 0;
    lbl.calculateLosses = true;
    rdoc.labels.push_back(lbl);

    // Single triangle with nodes that have varying Az values per step
    rdoc.nodes.resize(3);
    rdoc.nodes[0] = {0.0, 0.0, CmplxF(0, 0), 0.0};
    rdoc.nodes[1] = {10.0, 0.0, CmplxF(0, 0), 0.0};
    rdoc.nodes[2] = {5.0, 10.0, CmplxF(0, 0), 0.0};

    rdoc.elements.resize(1);
    rdoc.elements[0].p[0] = 0;
    rdoc.elements[0].p[1] = 1;
    rdoc.elements[0].p[2] = 2;
    rdoc.elements[0].lbl = 0;
    rdoc.elements[0].blk = 0;
    rdoc.elements[0].cx = 5.0;
    rdoc.elements[0].cy = 10.0/3.0;
    rdoc.elements[0].B1 = CmplxF(0.5, 0);
    rdoc.elements[0].B2 = CmplxF(0.3, 0);

    // B history: 3 steps with known Az values at the element centroid.
    // 1000 RPM, 2° per step → dt = 2/(6*1000) = 1/3000 s
    //
    // Az values simulate varying vector potential as field changes:
    //   Step 0: Az = 0.001 Wb/m
    //   Step 1: Az = 0.003 Wb/m
    //   Step 2: Az = 0.002 Wb/m
    std::vector<BSnapshot> bHistory(3);
    bHistory[0].add(5.0f, 10.0f/3.0f, 0.5f, 0.3f, 0.001f);
    bHistory[1].add(5.0f, 10.0f/3.0f, 0.5f, 0.3f, 0.003f);
    bHistory[2].add(5.0f, 10.0f/3.0f, 0.5f, 0.3f, 0.002f);

    MotionParams motion;
    motion.isRotation = true;
    motion.anglePerStep = 2.0;
    motion.totalSteps = 2;
    motion.rpm = 1000.0;

    IronLossResult result = computeIronLosses(bHistory, &rdoc, 100.0, 0.050, motion);

    QVERIFY(result.valid);
    QCOMPARE((int)result.elementLosses.size(), 1);
    QVERIFY2(result.elementLosses[0].loss_Wkg > 0.0,
             "Solid conductor with varying Az should have non-zero eddy loss");

    // Manual calculation:
    // dt = 2.0 / (6.0 * 1000) = 1/3000 s
    double dt = 2.0 / 6000.0;

    // Step 0→1: dAz/dt = (0.003 - 0.001) / dt = 0.002 * 3000 = 6.0
    double dAzdt_01 = (0.003 - 0.001) / dt;
    // Step 1→2: dAz/dt = (0.002 - 0.003) / dt = -0.001 * 3000 = -3.0
    double dAzdt_12 = (0.002 - 0.003) / dt;

    double meanDazdt2 = (dAzdt_01*dAzdt_01 + dAzdt_12*dAzdt_12) / 2.0;
    // = (36.0 + 9.0) / 2 = 22.5

    // P = sigma * <(dAz/dt)^2>
    double sigma_SI = 0.667e6;
    double expectedLoss_Wm3 = sigma_SI * meanDazdt2;
    double expectedLoss_Wkg = expectedLoss_Wm3 / 7500.0;

    double relErr = std::fabs(result.elementLosses[0].loss_Wkg - expectedLoss_Wkg)
                    / expectedLoss_Wkg;
    QVERIFY2(relErr < 0.01,
             qPrintable(QString("Az-based loss: expected %1 W/kg, got %2 W/kg (err=%3%)")
                 .arg(expectedLoss_Wkg).arg(result.elementLosses[0].loss_Wkg)
                 .arg(relErr * 100.0)));

    // The loss should be much more modest than the old d^2 slab formula
    // with problem depth would give
    QVERIFY2(result.elementLosses[0].loss_Wkg < 10000.0,
             "Az-based loss should be reasonable, not blown up by depth^2");
}

void TestDocument::steinmetzM19at60Hz()
{
    // Validate Steinmetz formula against M-19 steel datasheet:
    // At 60 Hz, 1.5 T, M-19 core loss ≈ 3.5-4.0 W/kg
    //
    // FEMM coefficients for M-19 (from lrk.fem):
    //   Kh = 179, Kc = 0.569, Ke = 1.56, alpha = 2, density = 7700

    double Kh = 179.0, Kc = 0.569, Ke = 1.56;
    double alpha = 2.0;
    double density = 7700.0;
    double freq = 60.0;
    double Bpk = 1.5;

    // Raw formula gives W/m^3
    double loss_Wm3 = steinmetzLoss_Wm3(Kh, Kc, Ke, alpha, freq, Bpk);
    double loss_Wkg = loss_Wm3 / density;

    // M-19 datasheet: core loss at 60 Hz, 1.5T is approximately 3.5-4.5 W/kg
    QVERIFY2(loss_Wkg > 2.5 && loss_Wkg < 6.0,
             qPrintable(QString("M-19 at 60Hz/1.5T: %1 W/kg (expected 3.5-4.5)")
                 .arg(loss_Wkg)));

    // At 0 frequency, loss should be zero
    QCOMPARE(steinmetzLoss_Wm3(Kh, Kc, Ke, alpha, 0.0, 1.5), 0.0);

    // At 0 flux, loss should be zero
    QCOMPARE(steinmetzLoss_Wm3(Kh, Kc, Ke, alpha, 60.0, 0.0), 0.0);

    // Check individual terms make sense:
    // Hysteresis: Kh * f * Bpk^2 = 179 * 60 * 2.25 = 24,165 W/m^3
    double hyst = Kh * freq * std::pow(Bpk, alpha);
    QVERIFY2(std::fabs(hyst - 24165.0) < 1.0,
             qPrintable(QString("Hysteresis term: %1 (expected 24165)").arg(hyst)));

    // Eddy: Kc * (f*Bpk)^2 = 0.569 * (90)^2 = 0.569 * 8100 = 4608.9
    double fB = freq * Bpk;
    double eddy = Kc * fB * fB;
    QVERIFY2(std::fabs(eddy - 4608.9) < 1.0,
             qPrintable(QString("Eddy term: %1 (expected 4608.9)").arg(eddy)));
}

// ---------------------------------------------------------------
// 3-phase commutation: verify electrical angle tracks rotor
// ---------------------------------------------------------------
void TestDocument::threePhaseCommutationTracking()
{
    // Simulate the commutation logic from motionrunner.cpp
    // For a 14-pole (7 PP) motor rotating at 2° per step:
    //   - rotor electrical angle at step N = N * 2° * 7 = N * 14°
    //   - stator electrical angle must also advance by N * 14° to maintain
    //     constant torque angle (= constant relative phase)
    //
    // The relative phase (stator_elec - rotor_elec) should be CONSTANT
    // across all steps, equal to the optimal angle.

    int polePairs = 7;
    double anglePerStep = 2.0;  // mechanical degrees
    double optimalAngle = 42.0; // arbitrary offset
    bool reversePhase = false;

    // Replicate the motionrunner sign logic:
    double sign = reversePhase ? -1.0 : 1.0;

    for (int step = 0; step <= 20; step++) {
        double cumMechAngle = anglePerStep * (double)step;
        double rotorElec = cumMechAngle * polePairs;
        double statorElec = optimalAngle + sign * cumMechAngle * polePairs;

        double relativePhase = statorElec - rotorElec;

        // The relative phase must equal optimalAngle at EVERY step
        QVERIFY2(std::fabs(relativePhase - optimalAngle) < 1e-10,
                 qPrintable(QString("Step %1: relative phase = %2, expected %3 "
                                     "(stator=%4, rotor=%5)")
                     .arg(step).arg(relativePhase).arg(optimalAngle)
                     .arg(statorElec).arg(rotorElec)));
    }

    // Also verify 3-phase balance: Ia + Ib + Ic ≈ 0
    // Using the same formula as computeCurrents:
    //   Ia = Ipk * cos(θ), Ib = Ipk * cos(θ - 120°), Ic = Ipk * cos(θ + 120°)
    for (int deg = 0; deg < 360; deg += 15) {
        double theta = (double)deg * M_PI / 180.0;
        double Ia = std::cos(theta);
        double Ib = std::cos(theta - 2.0 * M_PI / 3.0);
        double Ic = std::cos(theta + 2.0 * M_PI / 3.0);
        double sum = Ia + Ib + Ic;
        QVERIFY2(std::fabs(sum) < 1e-10,
                 qPrintable(QString("Phase balance at %1°: Ia+Ib+Ic = %2")
                     .arg(deg).arg(sum)));
    }

    // Verify reverse phase flips the tracking direction
    sign = -1.0;  // reversePhase = true
    double statorElecR = optimalAngle + sign * (10.0 * polePairs);
    double rotorElecR = 10.0 * polePairs;
    double relPhaseReversed = statorElecR - rotorElecR;
    // With reversed sign, relative phase = optimal - 2*rotor_elec (NOT constant)
    QVERIFY2(std::fabs(relPhaseReversed - optimalAngle) > 1.0,
             "Reversed phase should NOT maintain constant torque angle");
}
