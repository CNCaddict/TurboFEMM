// FEMM Qt 6 GUI — Post-processor results document
// Loads .ans solution files, computes field values
#ifndef RESULTSDOC_H
#define RESULTSDOC_H

#include <QString>
#include <QObject>
#include <vector>
#include <cmath>
#include <complex>

// Complex number alias
using CmplxF = std::complex<double>;

// ---------------------------------------------------------------
// Per-node solution data
// ---------------------------------------------------------------
struct SolnNode {
    double x = 0.0, y = 0.0;
    CmplxF A;           // vector potential
    double msk = 0.0;   // mask value
};

// ---------------------------------------------------------------
// Per-element solution data
// ---------------------------------------------------------------
struct SolnElement {
    int p[3] = {0, 0, 0};   // node indices
    int blk = 0;             // material property index
    int lbl = 0;             // block label index
    CmplxF B1, B2;           // element-average flux density (x,y)
    CmplxF b1[3], b2[3];    // smoothed nodal flux density
    double magdir = 0.0;    // magnetization direction
    double cx = 0.0, cy = 0.0; // centroid
    double rsqr = 0.0;      // bounding radius squared
    int n[3] = {-1, -1, -1}; // neighbor elements
};

// ---------------------------------------------------------------
// Block label solution data
// ---------------------------------------------------------------
struct SolnLabel {
    double x = 0.0, y = 0.0;
    int blockType = 0;
    int inCircuit = -1;
    int turns = 0;
    double fillFactor = 1.0;
    double magDir = 0.0;
    int inGroup = 0;
    bool isExternal = false;
    bool isSelected = false;

    // Iron loss toggle (from block label; lamination data lives on SolnMaterial)
    bool calculateLosses = false;
};

// ---------------------------------------------------------------
// Material property (for post-processor)
// ---------------------------------------------------------------
struct SolnMaterial {
    QString blockName;
    double mu_x = 1.0, mu_y = 1.0;
    double H_c = 0.0;
    double Cduct = 0.0;
    int lamType = 0;
    double lamFill = 1.0;
    double Lam_d = 0.0;
    int bhPoints = 0;
    std::vector<double> Bdata;
    std::vector<CmplxF> Hdata;
    CmplxF mu_fdx, mu_fdy;  // frequency-dependent permeability

    // Steinmetz iron loss coefficients
    double Kh = 0.0, Kc = 0.0, Ke = 0.0;
    double alpha_loss = 2.0;
    double density = 0.0;  // kg/m^3
};

// ---------------------------------------------------------------
// Point query result
// ---------------------------------------------------------------
struct PointValues {
    CmplxF A;       // vector potential
    CmplxF B1, B2;  // flux density
    CmplxF H1, H2;  // field intensity
    CmplxF Je, Js;  // eddy and source current
    double energy = 0.0;
    bool valid = false;
};

// ---------------------------------------------------------------
// Density plot types
// ---------------------------------------------------------------
enum class DensityType {
    None = 0,
    B_mag,        // |B|
    B_real,       // |Re(B)|
    B_imag,       // |Im(B)|
    H_mag,        // |H|
    J_mag,        // |J|
    IronLoss,     // Iron loss density (W/kg) from Steinmetz model
};

// ---------------------------------------------------------------
// Summary statistics (for parametric sweeps / CSV export)
// ---------------------------------------------------------------
struct ResultsSummary {
    double B_max = 0.0, B_min = 0.0, B_avg = 0.0;
    double A_max = 0.0, A_min = 0.0;
    double totalEnergy = 0.0;   // stored magnetic energy (J)
    double totalArea = 0.0;     // sum of element areas (m^2)
};

// ---------------------------------------------------------------
// Results Document
// ---------------------------------------------------------------
class ResultsDocument : public QObject
{
    Q_OBJECT

public:
    explicit ResultsDocument(QObject *parent = nullptr);
    ~ResultsDocument() override;

    bool loadFromFile(const QString &path);

    // In-memory loading: populate results directly from solver arrays.
    // nodeX/nodeY: mesh node coordinates in model units (pre-cm-conversion)
    // nodeA_re/nodeA_im: solution vector potential at each node
    // elmP: element node indices (3 per element)
    // elmLbl: element label (0-based block label index)
    // elmBlk: element material property index
    // All arrays are read-only; data is copied.
    bool loadFromSolverData(
        int probType, int lenUnits, double freq, double dpth,
        // Materials: arrays of length numMats
        int numMats, const QString *matNames,
        const double *mat_mu_x, const double *mat_mu_y,
        const double *mat_H_c, const double *mat_Cduct,
        const int *mat_lamType, const double *mat_lamFill, const double *mat_Lam_d,
        const int *mat_bhPoints, const double *const *mat_Bdata, const double *const *mat_Hdata_re,
        // Labels: arrays of length numLabels
        int numLabels, const double *lblX, const double *lblY,
        const int *lblBlockType, const double *lblMagDir, const int *lblInGroup, const int *lblTurns,
        // Mesh: arrays
        int numNodes, const double *nodeX, const double *nodeY,
        const double *nodeA_re, const double *nodeA_im,
        int numElements, const int *elmP, const int *elmLbl, const int *elmBlk);

    QString filePath() const { return m_filePath; }

    // Field queries
    PointValues getPointValues(double x, double y) const;
    int inTriangle(double x, double y) const;

    // Summary statistics for parametric sweeps
    ResultsSummary computeSummary() const;

    // Maxwell stress tensor torque (Henrotte method) about point (aboutX,aboutY)
    // in model coordinates.  groupNumber selects the rotor region.
    // Returns torque in N·m (planar) or N·m/radian (axisym).
    double computeTorque(double aboutX, double aboutY, int groupNumber) const;

    // Bounding box
    bool getBoundingBox(double &xmin, double &ymin,
                        double &xmax, double &ymax) const;

    // Solution data
    std::vector<SolnNode> nodes;
    std::vector<SolnElement> elements;
    std::vector<SolnLabel> labels;
    std::vector<SolnMaterial> materials;

    // Problem info
    double frequency = 0.0;
    double depth = 1.0;
    int problemType = 0;    // 0=planar, 1=axisymmetric
    int lengthUnits = 0;
    double lengthConv = 1.0; // conversion to meters

    // Plot bounds
    double A_Low = 0.0, A_High = 0.0;
    double B_Low = 0.0, B_High = 0.0;
    int numContours = 19;

    // Per-element iron loss (W/kg), populated by motion runner after sweep
    // Indexed by element index, same size as elements[] when populated.
    std::vector<double> ironLoss_Wkg;
    double ironLoss_Low = 0.0, ironLoss_High = 0.0;

    // Smoothing
    bool smoothB = true;

    // Call to ensure smoothed B values and plot bounds are ready
    // (called automatically by overlay renderer before drawing).
    void ensureSmoothedB();

private:
    void computeElementB();
    void computeSmoothedB();
    void computePlotBounds();
    void buildAdjacency();
    void findBoundaryEdges();

    bool m_smoothedBReady = false;

    // Element B computation
    void getElementB(SolnElement &elm);

    // Adjacency data (per mesh node)
    std::vector<int> m_numList;                   // count of elements per node
    std::vector<std::vector<int>> m_conList;      // element indices per node

    QString m_filePath;
    mutable int m_lastTriangle = 0;  // cache for InTriangle
};

#endif // RESULTSDOC_H
