// FEMM Qt 6 GUI — Document data model + .fem file I/O
#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "femm_types.h"
#include <QString>
#include <QObject>
#include <vector>

struct MeshEdge;

class FemmeDocument : public QObject
{
    Q_OBJECT

public:
    explicit FemmeDocument(QObject *parent = nullptr);
    ~FemmeDocument() override;

    // File I/O
    bool loadFromFile(const QString &path);
    bool saveToFile(const QString &path);

    // Path
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &p) { m_filePath = p; }

    // Geometry modification
    bool addNode(double x, double y, double tol);
    bool addSegment(int n0, int n1);
    bool addArcSegment(int n0, int n1, double arcLen, double maxSeg);
    bool addBlockLabel(double x, double y, double tol);

    int closestNode(double x, double y) const;
    int closestSegment(double x, double y) const;
    int closestBlockLabel(double x, double y) const;

    // Delete selected items (returns count of items removed)
    int deleteSelectedNodes();
    int deleteSelectedSegments();
    int deleteSelectedArcSegments();
    int deleteSelectedBlockLabels();
    int deleteSelected();  // delete all selected items

    // Bounding box of all geometry
    bool getBoundingBox(double &xmin, double &ymin,
                        double &xmax, double &ymax) const;

    // Group selection and geometry transforms
    void selectGroup(int group);
    void deselectAll();
    void translateSelected(double dx, double dy);
    void rotateSelected(double cx, double cy, double angleDeg);

    // --- Problem definition ---
    double frequency = 0.0;
    double precision = 1.0e-8;
    double minAngle = 30.0;
    double depth = 1.0;           // for planar problems
    int smartMesh = 1;
    int acSolver = 0;
    ProblemType problemType = ProblemType::Planar;
    LengthUnits lengthUnits = LengthUnits::Inches;
    CoordType   coordType = CoordType::Cartesian;
    double extZo = 0.0, extRo = 0.0, extRi = 0.0;
    QString comment;
    QString prevSoln;
    int prevType = 0;

    // --- Geometry ---
    std::vector<FNode>        nodes;
    std::vector<FSegment>     segments;
    std::vector<FArcSegment>  arcSegments;
    std::vector<FBlockLabel>  blockLabels;

    // --- Properties ---
    std::vector<FPointProp>    pointProps;
    std::vector<FBoundaryProp> boundaryProps;
    std::vector<FMaterialProp> materialProps;
    std::vector<FCircuit>      circuitProps;

    // --- Mesh data (loaded after meshing) ---
    std::vector<FMeshNode>    meshNodes;
    std::vector<FMeshElement> meshElements;
    std::vector<struct MeshEdge> meshEdges;  // edge data for solver (boundary conditions)
    bool hasMesh = false;

    // Write mesh files (.node, .ele, .edge, .pbc) for standalone solver compatibility
    bool writeMeshFiles(const QString &basePath,
                        const std::vector<struct MeshEdge> &edges);

    bool isModified = false;

    // Property index lookup (by name)
    int findPointPropIndex(const QString &name) const;
    int findBoundaryPropIndex(const QString &name) const;
    int findMaterialPropIndex(const QString &name) const;
    int findCircuitPropIndex(const QString &name) const;

private:
    // .fem parsing helpers
    static QString stripKey(const QString &line);
    QString pointPropName(int idx) const;
    QString boundaryPropName(int idx) const;
    QString materialPropName(int idx) const;
    QString circuitPropName(int idx) const;

    QString m_filePath;
};

#endif // DOCUMENT_H
