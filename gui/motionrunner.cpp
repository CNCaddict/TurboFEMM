// FEMM Qt 6 GUI — Parametric Motion Runner implementation
#include "motionrunner.h"
#include "document.h"
#include "drawingwidget.h"
#include "meshgen.h"
#include "solverrunner.h"
#include "inprocesssolver.h"
#include "resultsoverlay.h"
#include "resultsdoc.h"
#include "motoroptimizer.h"
#include "ironloss.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QBuffer>
#include <QDir>
#include <QPixmap>
#include <QPainter>
#include <QApplication>
#include <QThread>
#include <QTimer>
#include <cmath>

// Worker thread for running the in-process solver off the main thread
class SolveThread : public QThread {
public:
    InProcessSolver *solver = nullptr;
    FemmeDocument *doc = nullptr;
    ResultsDocument *results = nullptr;
    bool success = false;

protected:
    void run() override {
        success = solver->solve(doc, results);
    }
};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------
// Minimal GIF89a encoder (no external dependencies)
// ---------------------------------------------------------------

// Write a GIF87a/89a animated GIF from a sequence of QImages.
// Uses a simple median-cut palette per frame + local color tables.
// Supports disposal and frame delay for animation.

namespace {

// Simple color quantization: uniform 6-6-6 palette (216 colors)
// Fast and avoids complex median-cut for this use case.
static void buildUniformPalette(QVector<QRgb> &palette)
{
    palette.clear();
    palette.reserve(216);
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++)
                palette.append(qRgb(r * 51, g * 51, b * 51));
    // Pad to 256 entries (GIF requires power-of-2 color table)
    while (palette.size() < 256)
        palette.append(qRgb(0, 0, 0));
}

static int findClosestColor(const QVector<QRgb> &palette, QRgb pixel)
{
    int r = qRed(pixel), g = qGreen(pixel), b = qBlue(pixel);
    // Fast lookup for uniform 6-6-6 palette
    int ri = qBound(0, (r + 25) / 51, 5);
    int gi = qBound(0, (g + 25) / 51, 5);
    int bi = qBound(0, (b + 25) / 51, 5);
    return ri * 36 + gi * 6 + bi;
}

// LZW encoder for GIF
struct LZWEncoder {
    QByteArray output;
    int minCodeSize;
    int clearCode;
    int eoiCode;
    int nextCode;
    int codeSize;

    struct Entry {
        int prefix;
        int suffix;
        int next;  // hash chain
    };
    static const int TABLE_SIZE = 5003;  // prime for hashing
    std::vector<Entry> table;
    std::vector<int> hashTable;

    // Bit accumulator
    uint32_t bitAcc;
    int bitCount;
    QByteArray subBlock;

    void init(int minBits)
    {
        minCodeSize = minBits;
        clearCode = 1 << minBits;
        eoiCode = clearCode + 1;
        resetTable();
        bitAcc = 0;
        bitCount = 0;
        subBlock.clear();
        output.clear();
    }

    void resetTable()
    {
        nextCode = eoiCode + 1;
        codeSize = minCodeSize + 1;
        table.clear();
        table.resize(TABLE_SIZE);
        hashTable.assign(TABLE_SIZE, -1);
    }

    int hashKey(int prefix, int suffix) const
    {
        return ((prefix << 8) ^ suffix) % TABLE_SIZE;
    }

    int findEntry(int prefix, int suffix) const
    {
        int h = hashKey(prefix, suffix);
        int idx = hashTable[h];
        while (idx >= 0) {
            if (table[idx].prefix == prefix && table[idx].suffix == suffix)
                return idx;
            idx = table[idx].next;
        }
        return -1;
    }

    void addEntry(int prefix, int suffix)
    {
        if (nextCode >= 4096) return;
        int h = hashKey(prefix, suffix);
        int idx = nextCode++;
        if (idx < (int)table.size()) {
            table[idx].prefix = prefix;
            table[idx].suffix = suffix;
            table[idx].next = hashTable[h];
            hashTable[h] = idx;
        }
    }

    void emitCode(int code)
    {
        bitAcc |= ((uint32_t)code << bitCount);
        bitCount += codeSize;
        while (bitCount >= 8) {
            subBlock.append((char)(bitAcc & 0xFF));
            bitAcc >>= 8;
            bitCount -= 8;
            if (subBlock.size() >= 255) {
                output.append((char)subBlock.size());
                output.append(subBlock);
                subBlock.clear();
            }
        }
    }

    void flushBits()
    {
        if (bitCount > 0) {
            subBlock.append((char)(bitAcc & 0xFF));
            bitAcc = 0;
            bitCount = 0;
        }
        if (!subBlock.isEmpty()) {
            output.append((char)subBlock.size());
            output.append(subBlock);
            subBlock.clear();
        }
        output.append((char)0);  // block terminator
    }

    QByteArray encode(const QByteArray &indices)
    {
        init(8);  // 8-bit min code size for 256-color images

        emitCode(clearCode);

        if (indices.isEmpty()) {
            emitCode(eoiCode);
            flushBits();
            return output;
        }

        int w = (unsigned char)indices[0];
        for (int i = 1; i < indices.size(); i++) {
            int k = (unsigned char)indices[i];
            int entry = findEntry(w, k);
            if (entry >= 0) {
                w = entry;
            } else {
                emitCode(w);
                addEntry(w, k);
                if (nextCode > (1 << codeSize) && codeSize < 12)
                    codeSize++;
                if (nextCode >= 4094) {
                    emitCode(clearCode);
                    resetTable();
                }
                w = k;
            }
        }
        emitCode(w);
        emitCode(eoiCode);
        flushBits();
        return output;
    }
};

static void writeLE16(QByteArray &buf, uint16_t val)
{
    buf.append((char)(val & 0xFF));
    buf.append((char)((val >> 8) & 0xFF));
}

static QByteArray encodeAnimatedGIF(const std::vector<QImage> &frames, int delayCs)
{
    if (frames.empty()) return QByteArray();

    int W = frames[0].width();
    int H = frames[0].height();

    // Build global palette (uniform 6-6-6)
    QVector<QRgb> palette;
    buildUniformPalette(palette);

    QByteArray gif;

    // --- Header ---
    gif.append("GIF89a", 6);
    writeLE16(gif, (uint16_t)W);
    writeLE16(gif, (uint16_t)H);
    gif.append((char)0xF7);  // GCT flag=1, color res=7, sort=0, GCT size=7 (256 colors)
    gif.append((char)0);     // background color index
    gif.append((char)0);     // pixel aspect ratio

    // --- Global Color Table (256 * 3 bytes) ---
    for (int i = 0; i < 256; i++) {
        QRgb c = palette[i];
        gif.append((char)qRed(c));
        gif.append((char)qGreen(c));
        gif.append((char)qBlue(c));
    }

    // --- NETSCAPE Application Extension (for looping) ---
    gif.append((char)0x21);  // extension introducer
    gif.append((char)0xFF);  // application extension
    gif.append((char)11);    // block size
    gif.append("NETSCAPE2.0", 11);
    gif.append((char)3);     // sub-block size
    gif.append((char)1);     // sub-block ID
    gif.append((char)0);     // loop count = 0 (infinite)
    gif.append((char)0);
    gif.append((char)0);     // block terminator

    // --- Frames ---
    for (size_t f = 0; f < frames.size(); f++) {
        QImage img = frames[f].convertToFormat(QImage::Format_ARGB32);
        if (img.width() != W || img.height() != H)
            img = img.scaled(W, H, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

        // Graphic Control Extension
        gif.append((char)0x21);  // extension introducer
        gif.append((char)0xF9);  // graphic control
        gif.append((char)4);     // block size
        gif.append((char)0x00);  // disposal=none, no transparency
        writeLE16(gif, (uint16_t)delayCs);  // delay in centiseconds
        gif.append((char)0);     // transparent color index (unused)
        gif.append((char)0);     // block terminator

        // Image Descriptor
        gif.append((char)0x2C);  // image separator
        writeLE16(gif, 0);       // left
        writeLE16(gif, 0);       // top
        writeLE16(gif, (uint16_t)W);
        writeLE16(gif, (uint16_t)H);
        gif.append((char)0x00);  // no local color table, not interlaced

        // Quantize pixels to indices
        QByteArray indices;
        indices.resize(W * H);
        for (int y = 0; y < H; y++) {
            const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < W; x++) {
                indices[y * W + x] = (char)findClosestColor(palette, line[x]);
            }
        }

        // LZW encode
        gif.append((char)8);  // LZW minimum code size
        LZWEncoder enc;
        QByteArray lzwData = enc.encode(indices);
        gif.append(lzwData);
    }

    // --- Trailer ---
    gif.append((char)0x3B);

    return gif;
}

}  // anonymous namespace

// ---------------------------------------------------------------
// MotionRunner implementation
// ---------------------------------------------------------------

MotionRunner::MotionRunner(QObject *parent)
    : QObject(parent)
{
}

MotionRunner::~MotionRunner()
{
    m_aborting = true;
    if (m_solveThread) {
        m_solveThread->wait();
        delete m_solveThread;
        m_solveThread = nullptr;
    }
    delete m_pendingResults;
    m_pendingResults = nullptr;
    delete m_lastResultsDoc;
    m_lastResultsDoc = nullptr;
}

ResultsDocument *MotionRunner::takeLastResults()
{
    ResultsDocument *doc = m_lastResultsDoc;
    m_lastResultsDoc = nullptr;
    return doc;
}

void MotionRunner::start(const MotionConfig &config,
                          FemmeDocument *doc,
                          DrawingWidget *dw,
                          MeshGenerator *meshGen,
                          SolverRunner *solver,
                          ResultsOverlayRenderer *overlay)
{
    if (m_running) return;

    m_config = config;
    m_doc = doc;
    m_dw = dw;
    m_meshGen = meshGen;
    m_solver = solver;
    m_overlay = overlay;
    m_running = true;
    m_aborting = false;
    m_currentStep = 0;
    m_results.clear();
    m_bHistory.clear();
    m_frames.clear();
    m_timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

    // Backup geometry, circuits, and mesh for restore
    m_backupNodes = doc->nodes;
    m_backupSegments = doc->segments;
    m_backupArcs = doc->arcSegments;
    m_backupLabels = doc->blockLabels;
    m_backupCircuits = doc->circuitProps;
    m_backupMeshNodes = doc->meshNodes;
    m_backupMeshElements = doc->meshElements;
    m_backupMeshEdges = doc->meshEdges;
    m_backupHasMesh = doc->hasMesh;

    // Let Qt settle any pending layout/resize events (e.g. from the modal
    // dialog closing) before we snapshot the view state.  ExcludeUserInput
    // prevents re-entrancy from mouse/key events.
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // Backup view state for restore after sweep (post-layout-settle)
    m_backupViewOx = dw->viewOx();
    m_backupViewOy = dw->viewOy();
    m_backupViewMag = dw->viewMag();

    // Freeze the entire window hierarchy so the mesh/solve pipeline can't
    // trigger layout changes that resize anything (which causes the view to
    // drift and creates grey gaps).
    m_backupMinSize = dw->minimumSize();
    m_backupMaxSize = dw->maximumSize();
    QWidget *subWin = dw->parentWidget();
    if (subWin) {
        m_backupSubMinSize = subWin->minimumSize();
        m_backupSubMaxSize = subWin->maximumSize();
        subWin->setFixedSize(subWin->size());
    }
    QWidget *topWin = dw->window();
    if (topWin) {
        m_backupTopMinSize = topWin->minimumSize();
        m_backupTopMaxSize = topWin->maximumSize();
        topWin->setFixedSize(topWin->size());
    }
    dw->setFixedSize(dw->size());

    // Lock the view to prevent any user interaction from shifting it mid-sweep.
    dw->setViewLocked(true);

    // Create in-process solver for the sweep
    m_inProcessSolver = new InProcessSolver(this);
    connect(m_inProcessSolver, &InProcessSolver::progress,
            this, &MotionRunner::progress);

    // Connect to subprocess solver finished signal (fallback only)
    m_solverConn = connect(m_solver, &SolverRunner::finished,
                           this, &MotionRunner::onSolverFinished);

    // Motor diagnostic dump at sweep start
    if (m_config.motorEnabled) {
        emit progress(tr("=== Motor Config ==="));
        emit progress(tr("  optimalAngle = %1°").arg(m_config.motorOptimalAngle, 0, 'f', 2));
        emit progress(tr("  polePairs = %1").arg(m_config.motorPolePairs));
        emit progress(tr("  rmsCurrent = %1 A").arg(m_config.motorRmsCurrent, 0, 'f', 3));
        emit progress(tr("  stepAngle = %1° mech").arg(m_config.angle, 0, 'f', 3));
        emit progress(tr("  numSteps = %1").arg(m_config.numSteps));
        emit progress(tr("  reversePhase = %1").arg(m_config.motorReversePhase ? "true" : "false"));
        emit progress(tr("  rotationCenter = (%1, %2)")
            .arg(m_config.cx, 0, 'f', 3).arg(m_config.cy, 0, 'f', 3));
        emit progress(tr("  phaseA=%1  phaseB=%2  phaseC=%3")
            .arg(m_config.motorPhaseA, m_config.motorPhaseB, m_config.motorPhaseC));
        emit progress(tr("  group = %1").arg(m_config.groupNumber));
        // Dump circuit properties
        for (int ci = 0; ci < (int)doc->circuitProps.size(); ci++) {
            emit progress(tr("  Circuit[%1] '%2': amps=(%3, %4) type=%5")
                .arg(ci).arg(doc->circuitProps[ci].circName)
                .arg(doc->circuitProps[ci].amps.re, 0, 'f', 4)
                .arg(doc->circuitProps[ci].amps.im, 0, 'f', 4)
                .arg(doc->circuitProps[ci].circType));
        }
        // Dump winding slot block labels (those with non-zero circuit)
        for (int bi = 0; bi < (int)doc->blockLabels.size(); bi++) {
            const auto &bl = doc->blockLabels[bi];
            if (bl.inCircuit != "<None>" && bl.turns != 0) {
                double ang = std::atan2(bl.y, bl.x) * 180.0 / M_PI;
                if (ang < 0) ang += 360.0;
                emit progress(tr("  Slot[%1] (%2,%3) angle=%4° circuit='%5' turns=%6 group=%7")
                    .arg(bi).arg(bl.x, 0, 'f', 1).arg(bl.y, 0, 'f', 1)
                    .arg(ang, 0, 'f', 1).arg(bl.inCircuit).arg(bl.turns).arg(bl.inGroup));
            }
        }
        emit progress(tr("=== End Motor Config ==="));
    }

    // Run step 0 (initial position — no transform, just solve)
    runNextStep();
}

void MotionRunner::abort()
{
    m_aborting = true;
    if (m_solveThread && m_solveThread->isRunning()) {
        // Thread will finish and onInProcessSolveFinished will handle cleanup
        return;
    }
    if (m_solver->isRunning()) {
        // Solver will finish and we'll catch it in onSolverFinished
        return;
    }
    disconnect(m_solverConn);
    if (m_dw) {
        m_dw->setResultsOverlay(nullptr);
        m_dw->setViewLocked(false);
    }
    restoreGeometry();
    m_running = false;
    m_frames.clear();
    m_deferredPNGs.clear();
    m_bHistory.clear();
    delete m_pendingResults;
    m_pendingResults = nullptr;
    emit progress(tr("Motion sweep aborted."));
    emit finished(false, QString(), QString());
}

void MotionRunner::runNextStep()
{
    if (m_aborting) {
        disconnect(m_solverConn);
        if (m_dw) m_dw->setViewLocked(false);
        restoreGeometry();
        m_running = false;
        emit finished(false, QString(), QString());
        return;
    }

    emit progress(QString("Step %1 of %2...")
        .arg(m_currentStep).arg(m_config.numSteps));
    emit stepCompleted(m_currentStep, m_config.numSteps);

    // Apply transform (skip for step 0 — initial position)
    if (m_currentStep > 0) {
        m_doc->selectGroup(m_config.groupNumber);
        if (m_config.isRotation)
            m_doc->rotateSelected(m_config.cx, m_config.cy, m_config.angle);
        else
            m_doc->translateSelected(m_config.dx, m_config.dy);
        m_doc->deselectAll();
    }

    // Apply motor 3-phase currents if motor module is enabled
    if (m_config.motorEnabled) {
        double sign = m_config.motorReversePhase ? -1.0 : 1.0;
        double elecAngle;
        if (m_config.isRotation) {
            // Rotary motor: electrical angle tracks rotor position.
            // As the rotor advances, the stator electrical angle must
            // track to maintain the optimal torque-producing relative
            // angle.  The sign depends on the relationship between
            // rotor mechanical advance and stator field direction.
            double cumMechAngle = m_config.angle * (double)m_currentStep;
            elecAngle = m_config.motorOptimalAngle
                      - sign * cumMechAngle * (double)m_config.motorPolePairs;
        } else {
            // Linear motor: same sign convention
            double stepDist = std::sqrt(m_config.dx * m_config.dx
                                       + m_config.dy * m_config.dy);
            double cumDist = stepDist * (double)m_currentStep;
            double polePitch = m_config.motorPolePitch;
            if (polePitch < 1e-12) polePitch = 1e-12;
            elecAngle = m_config.motorOptimalAngle
                      - sign * (cumDist / polePitch) * 360.0;
        }
        auto currents = MotorOptimizer::computeCurrents(
            m_config.motorRmsCurrent, elecAngle);
        MotorOptimizer::applyCurrents(
            m_doc, currents,
            m_config.motorPhaseA, m_config.motorPhaseB, m_config.motorPhaseC);

        // Cache for StepResult after solve completes
        m_stepElecAngle = elecAngle;
        m_stepIa = currents.Ia;
        m_stepIb = currents.Ib;
        m_stepIc = currents.Ic;

        // Log ALL steps for debugging motor commutation
        emit progress(tr("  Step %1: elecAngle=%2° Ia=%3 Ib=%4 Ic=%5")
            .arg(m_currentStep)
            .arg(elecAngle, 0, 'f', 1)
            .arg(currents.Ia, 0, 'f', 3)
            .arg(currents.Ib, 0, 'f', 3)
            .arg(currents.Ic, 0, 'f', 3));
    }

    // In-process solve in a worker thread (keeps UI responsive)
    m_pendingResults = new ResultsDocument(this);
    m_solveThread = new SolveThread;
    m_solveThread->solver = m_inProcessSolver;
    m_solveThread->doc = m_doc;
    m_solveThread->results = m_pendingResults;
    connect(m_solveThread, &QThread::finished,
            this, &MotionRunner::onInProcessSolveFinished);
    m_solveThread->start();
    // Returns to event loop — onInProcessSolveFinished handles the rest
}

void MotionRunner::onInProcessSolveFinished()
{
    bool success = m_solveThread->success;
    m_solveThread->deleteLater();
    m_solveThread = nullptr;

    ResultsDocument *rdoc = m_pendingResults;
    m_pendingResults = nullptr;

    if (m_aborting) {
        delete rdoc;
        disconnect(m_solverConn);
        if (m_dw) {
            m_dw->setResultsOverlay(nullptr);
            m_dw->setViewLocked(false);
        }
        restoreGeometry();
        m_running = false;
        emit finished(false, QString(), QString());
        return;
    }

    if (!success) {
        emit progress(tr("Solve failed at step %1: %2")
            .arg(m_currentStep).arg(m_inProcessSolver->lastError()));
        delete rdoc;
        disconnect(m_solverConn);
        if (m_dw) {
            m_dw->setResultsOverlay(nullptr);
            m_dw->setViewLocked(false);
        }
        restoreGeometry();
        m_running = false;
        emit finished(false, QString(), QString());
        return;
    }

    // Update overlay for visualization
    if (m_overlay) {
        m_overlay->setDocument(rdoc);
    }

    // Compute summary
    ResultsSummary summary = rdoc->computeSummary();

    StepResult sr;
    sr.step = m_currentStep;
    if (m_config.isRotation) {
        sr.cumDx = 0.0;
        sr.cumDy = 0.0;
        sr.cumAngle = m_config.angle * (double)m_currentStep;
    } else {
        sr.cumDx = m_config.dx * (double)m_currentStep;
        sr.cumDy = m_config.dy * (double)m_currentStep;
        sr.cumAngle = 0.0;
    }
    sr.summary = summary;
    sr.elecAngleDeg = m_stepElecAngle;
    sr.Ia = m_stepIa;
    sr.Ib = m_stepIb;
    sr.Ic = m_stepIc;

    // Maxwell stress tensor torque (motor + rotation mode)
    if (m_config.motorEnabled && m_config.isRotation && m_config.csvForceTorque) {
        sr.instantTorque = rdoc->computeTorque(
            m_config.cx, m_config.cy, m_config.groupNumber);
        sr.hasTorque = true;
    }

    m_results.push_back(sr);

    // Detailed per-step diagnostic for motor debugging
    if (m_config.motorEnabled) {
        // On first step, dump torque-relevant parameters
        if (m_currentStep == 0 && rdoc) {
            emit progress(tr("  [TorqueDiag] depth=%1, lengthConv=%2, depth_m=%3")
                .arg(rdoc->depth, 0, 'f', 4)
                .arg(rdoc->lengthConv, 0, 'e', 4)
                .arg(rdoc->depth * rdoc->lengthConv, 0, 'f', 6));
            // Count boundary elements and sample B values
            int boundaryElms = 0;
            double maxB = 0;
            for (size_t e = 0; e < rdoc->elements.size(); e++) {
                double Bx = rdoc->elements[e].B1.real();
                double By = rdoc->elements[e].B2.real();
                double Bmag = std::sqrt(Bx*Bx + By*By);
                if (Bmag > maxB) maxB = Bmag;
            }
            emit progress(tr("  [TorqueDiag] numElements=%1, maxB=%2 T, group=%3, center=(%4,%5)")
                .arg(rdoc->elements.size())
                .arg(maxB, 0, 'f', 4)
                .arg(m_config.groupNumber)
                .arg(m_config.cx, 0, 'f', 3)
                .arg(m_config.cy, 0, 'f', 3));
        }
        QString diag = tr("  → Step %1: E=%2 J")
            .arg(m_currentStep - 0)  // step just solved (m_currentStep hasn't incremented yet)
            .arg(summary.totalEnergy, 0, 'f', 6);
        if (sr.hasTorque)
            diag += tr(", MSTorque=%1 Nm").arg(sr.instantTorque, 0, 'f', 6);
        // Energy-difference torque (compare with MST)
        if (m_results.size() >= 2) {
            double dTheta_mech = m_config.angle * M_PI / 180.0;
            int n = (int)m_results.size();
            double dE = m_results[n-1].summary.totalEnergy - m_results[n-2].summary.totalEnergy;
            double vwTorque = -dE / dTheta_mech;
            diag += tr(", VW_dE=%1, VWTorque=%2 Nm").arg(dE, 0, 'f', 6).arg(vwTorque, 0, 'f', 6);
        }
        emit progress(diag);
    }

    // Capture B-field snapshot for iron loss computation
    if (m_config.calculateLosses) {
        BSnapshot snap;
        int lossElmCount = 0;
        for (int i = 0; i < (int)rdoc->elements.size(); i++) {
            const auto &elm = rdoc->elements[i];
            int lbl = elm.lbl;
            if (lbl < 0 || lbl >= (int)rdoc->labels.size()) continue;
            if (!rdoc->labels[lbl].calculateLosses) continue;
            lossElmCount++;
            // Compute Az at centroid as average of 3 nodal values
            float azCentroid = 0.0f;
            if (elm.p[0] >= 0 && elm.p[0] < (int)rdoc->nodes.size() &&
                elm.p[1] >= 0 && elm.p[1] < (int)rdoc->nodes.size() &&
                elm.p[2] >= 0 && elm.p[2] < (int)rdoc->nodes.size()) {
                azCentroid = (float)((rdoc->nodes[elm.p[0]].A.real() +
                                      rdoc->nodes[elm.p[1]].A.real() +
                                      rdoc->nodes[elm.p[2]].A.real()) / 3.0);
            }
            snap.add((float)elm.cx, (float)elm.cy,
                     (float)elm.B1.real(), (float)elm.B2.real(), azCentroid);
        }
        m_bHistory.push_back(std::move(snap));

        // Diagnostic at first step
        if (m_currentStep == 0) {
            emit progress(tr("Iron loss: %1/%2 elements have calculateLosses enabled")
                .arg(lossElmCount).arg(rdoc->elements.size()));
            if (lossElmCount == 0) {
                emit progress(tr("WARNING: No block labels have 'Calculate iron losses' enabled.\n"
                                  "Enable it in Properties → Block Labels for iron/steel regions."));
            }
            int matWithLoss = 0;
            int matConductiveNoDensity = 0;
            for (int m = 0; m < (int)rdoc->materials.size(); m++) {
                const auto &mat = rdoc->materials[m];
                bool hasSteinmetz = (mat.Kh > 0 || mat.Kc > 0 || mat.Ke > 0);
                bool hasConductivity = (mat.Cduct > 0);
                if ((hasSteinmetz || hasConductivity) && mat.density > 0)
                    matWithLoss++;
                if (hasConductivity && mat.density <= 0)
                    matConductiveNoDensity++;
            }
            emit progress(tr("Iron loss: %1/%2 materials have loss data + density")
                .arg(matWithLoss).arg(rdoc->materials.size()));
            if (matConductiveNoDensity > 0) {
                emit progress(tr("WARNING: %1 conductive material(s) missing density — "
                                  "eddy losses will be skipped. Set density in Material Properties.")
                    .arg(matConductiveNoDensity));
            }
            if (matWithLoss == 0 && lossElmCount > 0) {
                emit progress(tr("WARNING: No materials have loss data.\n"
                                  "Set Kh/Kc/Ke + density (steel), or sigma + density (solid conductors)."));
            }
        }
    }

    // Clear the cached frame so paintEvent does a live render
    if (m_dw)
        m_dw->clearCachedFrame();

    // Synchronously repaint and capture frame
    m_dw->repaint();
    QPixmap frame = captureFrame();

    // Cache frame for display while next step runs
    if (m_dw)
        m_dw->setCachedFrame(frame);

    // Advance to next step
    m_currentStep++;
    if (m_currentStep > m_config.numSteps) {
        // All steps complete
        disconnect(m_solverConn);

        // Compute iron losses from B history if enabled
        if (m_config.calculateLosses && !m_bHistory.empty() && m_overlay && m_overlay->document()) {
            ResultsDocument *lastDoc = m_overlay->document();
            double freq = m_config.operatingFreqHz;
            // Auto-derive from RPM if freq not set but RPM is
            if (freq <= 0 && m_config.motorRPM > 0 && m_config.motorPolePairs > 0)
                freq = m_config.motorRPM * (double)m_config.motorPolePairs / 60.0;

            if (freq > 0) {
                emit progress(tr("Computing iron losses at %.1f Hz...").arg(freq));

                // Build motion params for rotor-aware B(t) lookup
                MotionParams motionParams;
                motionParams.movingGroup = m_config.groupNumber;
                motionParams.isRotation = m_config.isRotation;
                motionParams.cx = m_config.cx;
                motionParams.cy = m_config.cy;
                motionParams.anglePerStep = m_config.angle;
                motionParams.dx = m_config.dx;
                motionParams.dy = m_config.dy;
                motionParams.rpm = m_config.motorRPM;
                motionParams.totalSteps = m_config.numSteps;

                // depth is stored in original length units — convert to metres
                double depthM = lastDoc->depth * lastDoc->lengthConv;
                m_ironLossResult = computeIronLosses(
                    m_bHistory, lastDoc, freq, depthM, motionParams);

                // Populate per-element loss data on the ResultsDocument for heatmap
                int numElm = (int)lastDoc->elements.size();
                lastDoc->ironLoss_Wkg.resize(numElm, 0.0);
                lastDoc->ironLoss_Low = 0.0;
                lastDoc->ironLoss_High = 0.0;
                for (int i = 0; i < numElm && i < (int)m_ironLossResult.elementLosses.size(); i++) {
                    lastDoc->ironLoss_Wkg[i] = m_ironLossResult.elementLosses[i].loss_Wkg;
                    if (lastDoc->ironLoss_Wkg[i] > lastDoc->ironLoss_High)
                        lastDoc->ironLoss_High = lastDoc->ironLoss_Wkg[i];
                }

                // Report total loss
                if (m_ironLossResult.valid) {
                    emit progress(tr("Total iron loss: %1 W")
                        .arg(m_ironLossResult.totalLoss_W, 0, 'f', 3));
                    for (const auto &bs : m_ironLossResult.blockSummaries) {
                        emit progress(tr("  Block %1 (%2): %3 W, avg %4 W/kg, peak B=%5 T")
                            .arg(bs.blockIndex)
                            .arg(bs.materialName)
                            .arg(bs.totalLoss_W, 0, 'f', 3)
                            .arg(bs.avgLoss_Wkg, 0, 'f', 2)
                            .arg(bs.avgBpeak, 0, 'f', 3));
                    }
                }
                // Capture iron loss heatmap to offscreen image
                if (m_ironLossResult.valid && m_overlay && m_dw) {
                    // Save and set overlay to iron loss mode
                    DensityType prevDensity = m_overlay->densityType();
                    bool prevLegend = m_overlay->showLegend();
                    m_overlay->setShowDensity(DensityType::IronLoss);
                    m_overlay->setShowLegend(true);

                    // Render to offscreen QImage using the current view transform
                    int w = m_dw->width();
                    int h = m_dw->height();
                    if (w > 0 && h > 0) {
                        QImage offscreen(w, h, QImage::Format_ARGB32_Premultiplied);
                        offscreen.fill(Qt::white);
                        QPainter painter(&offscreen);
                        painter.setRenderHint(QPainter::Antialiasing);
                        m_overlay->render(painter, m_dw->viewOx(), m_dw->viewOy(),
                                          m_dw->viewMag(), w, h);
                        painter.end();

                        QString heatmapPath = QString("%1/ironloss_%2.png")
                            .arg(m_config.outputDir, m_timestamp);
                        offscreen.save(heatmapPath, "PNG");
                        emit progress(tr("Iron loss heatmap saved: %1").arg(heatmapPath));
                    }

                    // Restore previous overlay state
                    m_overlay->setShowDensity(prevDensity);
                    m_overlay->setShowLegend(prevLegend);
                }
            } else {
                emit progress(tr("Iron loss skipped: operating frequency is 0 Hz.\n"
                                  "Set RPM or frequency in the motion dialog."));
            }
        }

        if (m_config.saveCSV)
            writeCSV();
        if (m_config.saveVideo)
            assembleGIF();

        // Write deferred PNG frames to disk
        if (!m_deferredPNGs.empty()) {
            emit progress(tr("Writing %1 PNG frames...").arg(m_deferredPNGs.size()));
            for (const auto &df : m_deferredPNGs)
                df.pixmap.save(df.path, "PNG");
            m_deferredPNGs.clear();
        }

        // Save the last step's ResultsDocument (with iron loss data) so
        // MainWindow can take ownership and display it as an overlay after
        // the sweep.  Re-parent it away from MotionRunner so it survives.
        delete m_lastResultsDoc;
        m_lastResultsDoc = nullptr;
        if (m_overlay && m_overlay->document()) {
            m_lastResultsDoc = m_overlay->document();
            m_lastResultsDoc->setParent(nullptr);  // detach from MotionRunner
        }

        // Clear overlay, cached frame, and unlock view before restoring
        // geometry.  The overlay MUST be detached from the DrawingWidget
        // before emitting finished(), because MainWindow will delete the
        // renderer.  If it relied on currentDrawing() and that returned
        // nullptr (e.g. MDI focus lost), the widget would keep a dangling
        // m_overlay pointer → crash during QMessageBox repaint.
        if (m_dw) {
            m_dw->setResultsOverlay(nullptr);
            m_dw->clearCachedFrame();
            m_dw->setViewLocked(false);
        }
        restoreGeometry();

        m_running = false;

        QString csvPath = QString("%1/results_%2.csv")
            .arg(m_config.outputDir, m_timestamp);
        QString gifPath = QString("%1/animation_%2.gif")
            .arg(m_config.outputDir, m_timestamp);
        if (!QFile::exists(gifPath))
            gifPath.clear();

        emit progress(tr("Motion sweep complete: %1 steps.").arg(m_config.numSteps));
        emit finished(true, csvPath, gifPath);
    } else {
        // Schedule next step via event loop (keeps UI responsive between steps)
        QTimer::singleShot(0, this, &MotionRunner::runNextStep);
    }
}

void MotionRunner::onSolverFinished(bool success)
{
    if (!m_running) return;

    if (m_aborting || !success) {
        disconnect(m_solverConn);
        if (m_dw) {
            m_dw->setResultsOverlay(nullptr);
            m_dw->setViewLocked(false);
        }
        restoreGeometry();
        m_running = false;
        if (m_aborting)
            emit finished(false, QString(), QString());
        else {
            emit progress(tr("Solver failed at step %1").arg(m_currentStep));
            emit finished(false, QString(), QString());
        }
        return;
    }

    // Load results
    QString ansPath = m_doc->filePath();
    if (ansPath.endsWith(".fem", Qt::CaseInsensitive))
        ansPath = ansPath.left(ansPath.length() - 4) + ".ans";
    else
        ansPath += ".ans";

    auto *rdoc = new ResultsDocument(this);
    if (!rdoc->loadFromFile(ansPath)) {
        emit progress(tr("Failed to load results at step %1").arg(m_currentStep));
        delete rdoc;
        disconnect(m_solverConn);
        if (m_dw) m_dw->setViewLocked(false);
        restoreGeometry();
        m_running = false;
        emit finished(false, QString(), QString());
        return;
    }

    // Update overlay for visualization
    if (m_overlay) {
        m_overlay->setDocument(rdoc);
    }

    // Compute summary
    ResultsSummary summary = rdoc->computeSummary();

    StepResult sr;
    sr.step = m_currentStep;
    if (m_config.isRotation) {
        sr.cumDx = 0.0;
        sr.cumDy = 0.0;
        sr.cumAngle = m_config.angle * (double)m_currentStep;
    } else {
        sr.cumDx = m_config.dx * (double)m_currentStep;
        sr.cumDy = m_config.dy * (double)m_currentStep;
        sr.cumAngle = 0.0;
    }
    sr.summary = summary;

    // --- Maxwell stress tensor torque (motor + rotation mode) ---
    // The standard virtual-work method (dE between successive steps) is
    // invalid when currents change between steps.  Use the Henrotte
    // weighted stress tensor instead — computed from a single solution,
    // no extra solve needed.
    if (m_config.motorEnabled && m_config.isRotation && m_config.csvForceTorque) {
        sr.instantTorque = rdoc->computeTorque(
            m_config.cx, m_config.cy, m_config.groupNumber);
        sr.hasTorque = true;
    }

    m_results.push_back(sr);

    // Capture B-field snapshot for iron loss computation
    if (m_config.calculateLosses) {
        BSnapshot snap;
        for (int i = 0; i < (int)rdoc->elements.size(); i++) {
            const auto &elm = rdoc->elements[i];
            int lbl = elm.lbl;
            if (lbl < 0 || lbl >= (int)rdoc->labels.size()) continue;
            if (!rdoc->labels[lbl].calculateLosses) continue;
            float azCentroid = 0.0f;
            if (elm.p[0] >= 0 && elm.p[0] < (int)rdoc->nodes.size() &&
                elm.p[1] >= 0 && elm.p[1] < (int)rdoc->nodes.size() &&
                elm.p[2] >= 0 && elm.p[2] < (int)rdoc->nodes.size()) {
                azCentroid = (float)((rdoc->nodes[elm.p[0]].A.real() +
                                      rdoc->nodes[elm.p[1]].A.real() +
                                      rdoc->nodes[elm.p[2]].A.real()) / 3.0);
            }
            snap.add((float)elm.cx, (float)elm.cy,
                     (float)elm.B1.real(), (float)elm.B2.real(), azCentroid);
        }
        m_bHistory.push_back(std::move(snap));
    }

    // Clear the cached frame so paintEvent does a live render with the
    // new overlay + current geometry (which are now in sync for this step).
    if (m_dw)
        m_dw->clearCachedFrame();

    // Synchronously repaint the widget so the on-screen display shows the
    // correct overlay + geometry for this step, then capture the frame.
    m_dw->repaint();
    QPixmap frame = captureFrame();

    // Cache this frame: while geometry is moved for the next step and the
    // solver runs, any repaint will draw this snapshot instead of the
    // live (out-of-sync) state.  The cached frame keeps the display
    // showing the correct overlay+geometry until the next solver finishes.
    if (m_dw)
        m_dw->setCachedFrame(frame);

    // Advance to next step
    m_currentStep++;
    if (m_currentStep > m_config.numSteps) {
        // All steps complete
        disconnect(m_solverConn);

        if (m_config.saveCSV)
            writeCSV();
        if (m_config.saveVideo)
            assembleGIF();

        // Write deferred PNG frames to disk
        if (!m_deferredPNGs.empty()) {
            emit progress(tr("Writing %1 PNG frames...").arg(m_deferredPNGs.size()));
            for (const auto &df : m_deferredPNGs)
                df.pixmap.save(df.path, "PNG");
            m_deferredPNGs.clear();
        }

        // Clear overlay, cached frame, and unlock view before restoring
        // geometry (see matching comment in onInProcessSolveFinished).
        if (m_dw) {
            m_dw->setResultsOverlay(nullptr);
            m_dw->clearCachedFrame();
            m_dw->setViewLocked(false);
        }
        restoreGeometry();

        m_running = false;

        QString csvPath = QString("%1/results_%2.csv")
            .arg(m_config.outputDir, m_timestamp);
        QString gifPath = QString("%1/animation_%2.gif")
            .arg(m_config.outputDir, m_timestamp);
        if (!QFile::exists(gifPath))
            gifPath.clear();

        emit progress(tr("Motion sweep complete: %1 steps.").arg(m_config.numSteps));
        emit finished(true, csvPath, gifPath);
    } else {
        runNextStep();
    }
}

QPixmap MotionRunner::captureFrame()
{
    QPixmap pixmap = m_dw->grab();

    // Defer PNG frame save to end of run (avoid per-step disk I/O)
    if (m_config.saveImages) {
        QString framePath = QString("%1/frame_%2_%3.png")
            .arg(m_config.outputDir, m_timestamp)
            .arg(m_currentStep, 4, 10, QChar('0'));
        m_deferredPNGs.push_back({pixmap, framePath});
    }

    // Store full-resolution image for GIF assembly if video saving is enabled.
    // When loop playback is on, skip the last frame (step == numSteps) so the
    // GIF loops seamlessly without a duplicate/stutter at the wrap point.
    if (m_config.saveVideo) {
        bool isLastStep = (m_currentStep == m_config.numSteps);
        if (!m_config.loopPlayback || !isLastStep) {
            m_frames.push_back(pixmap.toImage());
        }
    }

    return pixmap;
}

void MotionRunner::writeCSV()
{
    QString csvPath = QString("%1/results_%2.csv")
        .arg(m_config.outputDir, m_timestamp);
    QFile file(csvPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit progress(tr("Could not write CSV: %1").arg(csvPath));
        return;
    }

    QTextStream out(&file);

    // --- Header (position columns always included) ---
    // NOTE: column order must match data row order below
    QStringList hdr;
    hdr << "Step" << "Displacement_X" << "Displacement_Y" << "Angle_deg";
    if (m_config.motorEnabled)
        hdr << "ElecAngle_deg" << "Ia_A" << "Ib_A" << "Ic_A";
    if (m_config.csvFluxDensity)
        hdr << "B_max_T" << "B_min_T" << "B_avg_T";
    if (m_config.csvVectorPotential)
        hdr << "A_max_Wb_m" << "A_min_Wb_m";
    if (m_config.csvEnergy)
        hdr << "Energy_J" << "Area_m2";
    if (m_config.csvForceTorque)
        hdr << "Force_X_N" << "Force_Y_N" << "Torque_Nm";
    if (m_config.csvIronLoss && m_ironLossResult.valid) {
        hdr << "IronLoss_Total_W";
        for (const auto &bs : m_ironLossResult.blockSummaries)
            hdr << QString("IronLoss_%1_W").arg(bs.materialName.isEmpty()
                    ? QString("Block%1").arg(bs.blockIndex) : bs.materialName);
    }
    out << hdr.join(",") << "\n";

    // --- Pre-compute force/torque for all steps ---
    // Motor rotation mode: use per-step probe-solve torque (instantTorque).
    // Non-motor mode: use the standard virtual-work energy-difference method.
    std::vector<double> forceX(m_results.size(), 0.0);
    std::vector<double> forceY(m_results.size(), 0.0);
    std::vector<double> torque(m_results.size(), 0.0);
    std::vector<bool> hasForce(m_results.size(), false);

    bool useProbe = m_config.motorEnabled && m_config.isRotation;

    if (m_config.csvForceTorque && useProbe) {
        // Motor mode: use instantaneous torque from probe solves
        for (int i = 0; i < (int)m_results.size(); i++) {
            if (m_results[i].hasTorque) {
                torque[i] = m_results[i].instantTorque;
                hasForce[i] = true;
            }
        }
    } else if (m_config.csvForceTorque && m_results.size() > 1) {
        // Standard virtual-work method (energy differences)
        if (m_config.isRotation) {
            double dTheta = m_config.angle * M_PI / 180.0;
            for (int i = 0; i < (int)m_results.size(); i++) {
                if (i > 0 && i < (int)m_results.size() - 1) {
                    torque[i] = -(m_results[i+1].summary.totalEnergy
                                - m_results[i-1].summary.totalEnergy) / (2.0 * dTheta);
                } else if (i == 0) {
                    torque[i] = -(m_results[i+1].summary.totalEnergy
                                - m_results[i].summary.totalEnergy) / dTheta;
                } else {
                    torque[i] = -(m_results[i].summary.totalEnergy
                                - m_results[i-1].summary.totalEnergy) / dTheta;
                }
                hasForce[i] = true;
            }
        } else {
            double stepSize = std::sqrt(m_config.dx * m_config.dx
                                      + m_config.dy * m_config.dy);
            if (stepSize < 1e-30) stepSize = 1e-30;
            for (int i = 0; i < (int)m_results.size(); i++) {
                double f = 0.0;
                if (i > 0 && i < (int)m_results.size() - 1) {
                    f = -(m_results[i+1].summary.totalEnergy
                        - m_results[i-1].summary.totalEnergy) / (2.0 * stepSize);
                } else if (i == 0) {
                    f = -(m_results[i+1].summary.totalEnergy
                        - m_results[i].summary.totalEnergy) / stepSize;
                } else {
                    f = -(m_results[i].summary.totalEnergy
                        - m_results[i-1].summary.totalEnergy) / stepSize;
                }
                forceX[i] = f * m_config.dx / stepSize;
                forceY[i] = f * m_config.dy / stepSize;
                hasForce[i] = true;
            }
        }
    }

    // --- Data rows ---
    for (int i = 0; i < (int)m_results.size(); i++) {
        const auto &r = m_results[i];
        QStringList row;

        // Position columns (always)
        row << QString::number(r.step)
            << QString::number(r.cumDx, 'e', 6)
            << QString::number(r.cumDy, 'e', 6)
            << QString::number(r.cumAngle, 'f', 4);

        if (m_config.motorEnabled) {
            row << QString::number(r.elecAngleDeg, 'f', 2)
                << QString::number(r.Ia, 'f', 4)
                << QString::number(r.Ib, 'f', 4)
                << QString::number(r.Ic, 'f', 4);
        }
        if (m_config.csvFluxDensity) {
            row << QString::number(r.summary.B_max, 'e', 6)
                << QString::number(r.summary.B_min, 'e', 6)
                << QString::number(r.summary.B_avg, 'e', 6);
        }
        if (m_config.csvVectorPotential) {
            row << QString::number(r.summary.A_max, 'e', 6)
                << QString::number(r.summary.A_min, 'e', 6);
        }
        if (m_config.csvEnergy) {
            row << QString::number(r.summary.totalEnergy, 'e', 6)
                << QString::number(r.summary.totalArea, 'e', 6);
        }
        if (m_config.csvForceTorque) {
            if (hasForce[i]) {
                row << QString::number(forceX[i], 'e', 6)
                    << QString::number(forceY[i], 'e', 6)
                    << QString::number(torque[i], 'e', 6);
            } else {
                row << "" << "" << "";
            }
        }

        if (m_config.csvIronLoss && m_ironLossResult.valid) {
            row << QString::number(m_ironLossResult.totalLoss_W, 'e', 6);
            for (const auto &bs : m_ironLossResult.blockSummaries)
                row << QString::number(bs.totalLoss_W, 'e', 6);
        }

        out << row.join(",") << "\n";
    }

    file.close();
}

void MotionRunner::assembleGIF()
{
    if (m_frames.empty()) {
        emit progress(tr("No frames captured — skipping GIF creation."));
        return;
    }

    QString gifPath = QString("%1/animation_%2.gif")
        .arg(m_config.outputDir, m_timestamp);

    // 10 centiseconds = 100ms per frame = 10 fps
    QByteArray gifData = encodeAnimatedGIF(m_frames, 10);

    QFile file(gifPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit progress(tr("Could not write GIF: %1").arg(gifPath));
        return;
    }
    file.write(gifData);
    file.close();

    emit progress(tr("Animation created: %1 (%2 frames)")
        .arg(gifPath).arg(m_frames.size()));

    // Free frame memory
    m_frames.clear();
}

void MotionRunner::restoreGeometry()
{
    if (!m_doc) return;

    m_doc->nodes = m_backupNodes;
    m_doc->segments = m_backupSegments;
    m_doc->arcSegments = m_backupArcs;
    m_doc->blockLabels = m_backupLabels;
    if (m_config.motorEnabled)
        m_doc->circuitProps = m_backupCircuits;
    m_doc->isModified = true;

    // Save restored geometry and generate mesh files on disk.
    // generateMesh() writes .poly/.node/.ele/.edge so the external solver
    // can re-solve on the correct geometry when the user clicks Analyze.
    m_doc->saveToFile(m_doc->filePath());
    m_meshGen->generateMesh(m_doc);

    // Now overwrite the in-memory mesh with the adaptive-refined backup.
    // The disk files have a valid (freshly generated) mesh for the solver,
    // while in-memory we keep the adaptive mesh the user expects to see.
    if (m_backupHasMesh) {
        m_doc->meshNodes = m_backupMeshNodes;
        m_doc->meshElements = m_backupMeshElements;
        m_doc->meshEdges = m_backupMeshEdges;
        m_doc->hasMesh = true;
    }

    // Unfreeze the entire window hierarchy, clear cached frame, restore view
    if (m_dw) {
        m_dw->clearCachedFrame();

        QWidget *topWin = m_dw->window();
        if (topWin) {
            topWin->setMinimumSize(m_backupTopMinSize);
            topWin->setMaximumSize(m_backupTopMaxSize);
        }
        QWidget *subWin = m_dw->parentWidget();
        if (subWin) {
            subWin->setMinimumSize(m_backupSubMinSize);
            subWin->setMaximumSize(m_backupSubMaxSize);
        }
        m_dw->setMinimumSize(m_backupMinSize);
        m_dw->setMaximumSize(m_backupMaxSize);
        m_dw->setViewTransform(m_backupViewOx, m_backupViewOy, m_backupViewMag);
    }
}
