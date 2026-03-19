// =============================================================
// Iron loss computation — three methods:
//
// 1. Steinmetz/Bertotti (laminated steel with explicit Kh/Kc/Ke):
//    P = Kh * f * Bpk^alpha + Kc * (f*Bpk)^2 + Ke * (f*Bpk)^1.5
//    Coefficients are in W/m³ units as stored in .fem files.
//
// 2. Solid ferromagnetic eddy current (conductive steel/iron, Lam_d = 0):
//    Fast mode:
//      P = σ * d_eff² * <(dB/dt)²> / 12   [W/m³]
//      where d_eff is auto-estimated from the block geometry as 2A/P.
//    Optional accurate mode for rotating annular rotor steel:
//      Uses a boundary-driven 1D diffusion solve through the backiron
//      thickness for each angular sector, then maps the resulting local
//      J²/σ loss profile back onto the 2D mesh.
//
// 3. Az-based eddy current (solid low-μ conductors: σ > 0, Lam_d = 0):
//    P = σ * <(dAz/dt - <dAz/dt>_block)²>   [W/m³]
//    Uses the time derivative of vector potential at each point, with the
//    per-block mean removed to cancel common-mode/gauge motion that would
//    otherwise imply a non-physical net current in an isolated conductor.
//    This is a better approximation for isolated solid magnets and similar
//    low-permeability conductive regions.
//
// 4. dB/dt slab formula (laminated conductors: σ > 0, Lam_d > 0):
//    P = σ * d² * <(dB/dt)²> / 12   [W/m³]
//    Classical thin-slab eddy current model. For laminated materials
//    without Steinmetz data sheets.
// =============================================================
#ifndef IRONLOSS_H
#define IRONLOSS_H

#include "bhistory.h"
#include "resultsdoc.h"
#include <QString>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Per-element iron loss result
struct ElementLoss {
    double loss_Wkg = 0.0;    // specific loss (W/kg)
    double loss_Wm3 = 0.0;    // volumetric loss (W/m^3)
    double Bpeak = 0.0;       // peak |B| over all steps
    double freq = 0.0;        // effective frequency used
};

// Per-block (region) iron loss summary
struct BlockLossSummary {
    int blockIndex = -1;
    QString materialName;
    double totalLoss_W = 0.0;     // total loss in watts for the block
    double avgLoss_Wkg = 0.0;     // area-weighted average specific loss
    double maxLoss_Wkg = 0.0;     // peak specific loss in the block
    double avgBpeak = 0.0;        // average peak B
    double totalArea_m2 = 0.0;    // total element area
    int numElements = 0;
};

// Motion parameters for rotor-aware B(t) lookup
// Rotor elements move between steps; we inverse-transform query points
// to find where each element was at each historical step.
struct MotionParams {
    int movingGroup = 0;       // inGroup number for rotor (0 = none/stator-only)
    bool isRotation = false;
    double cx = 0, cy = 0;    // rotation center (model units)
    double anglePerStep = 0;   // degrees per step
    double dx = 0, dy = 0;    // translation per step (model units)
    int totalSteps = 0;        // total number of steps in sweep
    double rpm = 0;            // motor RPM (for dB/dt time step computation)
};

struct IronLossOptions {
    bool accurateSolidLosses = false;
    int solidLossRadialCells = 16;
};

// Iron loss computation result for the entire model
struct IronLossResult {
    std::vector<ElementLoss> elementLosses;   // indexed by element index
    std::vector<BlockLossSummary> blockSummaries;
    double totalLoss_W = 0.0;   // grand total watts
    double frequency = 0.0;      // operating frequency used
    bool valid = false;
};

inline double effectiveRelativeMuFromBH(const SolnMaterial &mat, double Bmag)
{
    constexpr double mu0 = 4.0e-7 * M_PI;
    if (Bmag < 1e-12) {
        double muGuess = std::sqrt(std::max(1.0, std::fabs(mat.mu_x * mat.mu_y)));
        return std::max(1.0, muGuess);
    }

    if (mat.bhPoints > 0 &&
        (int)mat.Bdata.size() >= mat.bhPoints &&
        (int)mat.Hdata.size() >= mat.bhPoints) {
        double b = std::fabs(Bmag);

        auto evalMu = [&](double Hre) {
            return std::max(1.0, b / std::max(mu0 * std::fabs(Hre), 1e-12));
        };

        if (b <= mat.Bdata.front())
            return evalMu(mat.Hdata.front().real());

        for (int i = 0; i + 1 < mat.bhPoints; i++) {
            double b0 = mat.Bdata[i];
            double b1 = mat.Bdata[i + 1];
            if (b >= b0 && b <= b1 && std::fabs(b1 - b0) > 1e-12) {
                double t = (b - b0) / (b1 - b0);
                double h = (1.0 - t) * mat.Hdata[i].real() + t * mat.Hdata[i + 1].real();
                return evalMu(h);
            }
        }

        return evalMu(mat.Hdata[mat.bhPoints - 1].real());
    }

    double muGuess = std::sqrt(std::max(1.0, std::fabs(mat.mu_x * mat.mu_y)));
    return std::max(1.0, muGuess);
}

inline std::pair<float, float> rotatePointToHistoryStep(float qx, float qy,
                                                        const MotionParams &motion,
                                                        size_t historySize,
                                                        size_t stepIndex)
{
    if (!(motion.isRotation && motion.movingGroup > 0))
        return {qx, qy};

    int stepsBack = (int)historySize - 1 - (int)stepIndex;
    if (stepsBack == 0)
        return {qx, qy};

    double backAngle = -(double)stepsBack * motion.anglePerStep * M_PI / 180.0;
    double rx = (double)qx - motion.cx;
    double ry = (double)qy - motion.cy;
    double cosA = std::cos(backAngle);
    double sinA = std::sin(backAngle);
    return {
        (float)(motion.cx + rx * cosA - ry * sinA),
        (float)(motion.cy + rx * sinA + ry * cosA)
    };
}

inline std::pair<double, double> rotateBToMaterialFrame(double bx, double by,
                                                        double pointAngle,
                                                        const MotionParams &motion,
                                                        size_t historySize,
                                                        size_t stepIndex)
{
    if (!(motion.isRotation && motion.movingGroup > 0))
        return {bx, by};

    int stepsBack = (int)historySize - 1 - (int)stepIndex;
    double historyAngle = pointAngle - (double)stepsBack * motion.anglePerStep * M_PI / 180.0;
    double cosA = std::cos(historyAngle);
    double sinA = std::sin(historyAngle);
    double br = bx * cosA + by * sinA;
    double bt = -bx * sinA + by * cosA;
    return {br, bt};
}

// =============================================================
// Raw Steinmetz formula: output units match coefficient units.
// FEMM stores coefficients in W/m³ units (e.g. Kh=179 for M-19),
// so the result is W/m³.  Divide by density to get W/kg.
// =============================================================
inline double steinmetzLoss_Wm3(double Kh, double Kc, double Ke,
                                 double alpha, double freq, double Bpk)
{
    if (Bpk < 1e-12 || freq < 1e-12) return 0.0;

    double hysteresis = Kh * freq * std::pow(Bpk, alpha);
    double fB = freq * Bpk;
    double eddy = Kc * fB * fB;
    double excess = Ke * fB * std::sqrt(fB);  // (fB)^1.5
    return hysteresis + eddy + excess;
}

// Backward-compat alias
inline double steinmetzLoss_Wkg(double Kh, double Kc, double Ke,
                                 double alpha, double freq, double Bpk)
{
    return steinmetzLoss_Wm3(Kh, Kc, Ke, alpha, freq, Bpk);
}

// =============================================================
// Compute iron losses for the entire model from B(t) history
//
// Parameters:
//   bHistory  — per-step B snapshots from motion sweep
//   lastDoc   — the final-step ResultsDocument (for mesh/material info)
//   sweepFreq — operating frequency in Hz
//   depth     — model depth in meters (for total watts computation)
//   motion    — motion parameters (for rotor inverse transform + dt)
//
// Loss computation strategy per element:
//   - Materials with explicit Steinmetz coefficients (Kh/Kc/Ke):
//     Uses peak-B Steinmetz formula (traditional 3-term Bertotti).
//   - Conductive solid ferromagnetic materials without Steinmetz
//     (Cduct > 0, Lam_d = 0, high-μ/B-H steel):
//     Uses a solid-body slab approximation with an auto-estimated effective
//     thickness d_eff = 2A/P for each block:
//     P = σ * d_eff² * <(dB/dt)²> / 12  [W/m³]
//   - Conductive solid low-μ materials without Steinmetz
//     (Cduct > 0, Lam_d = 0, magnets and similar):
//     Uses Az-based eddy current with block-mean removal:
//     P = σ * <(dAz/dt - <dAz/dt>_block)²>  [W/m³]
//     The spatial variation of Az across the conductor cross-section
//     captures geometry-limited eddy current distribution while the mean
//     removal suppresses non-physical common-mode drift in isolated regions.
//   - Conductive materials with Lam_d > 0 but no Steinmetz:
//     Uses dB/dt slab formula: P = σ * d² * <(dB/dt)²> / 12
//     For laminated conductors without loss data sheets.
// =============================================================
inline IronLossResult computeIronLosses(
    const std::vector<BSnapshot> &bHistory,
    const ResultsDocument *lastDoc,
    double sweepFreq,
    double depth = 1.0,
    const MotionParams &motion = MotionParams{},
    const IronLossOptions &options = IronLossOptions{})
{
    IronLossResult result;
    result.frequency = sweepFreq;

    if (!lastDoc || bHistory.empty() || lastDoc->elements.empty()) {
        return result;
    }

    int numElm = (int)lastDoc->elements.size();
    result.elementLosses.resize(numElm);

    double L = lastDoc->lengthConv;  // model units to meters

    // Compute time step between B snapshots for dB/dt and dAz/dt
    // For rotation: dt = anglePerStep / (6 * RPM) seconds
    double dt = 0.0;
    if (motion.isRotation && motion.rpm > 0 && motion.anglePerStep > 0) {
        dt = motion.anglePerStep / (6.0 * motion.rpm);
    } else if (sweepFreq > 0 && motion.totalSteps > 1) {
        // Fallback: assume one electrical cycle over the sweep
        dt = 1.0 / (sweepFreq * motion.totalSteps);
    }

    // Build spatial indices for each step
    std::vector<BHistoryIndex> indices(bHistory.size());
    for (size_t s = 0; s < bHistory.size(); s++) {
        indices[s].build(bHistory[s]);
    }

    struct BlockGeometry {
        double area_m2 = 0.0;
        double perimeter_m = 0.0;
        double characteristicThickness_m = 0.0;
    };
    std::map<int, BlockGeometry> blockGeometry;

    struct EdgeRecord {
        int firstLabel = -1;
        int secondLabel = -1;
        double length_m = 0.0;
        int count = 0;
    };
    std::map<std::pair<int, int>, EdgeRecord> edgeMap;

    for (int i = 0; i < numElm; i++) {
        const auto &elm = lastDoc->elements[i];
        int lbl = elm.lbl;
        if (lbl < 0 || lbl >= (int)lastDoc->labels.size()) continue;

        double x0 = lastDoc->nodes[elm.p[0]].x, y0 = lastDoc->nodes[elm.p[0]].y;
        double x1 = lastDoc->nodes[elm.p[1]].x, y1 = lastDoc->nodes[elm.p[1]].y;
        double x2 = lastDoc->nodes[elm.p[2]].x, y2 = lastDoc->nodes[elm.p[2]].y;
        double area = 0.5 * std::fabs((x1-x0)*(y2-y0) - (x2-x0)*(y1-y0));
        blockGeometry[lbl].area_m2 += area * L * L;

        for (int e = 0; e < 3; e++) {
            int n0 = elm.p[e];
            int n1 = elm.p[(e + 1) % 3];
            if (n1 < n0) std::swap(n0, n1);
            auto key = std::make_pair(n0, n1);

            double dx = lastDoc->nodes[n1].x - lastDoc->nodes[n0].x;
            double dy = lastDoc->nodes[n1].y - lastDoc->nodes[n0].y;
            double length_m = std::sqrt(dx * dx + dy * dy) * L;

            auto &rec = edgeMap[key];
            if (rec.count == 0) {
                rec.firstLabel = lbl;
                rec.length_m = length_m;
            } else if (rec.count == 1 && rec.firstLabel != lbl) {
                rec.secondLabel = lbl;
            }
            rec.count++;
        }
    }

    for (const auto &[key, rec] : edgeMap) {
        if (rec.count == 1) {
            if (rec.firstLabel >= 0)
                blockGeometry[rec.firstLabel].perimeter_m += rec.length_m;
        } else if (rec.secondLabel >= 0 && rec.secondLabel != rec.firstLabel) {
            blockGeometry[rec.firstLabel].perimeter_m += rec.length_m;
            blockGeometry[rec.secondLabel].perimeter_m += rec.length_m;
        }
    }

    for (auto &[lbl, geom] : blockGeometry) {
        if (geom.area_m2 > 0.0 && geom.perimeter_m > 0.0)
            geom.characteristicThickness_m = 2.0 * geom.area_m2 / geom.perimeter_m;
    }

    auto isFerromagneticSolid = [](const SolnMaterial &mat) {
        double muMax = std::max(std::fabs(mat.mu_x), std::fabs(mat.mu_y));
        bool hasBhCurve = (mat.bhPoints > 0);
        bool highMu = (muMax > 5.0);
        bool permanentMagnet = (std::fabs(mat.H_c) > 1e-6);
        return !permanentMagnet && (hasBhCurve || highMu);
    };

    auto wrapAngle = [](double a) {
        while (a < 0.0) a += 2.0 * M_PI;
        while (a >= 2.0 * M_PI) a -= 2.0 * M_PI;
        return a;
    };

    struct RotorRadialModel {
        bool active = false;
        double innerRadius = 0.0;
        double outerRadius = 0.0;
        int numSectors = 0;
        std::vector<double> innerMeanDbdt2;
        std::vector<double> outerMeanDbdt2;
        double globalInnerMeanDbdt2 = 0.0;
        double globalOuterMeanDbdt2 = 0.0;
    };
    std::map<int, RotorRadialModel> rotorRadialModels;

    struct RotorDiffusionModel {
        bool active = false;
        double innerRadius = 0.0;
        double outerRadius = 0.0;
        int numSectors = 0;
        int numCells = 0;
        std::vector<std::vector<double>> cellLoss_Wm3;
    };
    std::map<int, RotorDiffusionModel> rotorDiffusionModels;

    auto solveTridiagonal = [](const std::vector<double> &a,
                               const std::vector<double> &b,
                               const std::vector<double> &c,
                               const std::vector<double> &d) {
        const int n = (int)b.size();
        std::vector<double> cp(n, 0.0);
        std::vector<double> dp(n, 0.0);
        std::vector<double> x(n, 0.0);
        if (n <= 0)
            return x;

        double denom = b[0];
        if (std::fabs(denom) < 1e-30)
            denom = (denom < 0.0) ? -1e-30 : 1e-30;
        cp[0] = (n > 1) ? c[0] / denom : 0.0;
        dp[0] = d[0] / denom;

        for (int i = 1; i < n; i++) {
            denom = b[i] - a[i] * cp[i - 1];
            if (std::fabs(denom) < 1e-30)
                denom = (denom < 0.0) ? -1e-30 : 1e-30;
            cp[i] = (i + 1 < n) ? c[i] / denom : 0.0;
            dp[i] = (d[i] - a[i] * dp[i - 1]) / denom;
        }

        x[n - 1] = dp[n - 1];
        for (int i = n - 2; i >= 0; i--)
            x[i] = dp[i] - cp[i] * x[i + 1];
        return x;
    };

    if (motion.isRotation && motion.movingGroup > 0 && dt > 0.0 && bHistory.size() > 1) {
        for (int lbl = 0; lbl < (int)lastDoc->labels.size(); lbl++) {
            const auto &label = lastDoc->labels[lbl];
            if (!label.calculateLosses) continue;
            if (label.inGroup != motion.movingGroup) continue;

            int matIdx = label.blockType;
            if (matIdx < 0 || matIdx >= (int)lastDoc->materials.size()) continue;
            const auto &mat = lastDoc->materials[matIdx];
            bool hasSteinmetz = (mat.Kh > 0 || mat.Kc > 0 || mat.Ke > 0);
            bool hasConductivity = (!hasSteinmetz && mat.Cduct > 0);
            bool isSolid = (mat.Lam_d <= 0);
            if (!(hasConductivity && isSolid && isFerromagneticSolid(mat)))
                continue;

            std::vector<double> nodeRadii;
            std::vector<double> centroidAngles;
            int numLabelElements = 0;
            for (const auto &elm : lastDoc->elements) {
                if (elm.lbl != lbl) continue;
                numLabelElements++;
                centroidAngles.push_back(wrapAngle(std::atan2(elm.cy - motion.cy,
                                                              elm.cx - motion.cx)));
                for (int k = 0; k < 3; k++) {
                    int p = elm.p[k];
                    if (p < 0 || p >= (int)lastDoc->nodes.size()) continue;
                    double dx = lastDoc->nodes[p].x - motion.cx;
                    double dy = lastDoc->nodes[p].y - motion.cy;
                    nodeRadii.push_back(std::sqrt(dx * dx + dy * dy));
                }
            }

            if (numLabelElements < 8 || nodeRadii.empty())
                continue;

            double rMin = *std::min_element(nodeRadii.begin(), nodeRadii.end());
            double rMax = *std::max_element(nodeRadii.begin(), nodeRadii.end());
            double thickness = rMax - rMin;
            if (thickness <= 1e-9)
                continue;

            int numSectors = std::max(4, std::min(48, numLabelElements / 8));
            std::vector<bool> occupied(numSectors, false);
            for (double ang : centroidAngles) {
                int sec = std::min(numSectors - 1,
                                   (int)std::floor(ang * numSectors / (2.0 * M_PI)));
                occupied[sec] = true;
            }
            int occupiedCount = 0;
            for (bool v : occupied) if (v) occupiedCount++;
            if (occupiedCount < numSectors / 2)
                continue;

            double rInnerSample = rMin + 0.20 * thickness;
            double rOuterSample = rMax - 0.20 * thickness;
            if (rOuterSample <= rInnerSample)
                continue;

            if (options.accurateSolidLosses) {
                double sigma_SI = mat.Cduct * 1e6;
                double thickness_m = (rMax - rMin) * L;
                int numCells = std::max(8, std::min(32, options.solidLossRadialCells));
                if (sigma_SI <= 0.0 || thickness_m <= 0.0)
                    continue;

                RotorDiffusionModel model;
                model.active = true;
                model.innerRadius = rMin;
                model.outerRadius = rMax;
                model.numSectors = numSectors;
                model.numCells = numCells;
                model.cellLoss_Wm3.assign(numSectors, std::vector<double>(numCells, 0.0));

                constexpr double mu0 = 4e-7 * M_PI;
                double dx = thickness_m / (double)numCells;

                for (int sec = 0; sec < numSectors; sec++) {
                    double ang = (2.0 * M_PI) * ((double)sec + 0.5) / (double)numSectors;
                    std::vector<double> innerBt(bHistory.size(), 0.0);
                    std::vector<double> outerBt(bHistory.size(), 0.0);
                    std::vector<double> innerBmag(bHistory.size(), 0.0);
                    std::vector<double> outerBmag(bHistory.size(), 0.0);

                    for (size_t s = 0; s < bHistory.size(); s++) {
                        float qxInner = (float)(motion.cx + rInnerSample * std::cos(ang));
                        float qyInner = (float)(motion.cy + rInnerSample * std::sin(ang));
                        float qxOuter = (float)(motion.cx + rOuterSample * std::cos(ang));
                        float qyOuter = (float)(motion.cy + rOuterSample * std::sin(ang));
                        auto [hxInner, hyInner] = rotatePointToHistoryStep(qxInner, qyInner, motion, bHistory.size(), s);
                        auto [hxOuter, hyOuter] = rotatePointToHistoryStep(qxOuter, qyOuter, motion, bHistory.size(), s);

                        auto [bxInner, byInner, azInner] = indices[s].lookupAll(hxInner, hyInner, lbl);
                        auto [bxOuter, byOuter, azOuter] = indices[s].lookupAll(hxOuter, hyOuter, lbl);
                        (void)azInner;
                        (void)azOuter;

                        auto [brInner, btInner] = rotateBToMaterialFrame((double)bxInner, (double)byInner,
                                                                         ang, motion, bHistory.size(), s);
                        auto [brOuter, btOuter] = rotateBToMaterialFrame((double)bxOuter, (double)byOuter,
                                                                         ang, motion, bHistory.size(), s);
                        innerBt[s] = btInner;
                        outerBt[s] = btOuter;
                        (void)brInner;
                        (void)brOuter;
                        innerBmag[s] = std::hypot((double)bxInner, (double)byInner);
                        outerBmag[s] = std::hypot((double)bxOuter, (double)byOuter);
                    }

                    double innerExcitation = 0.0;
                    double outerExcitation = 0.0;
                    for (size_t step = 0; step + 1 < bHistory.size(); step++) {
                        double dInner = (innerBt[step + 1] - innerBt[step]) / dt;
                        double dOuter = (outerBt[step + 1] - outerBt[step]) / dt;
                        innerExcitation += dInner * dInner;
                        outerExcitation += dOuter * dOuter;
                    }

                    double muSampleB = 0.0;
                    for (size_t step = 0; step < bHistory.size(); step++) {
                        muSampleB = std::max(muSampleB, innerBmag[step]);
                        muSampleB = std::max(muSampleB, outerBmag[step]);
                    }
                    double muRel = effectiveRelativeMuFromBH(mat, muSampleB);
                    double eta = 1.0 / std::max(sigma_SI * mu0 * muRel, 1e-30);
                    double alpha = eta * dt / std::max(dx * dx, 1e-30);

                    bool useTwoSidedDrive = false;
                    bool driveFromInner = true;
                    double maxExcitation = std::max(innerExcitation, outerExcitation);
                    if (maxExcitation > 1e-30) {
                        double excitationRatio = std::min(innerExcitation, outerExcitation) / maxExcitation;
                        useTwoSidedDrive = (excitationRatio > 0.75);
                        driveFromInner = (innerExcitation >= outerExcitation);
                    }

                    std::vector<double> prev(numCells + 1, 0.0);
                    for (int j = 0; j <= numCells; j++) {
                        double t = (double)j / (double)numCells;
                        prev[j] = (1.0 - t) * innerBt[0] + t * outerBt[0];
                    }

                    std::vector<double> accum(numCells, 0.0);
                    for (size_t step = 0; step + 1 < bHistory.size(); step++) {
                        double innerNext = innerBt[step + 1];
                        double outerNext = outerBt[step + 1];
                        std::vector<double> next(numCells + 1, 0.0);
                        next[0] = innerNext;
                        next[numCells] = outerNext;

                        if (numCells > 1) {
                            int nInterior = numCells - 1;
                            std::vector<double> a(nInterior, -alpha);
                            std::vector<double> b(nInterior, 1.0 + 2.0 * alpha);
                            std::vector<double> c(nInterior, -alpha);
                            std::vector<double> rhs(nInterior, 0.0);
                            for (int j = 0; j < nInterior; j++) {
                                rhs[j] = prev[j + 1];
                            }

                            if (useTwoSidedDrive || driveFromInner) {
                                rhs[0] += alpha * innerNext;
                            } else {
                                b[0] = 1.0 + alpha;
                                a[0] = 0.0;
                            }

                            if (useTwoSidedDrive || !driveFromInner) {
                                rhs[nInterior - 1] += alpha * outerNext;
                            } else {
                                b[nInterior - 1] = 1.0 + alpha;
                                c[nInterior - 1] = 0.0;
                            }

                            auto interior = solveTridiagonal(a, b, c, rhs);
                            for (int j = 0; j < nInterior; j++)
                                next[j + 1] = interior[j];
                        }

                        if (!useTwoSidedDrive) {
                            if (driveFromInner) {
                                next[numCells] = next[numCells - 1];
                            } else {
                                next[0] = next[1];
                            }
                        }

                        for (int cell = 0; cell < numCells; cell++) {
                            double dBdx = (next[cell + 1] - next[cell]) / dx;
                            double jz = dBdx / (mu0 * muRel);
                            accum[cell] += (jz * jz) / sigma_SI;
                        }
                        prev.swap(next);
                    }

                    double nDeriv = (double)((int)bHistory.size() - 1);
                    for (int cell = 0; cell < numCells; cell++)
                        model.cellLoss_Wm3[sec][cell] = accum[cell] / std::max(nDeriv, 1.0);
                }

                rotorDiffusionModels[lbl] = std::move(model);
                continue;
            }

            RotorRadialModel model;
            model.active = true;
            model.innerRadius = rMin;
            model.outerRadius = rMax;
            model.numSectors = numSectors;
            model.innerMeanDbdt2.assign(numSectors, 0.0);
            model.outerMeanDbdt2.assign(numSectors, 0.0);

            for (int sec = 0; sec < numSectors; sec++) {
                double ang = (2.0 * M_PI) * ((double)sec + 0.5) / (double)numSectors;
                double sumInner = 0.0, sumOuter = 0.0;
                int nDeriv = 0;
                float prevBxInner = 0.0f, prevByInner = 0.0f;
                float prevBxOuter = 0.0f, prevByOuter = 0.0f;
                bool hasPrev = false;

                for (size_t s = 0; s < bHistory.size(); s++) {
                    float qxInner = (float)(motion.cx + rInnerSample * std::cos(ang));
                    float qyInner = (float)(motion.cy + rInnerSample * std::sin(ang));
                    float qxOuter = (float)(motion.cx + rOuterSample * std::cos(ang));
                    float qyOuter = (float)(motion.cy + rOuterSample * std::sin(ang));
                    auto [hxInner, hyInner] = rotatePointToHistoryStep(qxInner, qyInner, motion, bHistory.size(), s);
                    auto [hxOuter, hyOuter] = rotatePointToHistoryStep(qxOuter, qyOuter, motion, bHistory.size(), s);

                    auto [bxInner, byInner, azInner] = indices[s].lookupAll(hxInner, hyInner, lbl);
                    auto [bxOuter, byOuter, azOuter] = indices[s].lookupAll(hxOuter, hyOuter, lbl);
                    (void)azInner;
                    (void)azOuter;

                    auto [brInner, btInner] = rotateBToMaterialFrame((double)bxInner, (double)byInner,
                                                                     ang, motion, bHistory.size(), s);
                    auto [brOuter, btOuter] = rotateBToMaterialFrame((double)bxOuter, (double)byOuter,
                                                                     ang, motion, bHistory.size(), s);

                    if (hasPrev) {
                        double dBrInner = (brInner - (double)prevBxInner) / dt;
                        double dBtInner = (btInner - (double)prevByInner) / dt;
                        double dBrOuter = (brOuter - (double)prevBxOuter) / dt;
                        double dBtOuter = (btOuter - (double)prevByOuter) / dt;
                        sumInner += dBrInner * dBrInner + dBtInner * dBtInner;
                        sumOuter += dBrOuter * dBrOuter + dBtOuter * dBtOuter;
                        nDeriv++;
                    }

                    prevBxInner = (float)brInner;
                    prevByInner = (float)btInner;
                    prevBxOuter = (float)brOuter;
                    prevByOuter = (float)btOuter;
                    hasPrev = true;
                }

                if (nDeriv > 0) {
                    model.innerMeanDbdt2[sec] = sumInner / (double)nDeriv;
                    model.outerMeanDbdt2[sec] = sumOuter / (double)nDeriv;
                }
            }

            double sumInnerGlobal = 0.0, sumOuterGlobal = 0.0;
            int countGlobal = 0;
            for (int sec = 0; sec < numSectors; sec++) {
                if (model.innerMeanDbdt2[sec] > 0.0 || model.outerMeanDbdt2[sec] > 0.0) {
                    sumInnerGlobal += model.innerMeanDbdt2[sec];
                    sumOuterGlobal += model.outerMeanDbdt2[sec];
                    countGlobal++;
                }
            }
            if (countGlobal <= 0)
                continue;
            model.globalInnerMeanDbdt2 = sumInnerGlobal / (double)countGlobal;
            model.globalOuterMeanDbdt2 = sumOuterGlobal / (double)countGlobal;

            rotorRadialModels[lbl] = std::move(model);
        }
    }

    struct SolidConductorSeries {
        int elementIndex = -1;
        int labelIndex = -1;
        double sigma_SI = 0.0;
        double density = 0.0;
        double areaWeight = 0.0;
        std::vector<float> azSeries;
    };
    std::vector<SolidConductorSeries> solidSeries;

    struct AccurateRotorSeries {
        int elementIndex = -1;
        int labelIndex = -1;
        double density = 0.0;
        double radius = 0.0;
        double angle = 0.0;
        double fallbackMeanDbdt2 = 0.0;
        double sigma_SI = 0.0;
    };
    std::vector<AccurateRotorSeries> accurateRotorSeries;

    struct FerroSolidSeries {
        int elementIndex = -1;
        int labelIndex = -1;
        double sigma_SI = 0.0;
        double density = 0.0;
        double areaWeight = 0.0;
        double baseMeanDbdt2 = 0.0;
        double radius = 0.0;
        double angle = 0.0;
    };
    std::vector<FerroSolidSeries> ferroSeries;

    // For each element in the final mesh, compute loss
    for (int i = 0; i < numElm; i++) {
        const auto &elm = lastDoc->elements[i];
        int lbl = elm.lbl;
        if (lbl < 0 || lbl >= (int)lastDoc->labels.size()) continue;
        if (!lastDoc->labels[lbl].calculateLosses) continue;

        int matIdx = lastDoc->labels[lbl].blockType;
        if (matIdx < 0 || matIdx >= (int)lastDoc->materials.size()) continue;

        const auto &mat = lastDoc->materials[matIdx];
        const auto &label = lastDoc->labels[lbl];

        // Determine loss computation path:
        // Path 1: Steinmetz (has explicit Kh/Kc/Ke coefficients)
        // Path 2: dB/dt eddy current with auto thickness (conductive ferromagnetic solid)
        // Path 3: Az-based eddy current (conductive low-μ solid)
        // Path 4: dB/dt slab eddy current (conductive, laminated: Lam_d > 0)
        bool hasSteinmetz = (mat.Kh > 0 || mat.Kc > 0 || mat.Ke > 0);
        bool hasConductivity = (!hasSteinmetz && mat.Cduct > 0);
        bool isSolid = (mat.Lam_d <= 0);  // no lamination → solid conductor
        bool ferromagneticSolid = (hasConductivity && isSolid && isFerromagneticSolid(mat));

        // Skip if no loss mechanism at all
        if (!hasSteinmetz && !hasConductivity) continue;
        if (mat.density <= 0) continue;

        // Rotor elements need inverse transform to find where they
        // were at each historical step (they move with the rotor group)
        bool isRotor = (motion.movingGroup > 0 &&
                        label.inGroup == motion.movingGroup);

        float cx = (float)elm.cx;
        float cy = (float)elm.cy;

        // Collect B and Az at each step
        double Bpeak = 0.0;
        double sumDbdt2 = 0.0;   // for dB/dt slab path
        int numDeriv = 0;
        float prevBx = 0, prevBy = 0;
        bool hasPrev = false;
        std::vector<float> azSeries;
        if (hasConductivity && isSolid && !ferromagneticSolid)
            azSeries.reserve(bHistory.size());

        for (size_t s = 0; s < bHistory.size(); s++) {
            float qx = cx, qy = cy;
            double pointAngle = 0.0;
            if (isRotor)
                pointAngle = std::atan2((double)cy - motion.cy, (double)cx - motion.cx);
            if (isRotor) {
                // At step s in bHistory, the rotor was at position
                // s * anglePerStep from its initial position.  The element's
                // current centroid (cx,cy) corresponds to the LAST step
                // (bHistory.size()-1).  To find where it was at step s,
                // rotate back by (last - s) steps.
                int stepsBack = (int)(bHistory.size() - 1) - (int)s;
                if (motion.isRotation) {
                    auto historyPoint = rotatePointToHistoryStep(qx, qy, motion, bHistory.size(), s);
                    qx = historyPoint.first;
                    qy = historyPoint.second;
                } else if (!motion.isRotation) {
                    qx -= (float)((double)stepsBack * motion.dx);
                    qy -= (float)((double)stepsBack * motion.dy);
                }
            }
            auto [bx, by, azVal] = indices[s].lookupAll(qx, qy, lbl);
            double Bmag = std::sqrt((double)bx * bx + (double)by * by);
            if (Bmag > Bpeak) Bpeak = Bmag;
            if (hasConductivity && isSolid && !ferromagneticSolid)
                azSeries.push_back(azVal);

            double diffBx = (double)bx;
            double diffBy = (double)by;
            if (ferromagneticSolid && isRotor && motion.isRotation) {
                auto [localBr, localBt] = rotateBToMaterialFrame((double)bx, (double)by,
                                                                 pointAngle, motion, bHistory.size(), s);
                diffBx = localBr;
                diffBy = localBt;
            }

            // Accumulate time derivatives
            if (hasPrev && dt > 0) {
                double dBxdt = (diffBx - (double)prevBx) / dt;
                double dBydt = (diffBy - (double)prevBy) / dt;
                sumDbdt2 += dBxdt * dBxdt + dBydt * dBydt;

                numDeriv++;
            }
            prevBx = (float)diffBx;
            prevBy = (float)diffBy;
            hasPrev = true;
        }

        // Also check the final solution's direct B value
        double Bfinal = std::sqrt(std::norm(elm.B1) + std::norm(elm.B2));
        if (Bfinal > Bpeak) Bpeak = Bfinal;

        double meanDbdt2 = (numDeriv > 0) ? sumDbdt2 / numDeriv : 0.0;
        // Compute loss based on material type
        double loss_Wm3 = 0.0;

        if (hasSteinmetz) {
            // Path 1: Explicit Steinmetz coefficients (laminated steel)
            double eKh = mat.Kh, eKc = mat.Kc, eKe = mat.Ke;

            // Auto-compute Kc for laminated steel with explicit Kh but no Kc
            if (eKc <= 0 && mat.Cduct > 0 && mat.Lam_d > 0) {
                double sigma_SI = mat.Cduct * 1e6;
                double d_m = mat.Lam_d * 1e-3;
                eKc = sigma_SI * M_PI * M_PI * d_m * d_m / 6.0;
            }

            loss_Wm3 = steinmetzLoss_Wm3(eKh, eKc, eKe, mat.alpha_loss,
                                           sweepFreq, Bpeak);
        } else if (ferromagneticSolid) {
            // Path 2: Solid ferromagnetic conductor (e.g. rotor backiron).
            // Use an auto-estimated physical thickness from the block geometry.
            // For annular rotor steel, apply a sector-by-sector radial bias
            // from the inner/outer dB/dt histories so the stator-facing side
            // can carry higher loss density than the outer surface.
            double x0 = lastDoc->nodes[elm.p[0]].x, y0 = lastDoc->nodes[elm.p[0]].y;
            double x1 = lastDoc->nodes[elm.p[1]].x, y1 = lastDoc->nodes[elm.p[1]].y;
            double x2 = lastDoc->nodes[elm.p[2]].x, y2 = lastDoc->nodes[elm.p[2]].y;
            double area = 0.5 * std::fabs((x1-x0)*(y2-y0) - (x2-x0)*(y1-y0));
            double radiusSum = 0.0;
            int radiusCount = 0;
            for (int k = 0; k < 3; k++) {
                int p = elm.p[k];
                if (p < 0 || p >= (int)lastDoc->nodes.size()) continue;
                double ndx = lastDoc->nodes[p].x - motion.cx;
                double ndy = lastDoc->nodes[p].y - motion.cy;
                radiusSum += std::sqrt(ndx * ndx + ndy * ndy);
                radiusCount++;
            }
            double rx = (double)cx - motion.cx;
            double ry = (double)cy - motion.cy;
            double radius = (radiusCount > 0)
                ? (radiusSum / (double)radiusCount)
                : std::sqrt(rx * rx + ry * ry);
            double angle = wrapAngle(std::atan2(ry, rx));

            if (options.accurateSolidLosses &&
                rotorDiffusionModels.find(lbl) != rotorDiffusionModels.end()) {
                AccurateRotorSeries series;
                series.elementIndex = i;
                series.labelIndex = lbl;
                series.density = mat.density;
                series.radius = radius;
                series.angle = angle;
                series.fallbackMeanDbdt2 = meanDbdt2;
                series.sigma_SI = mat.Cduct * 1e6;
                accurateRotorSeries.push_back(series);
            } else {
                FerroSolidSeries series;
                series.elementIndex = i;
                series.labelIndex = lbl;
                series.sigma_SI = mat.Cduct * 1e6;
                series.density = mat.density;
                series.areaWeight = std::max(area * L * L, 1e-30);
                series.baseMeanDbdt2 = meanDbdt2;
                series.radius = radius;
                series.angle = angle;
                ferroSeries.push_back(series);
            }
        } else if (hasConductivity && isSolid) {
            // Path 3: Az-based eddy current for low-μ solid conductors.
            // Defer final loss until we've computed the block-average dAz/dt
            // for each time interval; isolated solid regions should not pick
            // up loss from a uniform/common-mode potential drift.
            double x0 = lastDoc->nodes[elm.p[0]].x, y0 = lastDoc->nodes[elm.p[0]].y;
            double x1 = lastDoc->nodes[elm.p[1]].x, y1 = lastDoc->nodes[elm.p[1]].y;
            double x2 = lastDoc->nodes[elm.p[2]].x, y2 = lastDoc->nodes[elm.p[2]].y;
            double area = 0.5 * std::fabs((x1-x0)*(y2-y0) - (x2-x0)*(y1-y0));
            SolidConductorSeries series;
            series.elementIndex = i;
            series.labelIndex = lbl;
            series.sigma_SI = mat.Cduct * 1e6;   // MS/m → S/m
            series.density = mat.density;
            series.areaWeight = std::max(area * L * L, 1e-30);
            series.azSeries = std::move(azSeries);
            solidSeries.push_back(std::move(series));
        } else if (hasConductivity && !isSolid) {
            // Path 4: dB/dt slab formula for laminated conductors without Steinmetz
            // P = σ * d² * <(dB/dt)²> / 12   [W/m³]
            double sigma_SI = mat.Cduct * 1e6;
            double d_m = mat.Lam_d * 1e-3;
            loss_Wm3 = sigma_SI * d_m * d_m * meanDbdt2 / 12.0;
        }

        // Convert to specific loss (W/kg)
        double loss_Wkg = (mat.density > 0) ? loss_Wm3 / mat.density : 0.0;

        result.elementLosses[i].loss_Wkg = loss_Wkg;
        result.elementLosses[i].loss_Wm3 = loss_Wm3;
        result.elementLosses[i].Bpeak = Bpeak;
        result.elementLosses[i].freq = sweepFreq;
    }

    if (dt > 0.0 && bHistory.size() > 1 && !solidSeries.empty()) {
        const int nDeriv = (int)bHistory.size() - 1;
        std::map<int, std::vector<double>> blockWeightedDazdt;
        std::map<int, std::vector<double>> blockWeights;

        for (const auto &series : solidSeries) {
            auto &sumVec = blockWeightedDazdt[series.labelIndex];
            auto &wtVec = blockWeights[series.labelIndex];
            if (sumVec.empty()) {
                sumVec.assign(nDeriv, 0.0);
                wtVec.assign(nDeriv, 0.0);
            }
            for (int k = 0; k < nDeriv; k++) {
                double dAzdt = ((double)series.azSeries[k + 1] - (double)series.azSeries[k]) / dt;
                sumVec[k] += dAzdt * series.areaWeight;
                wtVec[k] += series.areaWeight;
            }
        }

        for (const auto &series : solidSeries) {
            double sumDazdt2 = 0.0;
            for (int k = 0; k < nDeriv; k++) {
                double dAzdt = ((double)series.azSeries[k + 1] - (double)series.azSeries[k]) / dt;
                double meanDazdt = 0.0;
                const auto &sumVec = blockWeightedDazdt[series.labelIndex];
                const auto &wtVec = blockWeights[series.labelIndex];
                if (wtVec[k] > 0.0)
                    meanDazdt = sumVec[k] / wtVec[k];
                double dAzdtEff = dAzdt - meanDazdt;
                sumDazdt2 += dAzdtEff * dAzdtEff;
            }

            double meanDazdt2 = sumDazdt2 / (double)nDeriv;
            double loss_Wm3 = series.sigma_SI * meanDazdt2;
            double loss_Wkg = (series.density > 0.0) ? loss_Wm3 / series.density : 0.0;
            result.elementLosses[series.elementIndex].loss_Wm3 = loss_Wm3;
            result.elementLosses[series.elementIndex].loss_Wkg = loss_Wkg;
        }
    }

    if (!ferroSeries.empty()) {
        std::map<int, double> labelProfileSum;
        std::map<int, double> labelProfileWeight;
        std::vector<double> profileValue(ferroSeries.size(), 0.0);

        for (size_t i = 0; i < ferroSeries.size(); i++) {
            const auto &series = ferroSeries[i];
            double profile = series.baseMeanDbdt2;
            auto it = rotorRadialModels.find(series.labelIndex);
            if (it != rotorRadialModels.end() && it->second.active) {
                const auto &model = it->second;
                double thickness = model.outerRadius - model.innerRadius;
                if (thickness > 1e-12) {
                    double t = (series.radius - model.innerRadius) / thickness;
                    t = std::max(0.0, std::min(1.0, t));
                    double inner = model.globalInnerMeanDbdt2;
                    double outer = model.globalOuterMeanDbdt2;
                    profile = (1.0 - t) * inner + t * outer;
                    labelProfileSum[series.labelIndex] += profile * series.areaWeight;
                    labelProfileWeight[series.labelIndex] += series.areaWeight;
                }
            }
            profileValue[i] = profile;
        }

        for (size_t i = 0; i < ferroSeries.size(); i++) {
            const auto &series = ferroSeries[i];
            double correctedMeanDbdt2 = series.baseMeanDbdt2;
            auto it = rotorRadialModels.find(series.labelIndex);
            if (it != rotorRadialModels.end() && it->second.active) {
                const auto &model = it->second;
                double thickness = model.outerRadius - model.innerRadius;
                if (thickness > 1e-12) {
                    double labelAvg = series.baseMeanDbdt2;
                    auto wtIt = labelProfileWeight.find(series.labelIndex);
                    if (wtIt != labelProfileWeight.end() && wtIt->second > 0.0)
                        labelAvg = labelProfileSum[series.labelIndex] / wtIt->second;
                    if (labelAvg > 1e-30) {
                        double scale = profileValue[i] / labelAvg;
                        scale = 1.0 + 0.85 * (scale - 1.0);
                        scale = std::max(0.25, std::min(4.0, scale));
                        correctedMeanDbdt2 = series.baseMeanDbdt2 * scale;
                    }
                }
            }

            double d_m = blockGeometry[series.labelIndex].characteristicThickness_m;
            if (d_m <= 0.0)
                d_m = 1e-9;
            double loss_Wm3 = series.sigma_SI * d_m * d_m * correctedMeanDbdt2 / 12.0;
            double loss_Wkg = (series.density > 0.0) ? loss_Wm3 / series.density : 0.0;
            result.elementLosses[series.elementIndex].loss_Wm3 = loss_Wm3;
            result.elementLosses[series.elementIndex].loss_Wkg = loss_Wkg;
        }
    }

    if (!accurateRotorSeries.empty()) {
        for (const auto &series : accurateRotorSeries) {
            auto it = rotorDiffusionModels.find(series.labelIndex);
            if (it == rotorDiffusionModels.end() || !it->second.active)
                continue;

            const auto &model = it->second;
            double thickness = model.outerRadius - model.innerRadius;
            if (thickness <= 1e-12 || model.numCells <= 0 || model.numSectors <= 0)
                continue;

            double t = (series.radius - model.innerRadius) / thickness;
            t = std::max(0.0, std::min(1.0, t));
            int cell = std::min(model.numCells - 1,
                                std::max(0, (int)std::floor(t * model.numCells)));
            int sec = std::min(model.numSectors - 1,
                               std::max(0, (int)std::floor(series.angle * model.numSectors / (2.0 * M_PI))));

            double loss_Wm3 = model.cellLoss_Wm3[sec][cell];
            if (loss_Wm3 <= 0.0) {
                double d_m = blockGeometry[series.labelIndex].characteristicThickness_m;
                if (d_m <= 0.0)
                    d_m = 1e-9;
                loss_Wm3 = series.sigma_SI * d_m * d_m * series.fallbackMeanDbdt2 / 12.0;
            }
            double loss_Wkg = (series.density > 0.0) ? loss_Wm3 / series.density : 0.0;
            result.elementLosses[series.elementIndex].loss_Wm3 = loss_Wm3;
            result.elementLosses[series.elementIndex].loss_Wkg = loss_Wkg;
        }
    }

    // Aggregate per-block summaries
    // Find unique block labels that have losses enabled
    std::map<int, BlockLossSummary> blockMap;

    for (int i = 0; i < numElm; i++) {
        const auto &elm = lastDoc->elements[i];
        int lbl = elm.lbl;
        if (lbl < 0 || lbl >= (int)lastDoc->labels.size()) continue;
        if (!lastDoc->labels[lbl].calculateLosses) continue;
        if (result.elementLosses[i].loss_Wkg <= 0 &&
            result.elementLosses[i].Bpeak <= 0) continue;

        int matIdx = lastDoc->labels[lbl].blockType;

        // Element area in m^2
        double x0 = lastDoc->nodes[elm.p[0]].x, y0 = lastDoc->nodes[elm.p[0]].y;
        double x1 = lastDoc->nodes[elm.p[1]].x, y1 = lastDoc->nodes[elm.p[1]].y;
        double x2 = lastDoc->nodes[elm.p[2]].x, y2 = lastDoc->nodes[elm.p[2]].y;
        double area = 0.5 * std::fabs((x1-x0)*(y2-y0) - (x2-x0)*(y1-y0));
        double areaM2 = area * L * L;

        // Stacking/fill factor from the material (lamFill)
        double sf = 1.0;
        if (matIdx >= 0 && matIdx < (int)lastDoc->materials.size()) {
            sf = lastDoc->materials[matIdx].lamFill;
            if (sf <= 0 || sf > 1.0) sf = 1.0;
        }

        // Total loss for this element in watts
        // P = loss_W/m3 * volume = loss_W/m3 * area * depth * stackingFactor
        double elemLoss_W = result.elementLosses[i].loss_Wm3
                          * areaM2 * depth * sf;

        auto &bs = blockMap[lbl];
        bs.blockIndex = lbl;
        if (matIdx >= 0 && matIdx < (int)lastDoc->materials.size())
            bs.materialName = lastDoc->materials[matIdx].blockName;
        bs.totalLoss_W += elemLoss_W;
        bs.avgLoss_Wkg += result.elementLosses[i].loss_Wkg * areaM2;  // area-weighted sum
        bs.avgBpeak += result.elementLosses[i].Bpeak * areaM2;
        if (result.elementLosses[i].loss_Wkg > bs.maxLoss_Wkg)
            bs.maxLoss_Wkg = result.elementLosses[i].loss_Wkg;
        bs.totalArea_m2 += areaM2;
        bs.numElements++;
    }

    // Finalize averages and collect summaries
    result.totalLoss_W = 0.0;
    for (auto &[lbl, bs] : blockMap) {
        if (bs.totalArea_m2 > 0) {
            bs.avgLoss_Wkg /= bs.totalArea_m2;
            bs.avgBpeak /= bs.totalArea_m2;
        }
        result.totalLoss_W += bs.totalLoss_W;
        result.blockSummaries.push_back(bs);
    }

    result.valid = true;
    return result;
}

// =============================================================
// Helper: derive operating frequency from motion config
//
// For rotary motors: freq = RPM * polePairs / 60
// For linear motors: freq = speed / (2 * polePitch)
// If motor module not used, estimate from sweep parameters:
//   freq = 1 / (total_sweep_time), but user should provide this
// =============================================================
inline double deriveFrequencyFromMotion(
    bool isRotation, double anglePerStep_deg, int numSteps,
    int polePairs, double rpm)
{
    if (rpm > 0 && polePairs > 0) {
        // Standard motor formula
        return rpm * (double)polePairs / 60.0;
    }

    // Fallback: assume one complete electrical cycle in the sweep
    // This is a rough estimate — user should ideally provide RPM
    if (isRotation && polePairs > 0) {
        double totalMechAngle = anglePerStep_deg * numSteps;
        double totalElecAngle = totalMechAngle * polePairs;
        // Assume 1 revolution per second as baseline (60 RPM)
        double totalTime = (totalMechAngle / 360.0);  // seconds at 60 RPM
        if (totalTime > 0)
            return totalElecAngle / 360.0 / totalTime;
    }

    return 0.0;  // Cannot determine frequency
}

#endif // IRONLOSS_H
