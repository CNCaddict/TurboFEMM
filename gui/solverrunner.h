// FEMM Qt 6 GUI — Solver process runner
#ifndef SOLVERRUNNER_H
#define SOLVERRUNNER_H

#include <QObject>
#include <QProcess>
#include <QString>

class FemmeDocument;

class SolverRunner : public QObject
{
    Q_OBJECT

public:
    explicit SolverRunner(QObject *parent = nullptr);
    ~SolverRunner() override;

    // Run the magnetics solver (fkn) on the given document.
    // Document must be saved to disk first.
    void runSolver(FemmeDocument *doc);

    // Set path to solver executable
    void setSolverPath(const QString &path) { m_solverPath = path; }

    bool isRunning() const;
    QString lastError() const { return m_lastError; }

signals:
    void progress(const QString &msg);
    void finished(bool success);

private slots:
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    QProcess *m_process = nullptr;
    QString m_solverPath;
    QString m_lastError;
};

#endif // SOLVERRUNNER_H
