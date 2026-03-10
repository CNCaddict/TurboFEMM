// FEMM Qt 6 GUI — Mesh generation (.poly writer + triangle invocation)
#ifndef MESHGEN_H
#define MESHGEN_H

#include <QString>
#include <QObject>
#include <vector>

class FemmeDocument;

// Edge from Triangle output (needed by solver for boundary condition mapping)
struct MeshEdge {
    int n0 = 0, n1 = 0;
    int marker = 0;
};

class MeshGenerator : public QObject
{
    Q_OBJECT

public:
    explicit MeshGenerator(QObject *parent = nullptr);
    ~MeshGenerator() override;

    // Generate mesh for a document. Returns true on success.
    // The document's filePath must be set (saved to disk) before meshing.
    bool generateMesh(FemmeDocument *doc);

    // In-process mesh generation using Triangle library (no disk I/O).
    // Populates doc->meshNodes and doc->meshElements, plus edgesOut for solver.
    bool generateMeshInProcess(FemmeDocument *doc, std::vector<MeshEdge> &edgesOut);

    // Refine existing mesh using Triangle's -r switch with per-element area constraints.
    // doc->meshNodes and doc->meshElements must already be populated.
    // triangleAreaList[i] = max area for element i (negative = no constraint).
    // segmentEdges = boundary/constrained edges that Triangle must preserve.
    bool refineMeshInProcess(FemmeDocument *doc,
                             const std::vector<MeshEdge> &segmentEdges,
                             const std::vector<double> &triangleAreaList,
                             std::vector<MeshEdge> &edgesOut);

    // Write the in-memory mesh to disk (.node, .ele, .pbc files).
    // Needed before running the external fkn solver when the mesh was
    // generated in-process (e.g. by adaptive refinement).
    bool writeMeshFiles(FemmeDocument *doc);

    // Path to the triangle executable
    void setTrianglePath(const QString &path) { m_trianglePath = path; }

    QString lastError() const { return m_lastError; }

signals:
    void progress(const QString &msg);

private:
    // Write the .poly file for triangle
    bool writePoly(FemmeDocument *doc, const QString &polyPath);

    // Run triangle mesh generator
    bool runTriangle(const QString &basePath);

    // Load mesh results back into document
    bool loadMesh(FemmeDocument *doc, const QString &basePath);

    // Helper: discretize arc segments into line segments
    struct TempNode { double x, y; int boundaryMarker; };
    struct TempSegment { int n0, n1; int boundaryMarker; };

    void discretizeArcs(FemmeDocument *doc,
                        std::vector<TempNode> &extraNodes,
                        std::vector<TempSegment> &extraSegments,
                        int baseNodeCount);

    QString m_trianglePath;
    QString m_lastError;
};

#endif // MESHGEN_H
