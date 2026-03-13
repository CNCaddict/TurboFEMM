// =============================================================
// Iron loss computation — three methods:
//
// 1. Steinmetz/Bertotti (laminated steel with explicit Kh/Kc/Ke):
//    P = Kh * f * Bpk^alpha + Kc * (f*Bpk)^2 + Ke * (f*Bpk)^1.5
//    Coefficients are in W/m³ units as stored in .fem files.
//
// 2. Az-based eddy current (solid conductors: σ > 0, Lam_d = 0):
//    P = σ * <(dAz/dt)²>   [W/m³]
//    Uses the time derivative of vector potential at each point.
//    Exact for 2D — spatial variation of Az across the conductor
//    naturally captures geometry-limited eddy current paths.
//    No characteristic dimension needed. For magnets, rotor iron, etc.
//
// 3. dB/dt slab formula (laminated conductors: σ > 0, Lam_d > 0):
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

// Iron loss computation result for the entire model
struct IronLossResult {
    std::vector<ElementLoss> elementLosses;   // indexed by element index
    std::vector<BlockLossSummary> blockSummaries;
    double totalLoss_W = 0.0;   // grand total watts
    double frequency = 0.0;      // operating frequency used
    bool valid = false;
};

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
//   - Conductive materials without Steinmetz (Cduct > 0, Lam_d = 0):
//     Uses Az-based eddy current: P = σ * <(dAz/dt)²>  [W/m³]
//     The spatial variation of Az across the conductor cross-section
//     naturally captures geometry-limited eddy current distribution.
//     No characteristic dimension needed — exact for 2D.
//   - Conductive materials with Lam_d > 0 but no Steinmetz:
//     Uses dB/dt slab formula: P = σ * d² * <(dB/dt)²> / 12
//     For laminated conductors without loss data sheets.
// =============================================================
inline IronLossResult computeIronLosses(
    const std::vector<BSnapshot> &bHistory,
    const ResultsDocument *lastDoc,
    double sweepFreq,
    double depth = 1.0,
    const MotionParams &motion = MotionParams{})
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
        // Path 2: Az-based eddy current (conductive, solid: Lam_d = 0)
        // Path 3: dB/dt slab eddy current (conductive, laminated: Lam_d > 0)
        bool hasSteinmetz = (mat.Kh > 0 || mat.Kc > 0 || mat.Ke > 0);
        bool hasConductivity = (!hasSteinmetz && mat.Cduct > 0);
        bool isSolid = (mat.Lam_d <= 0);  // no lamination → solid conductor

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
        double sumDazdt2 = 0.0;  // for Az-based path
        int numDeriv = 0;
        float prevBx = 0, prevBy = 0, prevAz = 0;
        bool hasPrev = false;

        for (size_t s = 0; s < bHistory.size(); s++) {
            float qx = cx, qy = cy;
            if (isRotor) {
                int stepsBack = motion.totalSteps - (int)s;
                if (motion.isRotation && stepsBack != 0) {
                    double backAngle = -(double)stepsBack * motion.anglePerStep
                                       * M_PI / 180.0;
                    double rx = (double)qx - motion.cx;
                    double ry = (double)qy - motion.cy;
                    double cosA = std::cos(backAngle);
                    double sinA = std::sin(backAngle);
                    qx = (float)(motion.cx + rx * cosA - ry * sinA);
                    qy = (float)(motion.cy + rx * sinA + ry * cosA);
                } else if (!motion.isRotation) {
                    qx -= (float)((double)stepsBack * motion.dx);
                    qy -= (float)((double)stepsBack * motion.dy);
                }
            }
            auto [bx, by, azVal] = indices[s].lookupAll(qx, qy);
            double Bmag = std::sqrt((double)bx * bx + (double)by * by);
            if (Bmag > Bpeak) Bpeak = Bmag;

            // Accumulate time derivatives
            if (hasPrev && dt > 0) {
                double dBxdt = ((double)bx - (double)prevBx) / dt;
                double dBydt = ((double)by - (double)prevBy) / dt;
                sumDbdt2 += dBxdt * dBxdt + dBydt * dBydt;

                double dAzdt = ((double)azVal - (double)prevAz) / dt;
                sumDazdt2 += dAzdt * dAzdt;

                numDeriv++;
            }
            prevBx = bx;
            prevBy = by;
            prevAz = azVal;
            hasPrev = true;
        }

        // Also check the final solution's direct B value
        double Bfinal = std::sqrt(std::norm(elm.B1) + std::norm(elm.B2));
        if (Bfinal > Bpeak) Bpeak = Bfinal;

        double meanDbdt2 = (numDeriv > 0) ? sumDbdt2 / numDeriv : 0.0;
        double meanDazdt2 = (numDeriv > 0) ? sumDazdt2 / numDeriv : 0.0;

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
        } else if (hasConductivity && isSolid) {
            // Path 2: Az-based eddy current for solid conductors
            // P = σ * <(dAz/dt)²>   [W/m³]
            // Exact for 2D — Az spatial variation captures geometry limits
            // on eddy current loops. No slab dimension needed.
            double sigma_SI = mat.Cduct * 1e6;   // MS/m → S/m
            loss_Wm3 = sigma_SI * meanDazdt2;
        } else if (hasConductivity && !isSolid) {
            // Path 3: dB/dt slab formula for laminated conductors without Steinmetz
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
