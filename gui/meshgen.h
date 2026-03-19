// FEMM Qt 6 GUI — Mesh generation (.poly writer + triangle invocation)
#ifndef MESHGEN_H
#define MESHGEN_H

#include <QString>
#include <QObject>
#include <vector>

class FemmeDocument;
struct SlidingBand;
struct MotionConfig;

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
    // If bandSetup is non-null, two interface circles are injected into the PSLG
    // to create a sliding band for motion sweep optimisation.
    bool generateMeshInProcess(FemmeDocument *doc, std::vector<MeshEdge> &edgesOut,
                               SlidingBand *bandSetup = nullptr);

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

    // --- Sliding band for motion sweep optimisation ---

    // Detect the airgap in a motor model and populate band radii/config.
    // Returns true if a clear airgap was found between the motion group and
    // the stationary geometry.  On success, band.innerRadius/outerRadius,
    // airBlockLabel, numInterfaceNodes, cx, cy are filled in.
    bool detectAirgap(FemmeDocument *doc, const MotionConfig &config,
                      SlidingBand &band);

    // Set up sliding band from a user-specified airgap radius.
    // Places inner/outer interface circles at radius ± bandWidth/2,
    // finds the air block label, and populates band config.
    bool setupSlidingBand(FemmeDocument *doc, const MotionConfig &config,
                          double innerR, double outerR, SlidingBand &band);

    // After initial mesh generation (with interface circles), classify every
    // node and element as rotor / stator / band and fill the index vectors
    // in `band`.  Also reorders meshElements so the band is contiguous at
    // the end.  Returns true on success.
    bool classifyMeshForSlidingBand(FemmeDocument *doc, SlidingBand &band);

    // Regenerate only the band elements (and edges) after rotating rotor
    // mesh nodes.  Writes new triangles into doc->meshElements starting at
    // band.bandElementStart and returns the full edge list (fixed + band).
    void remeshBand(FemmeDocument *doc, SlidingBand &band,
                    std::vector<MeshEdge> &edgesOut);

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
