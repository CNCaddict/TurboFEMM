// =============================================================
// FEMM geometry data types — ported from femm/nosebl.h
// Uses std types instead of MFC (QString, bool, std::vector)
// =============================================================
#ifndef FEMM_TYPES_H
#define FEMM_TYPES_H

#include <QString>
#include <QPointF>
#include <vector>
#include <cmath>

// Simple complex number for property storage
// (lighter than CComplex, used only in GUI data model)
struct FemmComplex {
    double re = 0.0, im = 0.0;
    FemmComplex() = default;
    FemmComplex(double r, double i = 0.0) : re(r), im(i) {}
    double abs() const { return std::sqrt(re*re + im*im); }
    double arg() const { return std::atan2(im, re); }
};

// ---------------------------------------------------------------
// Geometry primitives
// ---------------------------------------------------------------

struct FNode {
    double x = 0.0, y = 0.0;
    bool isSelected = false;
    QString boundaryMarker = "<None>";
    int inGroup = 0;
    QString name;  // user-assignable label (optional)

    double distanceTo(double xo, double yo) const {
        return std::sqrt((x-xo)*(x-xo) + (y-yo)*(y-yo));
    }
    QPointF toPointF() const { return QPointF(x, y); }
};

struct FSegment {
    int n0 = 0, n1 = 0;           // endpoint node indices
    bool isSelected = false;
    bool hidden = false;
    double maxSideLength = -1.0;   // -1 = no constraint
    QString boundaryMarker = "<None>";
    int inGroup = 0;
};

struct FArcSegment {
    int n0 = 0, n1 = 0;
    bool normalDirection = true;
    bool isSelected = false;
    bool hidden = false;
    double maxSideLength = 10.0;
    double arcLength = 90.0;       // degrees
    QString boundaryMarker = "<None>";
    int inGroup = 0;
};

struct FBlockLabel {
    double x = 0.0, y = 0.0;
    double maxArea = 0.0;          // 0 = no constraint
    double magDir = 0.0;           // magnetization direction, degrees
    int turns = 1;
    bool isSelected = false;
    QString blockType = "<None>";
    QString inCircuit = "<None>";
    QString magDirFctn;
    int inGroup = 0;
    bool isExternal = false;
    bool isDefault = false;
    QString name;  // user-assignable label (optional)

    // Iron loss toggle (per-block; lamination data lives on the material)
    bool calculateLosses = false;   // enable loss calculation for this block

    double distanceTo(double xo, double yo) const {
        return std::sqrt((x-xo)*(x-xo) + (y-yo)*(y-yo));
    }
};

// ---------------------------------------------------------------
// Material and boundary properties
// ---------------------------------------------------------------

// Core loss data point (from manufacturer datasheet)
struct CoreLossPoint {
    double B = 0.0;          // peak flux density (T)
    double freq = 0.0;       // frequency (Hz)
    double loss_Wkg = 0.0;   // specific loss (W/kg)
};

struct FMaterialProp {
    QString blockName;
    double mu_x = 1.0, mu_y = 1.0;      // relative permeability
    int bhPoints = 0;
    std::vector<std::pair<double,double>> bhData;  // (B, H) pairs
    int lamType = 0;           // 0=not lam, 1=x-dir, 2=y-dir
    double lamFill = 1.0;
    double theta_m = 0.0;     // magnetization direction, degrees
    double H_c = 0.0;         // coercivity, A/m
    FemmComplex Jsrc;          // applied current density, MA/m^2
    double Cduct = 0.0;       // conductivity, MS/m
    double Lam_d = 0.0;       // lamination thickness, mm
    double Theta_hn = 0.0;    // hysteresis angle (nonlinear)
    double Theta_hx = 0.0;    // hysteresis angle x
    double Theta_hy = 0.0;    // hysteresis angle y
    int nStrands = 0;
    double wireD = 0.0;       // strand diameter, mm

    // Steinmetz iron loss coefficients
    // P = Kh*f*B^alpha + Kc*(f*B)^2 + Ke*(f*B)^1.5  [W/m^3]
    double Kh = 0.0;          // hysteresis loss coefficient
    double Kc = 0.0;          // classical eddy current coefficient
    double Ke = 0.0;          // excess/anomalous loss coefficient
    double alpha_loss = 2.0;  // Steinmetz exponent (typically 1.6-2.5)
    double density = 0.0;     // material density (kg/m^3), for W/kg conversion

    // Core loss data points for automatic coefficient fitting
    std::vector<CoreLossPoint> coreLossData;
};

struct FBoundaryProp {
    QString bdryName;
    int bdryFormat = 0;
    // 0=prescribed A, 1=small skin depth, 2=mixed, 3=SDI,
    // 4=periodic, 5=antiperiodic, 6=periodic AGE, 7=antiperiodic AGE

    double A0 = 0.0, A1 = 0.0, A2 = 0.0, phi = 0.0;
    double Mu = 0.0, Sig = 0.0;
    FemmComplex c0, c1;
    double innerAngle = 0.0, outerAngle = 0.0;
};

struct FPointProp {
    QString pointName;
    FemmComplex Jp;   // applied point current
    FemmComplex Ap;   // prescribed vector potential
};

struct FCircuit {
    QString circName;
    FemmComplex amps;
    int circType = 0;  // 0=parallel, 1=series
};

// ---------------------------------------------------------------
// Mesh elements (for display after meshing / post-processing)
// ---------------------------------------------------------------

struct FMeshNode {
    double x = 0.0, y = 0.0;
    int boundaryMarker = 0;  // Triangle output point marker
};

struct FMeshElement {
    int p[3] = {0, 0, 0};     // node indices
    int label = 0;             // block label index
};

// ---------------------------------------------------------------
// Sliding band for motion sweep optimisation
// ---------------------------------------------------------------
// Forward-declare MeshEdge (defined in meshgen.h) — we only store
// vectors of them, so a forward declaration suffices.
struct MeshEdge;

struct SlidingBand {
    bool active = false;
    bool isRotation = true;
    double cx = 0.0, cy = 0.0;         // rotation center (model units)
    double innerRadius = 0.0;           // inner interface circle
    double outerRadius = 0.0;           // outer interface circle
    bool rotorIsInside = true;          // true=inner rotor, false=outer rotor
    int airBlockLabel = 0;              // 1-based block label index for air region
    int numInterfaceNodes = 0;          // N nodes per circle
    int movingGroup = 0;                // which group number is the rotor

    // For linear motion
    double linePos0 = 0.0, linePos1 = 0.0; // band boundary positions along motion-normal axis
    double lineDirX = 0.0, lineDirY = 0.0; // motion direction (unit vector)

    // Node classification (indices into doc->meshNodes)
    std::vector<int> rotorNodeIndices;       // nodes inside inner circle
    std::vector<int> innerCircleNodeIndices;  // N nodes on inner interface (sorted by angle)
    std::vector<int> outerCircleNodeIndices;  // N nodes on outer interface (sorted by angle)

    // Base angles (radians) for interface nodes
    std::vector<double> innerBaseAngles;
    std::vector<double> outerBaseAngles;

    // Element classification
    std::vector<int> rotorElementIndices;
    std::vector<int> statorElementIndices;
    int bandElementStart = 0;   // start index in doc->meshElements
    int bandElementCount = 0;   // number of band elements

    // Fixed edges (rotor + stator) — don't change across steps
    // Band edges are regenerated each step by remeshBand().
    std::vector<MeshEdge> fixedEdges;

    double cumulativeAngle = 0.0;  // total rotation applied (degrees)
};

// ---------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------

enum class ProblemType { Planar = 0, Axisymmetric = 1 };
enum class LengthUnits { Inches = 0, Millimeters, Centimeters, Meters, Mils, Microns };
enum class CoordType { Cartesian = 0, Polar };

#endif // FEMM_TYPES_H
