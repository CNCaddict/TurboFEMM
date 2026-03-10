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

    double distanceTo(double xo, double yo) const {
        return std::sqrt((x-xo)*(x-xo) + (y-yo)*(y-yo));
    }
};

// ---------------------------------------------------------------
// Material and boundary properties
// ---------------------------------------------------------------

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
// Enumerations
// ---------------------------------------------------------------

enum class ProblemType { Planar = 0, Axisymmetric = 1 };
enum class LengthUnits { Inches = 0, Millimeters, Centimeters, Meters, Mils, Microns };
enum class CoordType { Cartesian = 0, Polar };

#endif // FEMM_TYPES_H
