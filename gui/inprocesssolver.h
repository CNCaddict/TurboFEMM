// FEMM Qt 6 GUI — In-process solver (no disk I/O per step)
#ifndef INPROCESSSOLVER_H
#define INPROCESSSOLVER_H

#include <QObject>
#include <vector>
#include <memory>

class FemmeDocument;
class ResultsDocument;
struct MeshEdge;
struct lua_State;
#ifdef FEMM_USE_METAL
class GPUBackend;
#endif

class InProcessSolver : public QObject
{
    Q_OBJECT

public:
    explicit InProcessSolver(QObject *parent = nullptr);
    ~InProcessSolver() override;

    // Run mesh generation + solver in-process, populating resultsOut.
    // Returns true on success.
    // GPU backend and Lua state are persisted across calls for performance.
    bool solve(FemmeDocument *doc, ResultsDocument *resultsOut);

    // Solve using an existing mesh (doc->meshNodes/meshElements) + provided edges.
    // Skips mesh generation — useful for adaptive refinement.
    bool solveExistingMesh(FemmeDocument *doc,
                           const std::vector<MeshEdge> &edges,
                           ResultsDocument *resultsOut);

    QString lastError() const { return m_lastError; }

signals:
    void progress(const QString &msg);

private:
    QString m_lastError;

#ifdef FEMM_USE_METAL
    // Persistent GPU backend (device + pipelines reused across solves).
    // Saves ~15ms of Metal device init + pipeline compilation per step.
    std::unique_ptr<GPUBackend> m_gpu;
#endif

    // Persistent Lua state (reused across solves).
    // Saves ~5ms of Lua state init + library loading per step.
    lua_State *m_lua = nullptr;
};

#endif // INPROCESSSOLVER_H
