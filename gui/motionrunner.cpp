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

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QBuffer>
#include <QDir>
#include <QPixmap>
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
        double sign = m_config.motorReversePhase ? 1.0 : -1.0;
        double elecAngle;
        if (m_config.isRotation) {
            // Rotary motor: electrical angle = mechanical angle × pole pairs
            double cumMechAngle = m_config.angle * (double)m_currentStep;
            elecAngle = m_config.motorOptimalAngle
                      + sign * cumMechAngle * (double)m_config.motorPolePairs;
        } else {
            // Linear motor: electrical angle = displacement / pole pitch × 360°
            double stepDist = std::sqrt(m_config.dx * m_config.dx
                                       + m_config.dy * m_config.dy);
            double cumDist = stepDist * (double)m_currentStep;
            double polePitch = m_config.motorPolePitch;
            if (polePitch < 1e-12) polePitch = 1e-12;
            elecAngle = m_config.motorOptimalAngle
                      + sign * (cumDist / polePitch) * 360.0;
        }
        auto currents = MotorOptimizer::computeCurrents(
            m_config.motorRmsCurrent, elecAngle);
        MotorOptimizer::applyCurrents(
            m_doc, currents,
            m_config.motorPhaseA, m_config.motorPhaseB, m_config.motorPhaseC);
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

    // Maxwell stress tensor torque (motor + rotation mode)
    if (m_config.motorEnabled && m_config.isRotation && m_config.csvForceTorque) {
        sr.instantTorque = rdoc->computeTorque(
            m_config.cx, m_config.cy, m_config.groupNumber);
        sr.hasTorque = true;
    }

    m_results.push_back(sr);

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
    QStringList hdr;
    hdr << "Step" << "Displacement_X" << "Displacement_Y" << "Angle_deg";
    if (m_config.csvFluxDensity)
        hdr << "B_max_T" << "B_min_T" << "B_avg_T";
    if (m_config.csvVectorPotential)
        hdr << "A_max_Wb_m" << "A_min_Wb_m";
    if (m_config.csvEnergy)
        hdr << "Energy_J" << "Area_m2";
    if (m_config.csvForceTorque)
        hdr << "Force_X_N" << "Force_Y_N" << "Torque_Nm";
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
