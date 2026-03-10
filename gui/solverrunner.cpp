// FEMM Qt 6 GUI — Solver process runner implementation
#include "solverrunner.h"
#include "document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <cstdio>

SolverRunner::SolverRunner(QObject *parent)
    : QObject(parent)
{
}

SolverRunner::~SolverRunner()
{
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(3000);
        delete m_process;
    }
}

bool SolverRunner::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void SolverRunner::runSolver(FemmeDocument *doc)
{
    if (!doc) {
        m_lastError = "No document";
        emit finished(false);
        return;
    }

    QString femPath = doc->filePath();
    if (femPath.isEmpty()) {
        m_lastError = "Document must be saved before analyzing";
        emit finished(false);
        return;
    }

    // Strip .fem extension to get base path for solver
    QString basePath = femPath;
    if (basePath.endsWith(".fem", Qt::CaseInsensitive))
        basePath.chop(4);

    // Find solver executable
    QString solverPath = m_solverPath;
    if (solverPath.isEmpty()) {
        // App is at: build/gui/femm-gui.app/Contents/MacOS/femm-gui
        // Solver is at: build/fkn/fkn
        QDir appDir(QCoreApplication::applicationDirPath());
        QString buildDir = QDir::cleanPath(appDir.absolutePath() + "/../../../..");

        // Check common locations for fkn (magnetics solver)
        QStringList candidates = {
            appDir.filePath("fkn"),
            buildDir + "/fkn/fkn",
            QDir::cleanPath(appDir.absolutePath() + "/../../../../fkn/fkn"),
            QDir::cleanPath(appDir.absolutePath() + "/../fkn"),
        };

        for (const auto &c : candidates) {
            if (QFileInfo::exists(c)) {
                solverPath = QFileInfo(c).absoluteFilePath();
                break;
            }
        }

        if (solverPath.isEmpty()) {
            m_lastError = QString("Cannot find fkn solver executable. Searched:\n%1")
                .arg(candidates.join("\n"));
            emit finished(false);
            return;
        }
    }

    // Clean up old process
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(3000);
        delete m_process;
    }

    m_process = new QProcess(this);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &SolverRunner::onProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &SolverRunner::onProcessOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SolverRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &SolverRunner::onProcessError);

    // fkn expects the base pathname (without .fem extension)
    QStringList args;
    args << basePath;

    fprintf(stderr, "[SOLVER] Starting: %s %s\n",
            solverPath.toUtf8().constData(), basePath.toUtf8().constData());
    fprintf(stderr, "[SOLVER] Checking files: %s.fem=%d %s.node=%d %s.pbc=%d %s.ele=%d\n",
            basePath.toUtf8().constData(), QFileInfo::exists(basePath + ".fem"),
            basePath.toUtf8().constData(), QFileInfo::exists(basePath + ".node"),
            basePath.toUtf8().constData(), QFileInfo::exists(basePath + ".pbc"),
            basePath.toUtf8().constData(), QFileInfo::exists(basePath + ".ele"));
    emit progress("Starting solver: fkn " + basePath);
    m_process->start(solverPath, args);
}

void SolverRunner::onProcessOutput()
{
    if (!m_process) return;

    // Read stdout
    QByteArray stdoutData = m_process->readAllStandardOutput();
    if (!stdoutData.isEmpty()) {
        QString text = QString::fromUtf8(stdoutData).trimmed();
        if (!text.isEmpty())
            emit progress(text);
    }

    // Read stderr (solver writes normal progress to stderr too)
    QByteArray stderrData = m_process->readAllStandardError();
    if (!stderrData.isEmpty()) {
        QString text = QString::fromUtf8(stderrData).trimmed();
        if (!text.isEmpty())
            emit progress(text);
    }
}

void SolverRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    bool success = (exitStatus == QProcess::NormalExit && exitCode == 0);
    if (!success) {
        m_lastError = QString("Solver exited with code %1").arg(exitCode);
        emit progress(m_lastError);
    } else {
        emit progress("Solver completed successfully.");
    }
    emit finished(success);
}

void SolverRunner::onProcessError(QProcess::ProcessError error)
{
    m_lastError = QString("Process error: %1").arg(m_process ? m_process->errorString() : "unknown");
    emit progress(m_lastError);
    emit finished(false);
}
