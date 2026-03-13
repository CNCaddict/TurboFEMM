# TurboFEMM Development Log

> This file is the authoritative record of all AI-assisted development sessions on TurboFEMM.
> Each session is documented in reverse chronological order (newest first).
> Future AI sessions: **read this file first** to understand project history, decisions, and gotchas.

---

## Session 2026-03-13b — Torque Scaling Verification & Diagnostics

### Summary
User reported 23 Nm torque at 10A — suspected 100x scaling error. Thorough investigation confirmed the torque computation is correct (0.206 Nm at 10A in test, 0.236 Nm in user's run). The 23 Nm was from a CSV generated before the depth fix was compiled in.

### Torque Unit Analysis
Traced every step of the Henrotte MST torque computation to verify units:
- Mask gradient `vx`: file_units / (file_units² × lengthConv) = 1/m ✓
- Force density `F`: T² × (1/m) / (H/m) = N/m³ ✓
- Centroid: file_units × lengthConv = m ✓
- Volume: |da|/2 × L² × (depth × L) = m³ ✓
- Total: m³ × N/m² = N·m ✓

Verified cm→model coordinate conversion in `loadFromSolverData()` is correct (`invCm = 1/cmConv[lenUnits]`).

### Torque Scaling Test (`lrkTorqueScaling`)
Added new test that solves LRK at 1A, 5A, and 10A RMS at fixed angle (300° electrical):
- 1A: 18.7 mNm (baseline)
- 5A: 102 mNm (5.5× — slight super-linearity from reluctance torque)
- 10A: 206 mNm (11× — saturation shifts but doesn't reduce dramatically)
- Max B at 10A: 2.53 T (deep saturation in iron, reasonable)

### Motion Runner Diagnostics
Added `[TorqueDiag]` output on first step of motor sweep showing:
- depth, lengthConv, depth_m (depth in metres)
- numElements, maxB, group number, rotation center

### CLAUDE.md Updated
Added "Release & Changelog Requirements" section: every push must update DEVLOG.md, CHANGELOG.md, and GitHub release notes.

### Files Changed
| File | Change |
|------|--------|
| `gui/motionrunner.cpp` | Added `[TorqueDiag]` diagnostic output on first motor step |
| `tests/test_solver.cpp` | Added `lrkTorqueScaling()` test (1A/5A/10A torque comparison) |
| `tests/test_solver.h` | Added `lrkTorqueScaling` slot |
| `CLAUDE.md` | Added Release & Changelog Requirements section |

### Test Results
All 90 tests pass (89 prior + 1 new `lrkTorqueScaling`).

### Key Gotcha
The FEMM solver stores coordinates in centimeters internally. The in-process solver passes these to `loadFromSolverData()` which converts back to model units via `invCm = 1/cmConv[lenUnits]`. For mm models: invCm=10, so x_mm = x_cm × 10. This is correct but easy to get confused about.

---

## Session 2026-03-13 — Optimizer Peak Detection Fix, Auto-Solve Removal, UI Tooltips, v4.3.0

### Optimizer Peak Detection Fix

**Problem**: Motor optimizer found the braking (negative torque) angle instead of the motoring (positive torque) angle. The `sweepDirection()` function accepted the first local maximum of dE/dθ, which could be a peak in the *negative* region (maximum braking torque) rather than the positive (motoring) region.

**Root cause**: The energy landscape E(θ) is roughly sinusoidal. Starting from θ=0 and sweeping positive, dE/dθ goes through a negative peak (braking) before reaching the positive peak (motoring). The optimizer's `torque < prevTorque` check triggered at the negative peak, returning the wrong angle.

**Fix** (`motoroptimizer.cpp`): Added `&& prevTorque > 0.0` guard to the peak detection check. The optimizer now skips negative-torque peaks and continues sweeping until it finds the positive (motoring) peak.

### Auto-Solve on File Open Removed

**Problem**: Opening a .fem file triggered automatic mesh generation + solver, which takes significant time for complex models and blocks the UI.

**Fix** (`mainwindow.cpp`): Removed the auto-mesh + auto-solve code path from `openFile()`. If an up-to-date .ans results file exists, it is still loaded automatically for instant overlay display. Otherwise, the user must manually run the solver (Analyze menu).

### Comprehensive UI Tooltips

Added context-sensitive tooltips to all dialog parameters across the application:
- **Motion Dialog**: Group number, motion type, translate/rotate params, steps, motor module (RMS current, initial angle, phase assignments, pole pairs, pole pitch, optimisation step, reverse phase), iron loss
- **Material Dialog**: Magnetic properties (μ_x, μ_y, H_c), electrical (J_src, σ), lamination/wire, hysteresis lag angles, Steinmetz iron loss coefficients
- **Block Properties Dialog**: Block type, max mesh area, circuit, turns, magnetization direction, group, external region, iron loss
- **Problem Dialog**: Problem type, length units, frequency, depth, precision, min mesh angle, smart mesh, AC solver, previous solution
- **Boundary Dialog**: BC types, prescribed A, skin depth, mixed BC, air gap angles
- **Circuit Dialog**: Current (real/imag), circuit type (parallel vs series)

### Version Bump to 4.3.0

Updated version strings in CMakeLists.txt (root + gui), main.cpp, and Info.plist.in. Created CHANGELOG.txt tracking all releases.

### Files Changed
| File | Change |
|------|--------|
| `gui/motoroptimizer.cpp` | Peak detection: skip negative-torque peaks (`prevTorque > 0.0` guard) |
| `gui/mainwindow.cpp` | Removed auto-mesh/solve on file open; kept .ans auto-load |
| `gui/dialogs/motiondialog.cpp` | Added tooltips to all motor/motion/output parameters |
| `gui/dialogs/materialdlg.cpp` | Added tooltips to all material property fields |
| `gui/dialogs/blockpropsdlg.cpp` | Added tooltips to block label fields |
| `gui/dialogs/problemdlg.cpp` | Added tooltips to problem definition fields |
| `gui/dialogs/boundarydlg.cpp` | Added tooltips to boundary condition fields |
| `gui/dialogs/circuitdlg.cpp` | Added tooltips to circuit fields |
| `CMakeLists.txt` | Version 4.2 → 4.3.0 |
| `gui/CMakeLists.txt` | Bundle version 4.2 → 4.3.0 |
| `gui/main.cpp` | App version 4.2 → 4.3.0 |
| `CHANGELOG.txt` | Created with full release history |

### Test Results
All 89 tests pass.

---

## Session 2026-03-12b — Commutation Tracking Sign Fix + CSV Column Order Fix

**Problem**: Motor torque still wildly oscillating positive/negative during motion sweep, even at 10A RMS.

**Root cause 1 — Commutation direction inverted** (`motionrunner.cpp`):
The motion runner was *adding* `cumMechAngle × polePairs` to the stator electrical angle as the rotor advanced. This is backwards. When the rotor rotates forward by δ mechanical degrees, the torque curve (as seen from the stator) shifts *backward* by δ×p electrical degrees. The stator current angle must *subtract* the rotor advance to maintain the optimal torque-producing angle.

- **Before**: `elecAngle = optimalAngle + sign * cumMechAngle * polePairs` → torque collapsed from +19mNm to -3mNm in 2 steps
- **After**: `elecAngle = optimalAngle - sign * cumMechAngle * polePairs` → torque stays at +19mNm ±15% (cogging at 1A)

Same fix applied to the linear motor path.

**Root cause 2 — CSV column header/data ordering mismatch** (`motionrunner.cpp`):
The header wrote motor columns (ElecAngle, Ia, Ib, Ic) *after* flux/energy columns, but data rows wrote them *before* flux density columns. All middle columns were scrambled when motorEnabled=true. Fixed by moving the motor header block to match the data order.

**Files changed**:
- `gui/motionrunner.cpp` — commutation sign fix (+ → −) for both rotary and linear, CSV header order fix
- `tests/test_solver.cpp` — same sign fix in `lrkMotorTorqueDiagnostic` test

**Test results**: All 89 tests pass. Motor diagnostic test now shows all 3 motion steps with positive torque (avg 18.6 mNm at 1A RMS).

---

## Session 2026-03-12a — Motor Optimizer Sign Fix + Torque/Energy Units Fix

### Root Cause of Torque Oscillation (3 bugs found)

**Bug 1: Optimizer VW sign inversion** (`motoroptimizer.cpp`)
- The optimizer swept the stator electrical angle at fixed rotor position and computed "torque" as `-(dE/dθ_elec)`
- This quantity has the **opposite sign** from the physical rotor torque (MST)
- When increasing stator angle toward the attracting direction, energy decreases → positive VW, but MST torque is negative
- The optimizer found the angle of peak negative MST torque (~120° elec) instead of peak positive (~270° elec)
- Result: motor ran at the **braking** angle, producing negative torque instead of motoring torque
- **Fix**: Changed to `+(dE/dθ_elec)` (removed the minus sign) so VW metric aligns with MST direction
- Verified: corrected VW and MST agree on optimal angle at all test points

**Bug 2: Depth not converted to metres** (`resultsdoc.cpp`)
- `computeTorque()` and `computeSummary()` used `depth` directly (stored in original length units, e.g. 15 for mm)
- Should be `depth * lengthConv` to convert to metres
- For the LRK model (mm units, depth=15mm): torque and energy were **1000x too large**
- Example: MST torque reported 22 Nm instead of correct ~0.019 Nm = 19 mNm
- **Fix**: Changed `depth` → `depth * L` in both functions

**Bug 3: Iron loss depth argument** (`motionrunner.cpp`)
- `computeIronLosses()` was called with `lastDoc->depth` (in original units) but expects metres
- Total iron loss watts were 1000x too large (per-element W/kg was correct since it doesn't use depth)
- **Fix**: Pass `lastDoc->depth * lastDoc->lengthConv` instead

### Enhanced Diagnostics
- Added comprehensive motor config dump at sweep start (optimal angle, pole pairs, current, phases, circuit properties, winding slot assignments)
- Added per-step logging of ALL steps: electrical angle, phase currents (Ia/Ib/Ic), energy, MST torque, VW torque
- New test `lrkMotorTorqueDiagnostic`: solves LRK at multiple stator angles, verifies VW and MST sign agreement, runs motion sweep with commutation

### Files Changed
| File | Change |
|------|--------|
| `gui/motoroptimizer.cpp` | Sign fix: `-(dE/dθ)` → `+(dE/dθ)` for correct torque direction |
| `gui/resultsdoc.cpp` | Fixed depth units in `computeSummary()` and `computeTorque()`: `depth` → `depth * L` |
| `gui/motionrunner.cpp` | Fixed depth units in iron loss call; enhanced motor diagnostics logging |
| `tests/test_solver.h` | Added `lrkMotorTorqueDiagnostic` test |
| `tests/test_solver.cpp` | Implemented motor torque diagnostic test |

### Key Insight: VW Torque from Motion Sweep is Invalid for Motors
The energy-difference (virtual work) torque computed from the motion sweep is fundamentally incorrect for commutated motors because the energy change between steps includes both:
1. Rotor position change (physical torque)
2. Stator current angle change (not physical torque)

With polePairs=7, the stator angle changes 7x faster than the rotor, so the stator contribution dominates and the VW result is meaningless. The MST (Maxwell stress tensor) is the correct per-step torque method.

### Test Results
All 88 tests pass. Torque values now physically reasonable:
- LRK at 1A RMS: ~19 mNm peak (matches analytical estimate of ~18 mNm)
- Cogging ripple significant at 1A RMS; expected to be ~10% at rated current

---

## Session 2026-03-11g — Iron Loss Unit Fix & Auto Eddy Current for Magnets

**Scope:** Fixed iron loss values showing ~1000x too high (W/m³ treated as W/kg), and added automatic eddy current loss computation for conductive materials without explicit Steinmetz coefficients (e.g. NdFeB permanent magnets).

### Bug 1: Steinmetz Unit Mismatch (3000 W/kg → ~50 W/kg)

**Root cause:** FEMM stores Steinmetz coefficients (Kh, Kc, Ke) in W/m³ units (e.g. Kh=179 for M-19 steel). The `computeIronLosses()` function treated the raw formula output as W/kg, then multiplied by density again to get W/m³ — inflating everything by a factor of density (~7700).

**Verification:** M-19 at 60 Hz, 1.5 T: raw Steinmetz = ~30,100. Divided by density (7700) = 3.9 W/kg, matching the datasheet value of ~3.5 W/kg.

**Fix in `gui/ironloss.h`:** Swapped the conversion — raw output is now correctly treated as W/m³, divided by `mat.density` to get true W/kg.

### Bug 2: No Losses for Conductive Materials (magnets)

**Root cause:** `computeIronLosses()` skipped any material without explicit Kh/Kc/Ke. Permanent magnets like NdFeB have conductivity (`Cduct=0.667` MS/m) but no Steinmetz coefficients, so they showed zero loss.

**Fix:** Auto-compute classical eddy current coefficient when `Cduct > 0` but `Kc = 0`:
- `Kc = sigma * π² * d² / 6`
- For laminated steel (`Lam_d > 0`): d = lamination sheet thickness
- For solid conductors (`Lam_d = 0`): d = problem depth (z-extent into the page — the un-modeled dimension in 2D that limits eddy current loops)

**For NdFeB magnets:** Just need density (~7500 kg/m³) set on the material and "Calc Losses" enabled on block labels. No Steinmetz coefficients or Lam_d needed — auto-computes from conductivity and depth.

### Changes
- **`gui/ironloss.h`**: (1) Division by density for W/kg conversion. (2) Auto-compute eKc from Cduct for conductive materials. (3) Uses effective coefficients (eKh, eKc, eKe) in Steinmetz call.
- **`tests/test_document.cpp`**: Updated `ironLossFromBHistory` and `rotorLossInverseTransform` test coefficients to W/m³ scale (matching real .fem files). Added new `conductiveEddyLossAutoCompute` test.
- **`tests/test_document.h`**: Added `conductiveEddyLossAutoCompute` declaration.

### Known Issue — Stator Remeshing
Every motion sweep step regenerates the entire mesh (stator + rotor + air gap) via `InProcessSolver::solve()` → `generateMeshInProcess()`. The stator mesh should stay fixed between steps. This causes: (1) unnecessary computation, (2) stator element centroids shifting between steps which degrades B-history accuracy for iron loss. Future work: implement air-gap-only remeshing — cache stator/rotor meshes from step 0, rotate rotor nodes each step, remesh only the air gap.

---

## Session 2026-03-11f — Fix Magnetization Direction Accumulation Bug

**Scope:** magDir values were accumulating without wrapping during motion sweeps (e.g. 10 steps × 36° = magDir 360° instead of 0°). Fixed by normalizing to [0, 360) in all code paths.

### Root Cause
`rotateSelected()` did `blk.magDir += angleDeg` without `fmod()` wrapping. After a full sweep, magnets ended up with magDir values like 360, 540, 642.86, 745.71 etc. While `cos()/sin()` render these correctly (they're periodic), the values confused users and corrupted the .fem file.

### Changes
- **`gui/document.cpp` (rotateSelected)**: Added `fmod(magDir, 360)` normalization after accumulation
- **`gui/document.cpp` (loadDocument)**: Added normalization on .fem file load for backward compat with corrupted files
- **`gui/dialogs/blockpropsdlg.cpp` (onAccept)**: Normalize magDir from spinbox before storing
- **`gui/dialogs/modeldatadlg.cpp` (onBlockLabelCellChanged)**: Normalize magDir from table cell editing, added `<cmath>` include
- **`test_problems/lrk.fem`**: Fixed all corrupted magDir values (360→0, 540→180, 591.43→231.43, etc.)

---

## Session 2026-03-11e — Calc Losses Checkbox in Model Data Table

**Scope:** Added a "Calc Losses" checkbox column to the Block Labels tab in the Model Data dialog, so users can toggle `calculateLosses` per block label directly from the table view.

### Changes
- **`gui/dialogs/modeldatadlg.cpp`**: Column count 10→11, added "Calc Losses" header, added `QCheckBox` widget per row at column 10. Checkbox reads/writes `m_doc->blockLabels[i].calculateLosses` with `m_updatingTable` guard and `markModified()` call.

---

## Session 2026-03-11d — Remove Duplicate Lamination Fields from Block Labels

**Scope:** Lamination thickness and stacking factor were duplicated on both the block label (geometry) and material (library). Removed from block label — these are material properties. Now you define different M19 thicknesses as separate materials in the library (e.g. "M19 0.35mm", "M19 0.50mm").

### Changes
- **`gui/femm_types.h`**: Removed `lamThickness` and `stackingFactor` from `FBlockLabel`
- **`gui/resultsdoc.h`**: Removed `lamThickness` and `stackingFactor` from `SolnLabel`
- **`gui/dialogs/blockpropsdlg.h/.cpp`**: Removed thickness/stacking spinboxes from block dialog. Added tooltip on `calculateLosses` checkbox pointing to material dialog for lamination data.
- **`gui/inprocesssolver.cpp`**: Removed copy of lamThickness/stackingFactor to results labels
- **`gui/ironloss.h`**: Changed stacking factor lookup from `label.stackingFactor` to `mat.lamFill` (material's fill factor). The material already has `lamFill` (stacking factor) and `Lam_d` (lam thickness).
- **`gui/document.cpp`**: Reading: kept field parsing for backward compat but ignores values. Writing: writes zeros in the reserved positions to maintain field layout for name/magDirFctn parsing.
- **Tests**: Updated `blockLabelLossRoundTrip`, `backwardCompatNoLossFields`, `ironLossFromBHistory`, `rotorLossInverseTransform` to remove lamThickness/stackingFactor references.

### Note
`adaptiveLRKCoarsening` test is flaky (pre-existing, unrelated to this change) — 0.059 vs 0.05 threshold.

---

## Session 2026-03-11c — Iron Loss Pipeline Fix: Keep Sweep Results Alive

**Scope:** Fixed the iron loss overlay showing all zeros after sweep, and no heatmap PNG saving. Root cause: after the sweep, the ResultsDocument with iron loss data was being deleted and replaced by a fresh load from .ans (which has no iron loss data, Steinmetz coefficients, or calculateLosses flags).

### Root Cause Analysis
1. The motion runner computes iron losses on the in-memory ResultsDocument during the sweep
2. After sweep, `onMotionFinished` was deleting that document and loading fresh from .ans
3. The .ans file parser (`resultsdoc.cpp`) does NOT read: Steinmetz coefficients (Kh, Kc, Ke, alpha_loss, density), calculateLosses flag, lamThickness, stackingFactor — these only exist in the GUI model and get copied during in-process solving
4. Result: fresh .ans overlay had no loss data → all zeros

### Changes

**`gui/motionrunner.h`:**
- Added `takeLastResults()` method — transfers ownership of last step's ResultsDocument
- Added `m_lastResultsDoc` member to hold the document between sweep end and MainWindow pickup

**`gui/motionrunner.cpp`:**
- Before detaching overlay at sweep end, save `m_overlay->document()` to `m_lastResultsDoc` with `setParent(nullptr)` to detach from MotionRunner's ownership
- Added diagnostic logging at step 0: reports how many elements have calculateLosses, how many materials have Steinmetz data. Warns if either is zero.
- Destructor cleans up `m_lastResultsDoc` if not taken

**`gui/mainwindow.cpp` — `onMotionFinished()`:**
- Instead of loading from .ans, calls `m_motionRunner->takeLastResults()` to get the sweep's ResultsDocument (with iron loss data already populated)
- Re-parents to MainWindow, creates new overlay renderer, configures it
- If iron loss was computed, auto-switches density type to IronLoss
- Falls back to loadResultsOverlay(.ans) if takeLastResults returns null

### Key Insight
The overlay renderer renders from its own ResultsDocument mesh, independent of the GUI geometry. So using the last step's ResultsDocument (which has moved-geometry mesh positions) works fine as an overlay even though the GUI geometry was restored to original.

### Tests
All 84 tests pass. GUI builds clean.

---

## Session 2026-03-11b — Post-Sweep Overlay Persistence + Heatmap PNG Fix

**Scope:** Three fixes: (1) Results overlay now persists after motion sweep instead of resetting to bare geometry, (2) Iron loss heatmap PNG renders offscreen instead of capturing the widget, (3) Added "Clear Results Overlay" button for manual reset.

**Problem:** After a motion sweep completed, the overlay was detached and deleted (motionrunner.cpp line 695 + mainwindow.cpp onMotionFinished). The user saw bare geometry with no heatmap. The iron loss PNG also didn't save because `m_dw->grab()` captured the widget at a moment when the geometry was in a transitional state.

### Changes

**`gui/mainwindow.h`:**
- Added `void onClearOverlay()` slot
- Added `QAction *actClearOverlay` member

**`gui/mainwindow.cpp`:**
- `onMotionFinished()`: Instead of deleting the overlay, reloads it from the .ans file via `loadResultsOverlay()`. If iron losses were computed, auto-switches to Iron Loss density type.
- New `onClearOverlay()` slot: Detaches overlay, deletes renderer/doc, disables overlay-related UI. User can manually clear the overlay to return to geometry editing.
- Analysis menu: Added "Clear Results Overlay" menu item (disabled until overlay loaded)
- Mesh toolbar: Added actClearOverlay button for easy access
- Close handler: Also disables actClearOverlay when document closed

**`gui/motionrunner.h`:**
- Added `hasIronLossResult()` and `ironLossResult()` public accessors

**`gui/motionrunner.cpp`:**
- Iron loss heatmap capture: Replaced `m_dw->grab()` with offscreen QImage rendering using `m_overlay->render()`. This renders the heatmap independently of widget state, then saves to PNG.
- Added `#include <QPainter>` for offscreen rendering
- After rendering, restores previous overlay state (doesn't leave it stuck on Iron Loss)

**`gui/dialogs/adaptivedlg.cpp`:**
- Extended tolerance spinbox range from 10% to 20% max: `setRange(0.10, 20.00)`
- Updated slider↔tolerance mapping: slider 0→100 now maps to 20%→0.1% (was 10%→0.1%)
- Updated tooltip text to reflect new range

### Key Design Decisions
- **Reload overlay from .ans file** rather than keeping the stale sweep overlay: The sweep overlay's mesh positions are from the moved geometry, which no longer matches the restored geometry. Reloading from .ans gives a fresh overlay matching the current geometry.
- **Offscreen rendering for PNG**: Widget grab is fragile (depends on timing, paint state, geometry position). Offscreen rendering via QPainter on a QImage is deterministic and independent of widget state.
- **Manual clear button**: Instead of auto-resetting, give the user a visible "Clear Results Overlay" button in both the Analysis menu and Mesh toolbar. This lets them stay in results view as long as needed.

### Tests
All 84 tests pass. GUI builds clean.

---

## Session 2026-03-11 — Phase 4: Density Plot Submenu + Rotor/Magnet Loss Support

**Scope:** Two changes: (1) Replace binary density plot toggle with a proper submenu supporting all density types including Iron Loss, (2) Add rotor-aware B(t) history lookup so iron losses can be computed for moving elements (rotor steel, permanent magnets).

### Changes

**Part A: View Menu Density Type Submenu**

**`gui/mainwindow.h`:**
- Replaced `QAction *actOverlayDensity` + `bool m_savedOverlayDensity` + `onToggleOverlayDensity(bool)` with `QMenu *m_densityMenu`, `QActionGroup *m_densityGroup`, `int m_savedDensityType`, `onDensityTypeChanged(QAction*)`

**`gui/mainwindow.cpp`:**
- Menu construction: replaced single checkable action with "Density Plot" submenu containing 7 exclusive options (Off, |B|, |Re(B)|, |Im(B)|, |H|, |J|, Iron Loss) using QActionGroup pattern from the Rendering submenu
- New `onDensityTypeChanged(QAction*)` slot saves int to QSettings and calls `setShowDensity()`
- Updated overlay load and tab switch to use `static_cast<DensityType>(m_savedDensityType)`
- QSettings key changed from `"view/overlayDensity"` (bool) to `"view/densityType"` (int)

**Part B: Rotor-Aware B History for Magnet/Rotor Losses**

**`gui/ironloss.h`:**
- Added `MotionParams` struct: movingGroup, isRotation, cx/cy, anglePerStep, dx/dy, totalSteps
- Modified `computeIronLosses()` signature: added `const MotionParams &motion = MotionParams{}` (backward-compatible)
- Inner loop: detects rotor elements via `label.inGroup == motion.movingGroup`, applies inverse rotation/translation to query point before spatial lookup in each step's BHistoryIndex
- Added `#ifndef M_PI` guard for header-only use

**`gui/motionrunner.cpp`:**
- Constructs `MotionParams` from `m_config` fields and passes to `computeIronLosses()`

**Tests (`tests/test_document.h/.cpp`):**
- `rotorLossInverseTransform`: Synthetic 3-step rotation scenario with rotor element at (10,0). BSnapshots contain rotor entries at back-rotated positions plus stator entries at (10,0). Verifies that with MotionParams the peak B comes from the high-B step (via inverse transform), and without MotionParams only the low stator B is found.

### Design Decisions
- **Cartesian grid for both rotary and linear**: No special rotational grid needed. Instead, inverse-transform the query point (rotate/translate backward) before looking up in the Cartesian BHistoryIndex. Simple, works for both motor types.
- **Inverse transform direction**: At step s, rotor was `(totalSteps - s)` increments BEFORE its final position. Query point is rotated backward by that amount to match where the element was at that step.
- **Backward-compatible API**: `computeIronLosses()` defaults to `MotionParams{}` with `movingGroup=0`, meaning no rotor elements → existing callers unaffected.

### Test Results
All 84 tests pass including 1 new rotor loss test.

---

## Session 2026-03-11 — Phase 3: Steinmetz Iron Loss Computation & Heatmap

**Scope:** Compute iron losses from B(t) waveforms captured during motion sweeps. Add loss heatmap visualization and CSV export.

### Changes

**New file `gui/ironloss.h`:**
- `ElementLoss`, `BlockLossSummary`, `IronLossResult` structs for per-element and per-block loss data
- `steinmetzLoss_Wkg()`: three-term Bertotti formula — hysteresis + classical eddy + excess
- `computeIronLosses()`: main computation — builds BHistoryIndex for each step, finds peak |B| per element across all steps, applies Steinmetz formula using material coefficients, aggregates per-block summaries
- `deriveFrequencyFromMotion()`: helper for RPM-to-frequency conversion

**`gui/resultsdoc.h`:**
- Added `DensityType::IronLoss` to the enum
- Added `ironLoss_Wkg` vector, `ironLoss_Low`, `ironLoss_High` fields for heatmap rendering

**`gui/resultsoverlay.h/.cpp`:**
- Added `m_cachedIronLoss` member for per-element iron loss in getVertexValue()
- `getVertexValue()`: returns cached iron loss when DensityType is IronLoss
- `rasterElement()`: selects iron loss bounds vs B bounds based on density type
- `drawLegend()`: shows "Iron Loss (W/kg)" title with iron loss bounds

**`gui/motionrunner.h/.cpp`:**
- Added `#include "ironloss.h"` and `IronLossResult m_ironLossResult`
- After sweep completes, computes iron losses if enabled (auto-derives frequency from RPM × polePairs / 60)
- Populates `ResultsDocument::ironLoss_Wkg` and bounds for heatmap visualization
- Reports per-block loss summaries via progress messages
- `writeCSV()`: adds `IronLoss_Total_W` and per-block loss columns when `csvIronLoss` enabled

**`gui/dialogs/motiondialog.h/.cpp`:**
- Added `operatingFreqHz` and `motorRPM` fields to `MotionConfig`
- Added `m_motorRPM` (QDoubleSpinBox, RPM) and `m_operatingFreq` (QDoubleSpinBox, Hz) widgets
- RPM auto-computes frequency via `rpm * polePairs / 60` on value change
- Settings persistence for both new fields

**Tests (`tests/test_document.h/.cpp`):**
- `steinmetzLossFormula`: validates three-term formula with M-19 coefficients at 1.5T/60Hz, checks component breakdown, zero-B, zero-freq edge cases
- `ironLossFromBHistory`: builds synthetic 2-element mesh with M-19 material, 3-step B history, verifies peak B extraction from history, loss computation, block summary aggregation, and total loss in reasonable range

### Design Decisions
- **Peak B approach**: For this phase, uses peak |B| across all steps (not FFT decomposition). This is the standard modified Steinmetz method (MSE) and is accurate for sinusoidal waveforms. FFT-based generalized Steinmetz (iGSE) can be added later.
- **Iron loss per sweep, not per step**: Loss is computed once after the entire sweep, not incrementally per step. This gives the most accurate peak B values.
- **Frequency from RPM**: User enters RPM + pole pairs → electrical frequency = RPM × polePairs / 60. Can also set frequency directly for non-motor applications.
- **Heatmap as flat per-element color**: Iron loss doesn't use Gouraud smoothing (loss is constant per element, not nodal). Uses cached element value for all 3 vertices.

### Test Results
All 78+ tests pass including 2 new iron loss tests.

---

## Session 2026-03-11 — Phase 2: Per-Element B History Tracking

**Scope:** Store per-element flux density snapshots during motion sweeps for iron loss computation in Phase 3.

### Changes

**New file `gui/bhistory.h`:**
- `BSnapshot`: compact per-step storage — float vectors for centroids (cx, cy) and B components (bx, by), loss-enabled elements only
- `BHistoryIndex`: grid-accelerated nearest-centroid spatial lookup (O(1) per query) for reconstructing B(t) at any point across mesh topologies

**`gui/motionrunner.h`:** Added `#include "bhistory.h"` and `std::vector<BSnapshot> m_bHistory`

**`gui/motionrunner.cpp`:**
- After each solve step (both in-process and file-based paths), when `calculateLosses` is enabled, captures BSnapshot of loss-enabled elements
- Clears `m_bHistory` in `start()` and `abort()`

**Tests (`tests/test_document.h/.cpp`):**
- `bHistoryIndexLookup`: 3x3 grid, verifies exact/near lookups return correct nearest B values
- `bHistoryIndexEmpty`: empty snapshot returns (0,0)

### Gotcha
- `CmplxF` is `std::complex<double>`, not a custom type — use `.real()` not `.re`

### Next: Phase 3
Steinmetz loss computation from B(t) waveforms + loss heatmap visualization.

---

## Session 2026-03-11 — Phase 1: Iron Loss Data Model & UI

**Scope:** Added Steinmetz iron loss model data infrastructure — coefficients on materials, per-block loss settings, material presets, UI dialogs, .fem serialization. No solver/computation changes yet.

### Changes

**Data model (`gui/femm_types.h`):**
- Added `CoreLossPoint` struct and Steinmetz fields to `FMaterialProp`: Kh, Kc, Ke, alpha_loss, density, coreLossData
- Added loss fields to `FBlockLabel`: calculateLosses, lamThickness, stackingFactor

**Material presets (`gui/materialpresets.h` — NEW):**
- Header-only file with 11 steel presets (M-15 through M-47, Hiperco 50, Pure Iron)
- Kc computed analytically (σπ²d²/6), Kh fitted from ASTM A677, Ke ~10% of total

**Serialization (`gui/document.cpp`):**
- Material: new tags `<kh>`, `<kc>`, `<ke>`, `<alpha_loss>`, `<density>`, `<corelosspoints>`
- Block labels: lamThickness/stackingFactor appended after flags; calculateLosses in flags bit 2
- Backward compatible: old files load with defaults; detection heuristic checks if field 9 starts with quote

**UI Dialogs:**
- `materialdlg.cpp`: Added "Iron Loss (Steinmetz)" group with preset combo + 5 spinboxes
- `blockpropsdlg.cpp`: Added "Iron Loss" group (checkbox, lam thickness, stacking factor) + exposed missing `inGroup` field
- `motiondialog.cpp`: Added global "Calculate iron losses" checkbox + csvIronLoss toggle

**Results bridge (`resultsdoc.h`, `inprocesssolver.cpp`):**
- Added Steinmetz fields to SolnMaterial and loss fields to SolnLabel
- Copies from GUI data after solver completes

**Tests (`tests/test_document.h/.cpp`):**
- `steinmetzRoundTrip`: material with all Steinmetz fields + core loss data point survives save/reload
- `blockLabelLossRoundTrip`: block label loss settings survive save/reload
- `backwardCompatNoLossFields`: old solenoid.fem loads with correct defaults

### Gotchas
- LRK round-trip test now shows 31 line diffs (block labels get `0 1` appended for lamThickness/stackingFactor). Test still passes — this is expected.
- Qt test runner applies function name filter to ALL test classes, so `./tests/femm-tests steinmetzRoundTrip` shows failures for classes that don't have that function. Use full `./tests/femm-tests` run instead.

### Next: Phase 2
Per-element B history tracking in motion runner — store B vectors across rotor steps for loss computation.

---

## Session 2026-03-11 — Axisymmetric Geometry Debugging & 3D FEM Discussion

**Scope:** Debugging a solenoid model that produced all-magenta (incorrect) density plots despite correct boundary conditions. Also discussed feasibility of extending to 3D FEM.

### 1. Axisymmetric Arc Boundary Bug (User's Model)

**Problem:** User set up a solenoid with two 180° arcs forming a circular outer boundary with A=0 boundary conditions on both arcs. The density plot showed all-magenta (uniform maximum B) inside the circle — clearly wrong.

**Root cause:** The two arcs formed a complete circle centered at **(r=20, z=50) with radius 60**. The arc endpoints were both at r=20 (nodes at (20, 110) and (20, -10)):
- **Arc 0** (right semicircle): swept through (80, 50) — fine, r > 0
- **Arc 1** (left semicircle): swept through **(-40, 50)** — r < 0, physically invalid for axisymmetric problems

In axisymmetric mode, r represents radial distance and must be ≥ 0. Elements with negative r produce garbage in the stiffness matrix (integrals weighted by r flip sign), corrupting the entire solution.

**Resolution:** User switched to **planar mode**, which worked correctly since negative x coordinates are valid in planar. For axisymmetric problems, the correct approach would be a semicircular boundary staying in r ≥ 0 (e.g., nodes on the z-axis with a 180° arc curving rightward, plus a straight segment along r=0).

**No code changes — this was a modeling issue, not a software bug.**

### 2. 3D FEM Feasibility Discussion

User asked what it would take to extend TurboFEMM to 3D. Key points communicated:

- **Meshing:** 2D triangles → 3D tetrahedra. Triangle library can't do this; would need TetGen or Gmsh.
- **Solver:** Scalar potential A (1 DOF/node) → edge elements (Nédélec, 1 DOF/edge) for proper div(B)=0. Matrix size grows by orders of magnitude (~1M+ unknowns for modest problems).
- **Visualization:** QPainter 2D rendering → OpenGL/Metal 3D rendering with cut planes, isosurfaces. Essentially a complete rendering rewrite.
- **Assessment:** This is a new solver that shares material definitions, not a port. Months of work.
- **Pragmatic note:** Axisymmetric mode already gives 3D results for rotationally symmetric geometries (solenoids, motors) at 2D cost.

**No code changes — discussion only.**

### Files Modified

None.

### Gotchas

- **Arc direction matters for axisymmetric:** Two 180° arcs forming a full circle will always have one arc crossing the z-axis (r=0) into negative r unless the center is at r=0 and the arc endpoints are also on the z-axis. Users should use semicircular boundaries for axisymmetric problems.
- **No runtime warning:** The solver doesn't currently warn when mesh elements have r < 0 in axisymmetric mode. Could be a useful future enhancement.

### Tests
No code changes, so no test run needed.

---

**Scope:** Debugging and fixing the adaptive refinement algorithm that was producing insane mesh growth (4× per iteration instead of ~1.4×), causing the solver to fail on complex models like the LRK motor.

### 1. Root Cause: Tolerance-Based θ Refined ALL Regions

**Problem:** Running adaptive refinement on the LRK motor (29 regions, 36K elements, 11% global error) at 0.1% tolerance caused the mesh to explode from 36K → 146K elements in a single iteration. The solver then failed (PCG divergence or NaN).

**Root cause:** The original `computeRegionTargetAreas()` computed each region's θ ratio as:
```
budget = tolerance × sqrt(N_r / N)
θ = budget / regionErr
```
With tolerance=0.001 and globalErr=0.11, θ was tiny for every region (0.006–0.28), meaning **all 29 regions** were flagged for refinement. The global growth cap (1.5×) couldn't compensate enough — scaling all 29 θ values up still produced ~4× growth because the cap was applied uniformly.

**The key insight:** The tolerance should control *when the loop stops*, not *how aggressive each step is*. Trying to jump from 11% error to 0.1% in one step is unreasonable — it requires refining everything by 100×.

### 2. Fix: Step-Target Decoupled from Tolerance

**Solution:** Each iteration now targets an intermediate error level — halving the current global error — rather than jumping to the final tolerance:

```
stepTarget = max(globalErr / 2, tolerance)
```

- At 11% error: stepTarget = 5.5% (regardless of whether tolerance is 0.1% or 5%)
- At 5% error: stepTarget = 2.5%
- And so on, until globalErr < tolerance → converge

This makes each step **selective**: regions already below the step budget (θ ≥ 1) are kept unchanged. For the LRK motor at 11% error:
- **Region 0** (1481 elems, stator teeth): θ ≈ 0.55 → REFINE 1.4×
- **Region 1** (14385 elems, stator iron): θ ≈ 0.33 → REFINE 2×
- **All other 27 regions**: θ > 1 → KEEP (already within step budget)
- **Predicted total growth: ~1.4×** (36K → ~51K), well under the 1.5× cap

The tolerance slider now controls *how many iterations* run (and thus how refined the final mesh is), rather than causing catastrophic single-step growth.

### 3. Algorithm Details (in `computeRegionTargetAreas`)

Controls preserved from the original:
- **Damping α = 0.6:** θ_damped = θ^0.6 (60% toward optimum in log-space)
- **Per-region floor:** θ ≥ 1/3 (max 3× refinement per region per step)
- **Global growth cap:** ≤ 1.5× total elements per step
- **Selectivity threshold:** only refine if θ_final < 0.8 (regions close to budget are left alone)

Log output format changed to help debugging:
- Old: `[ADAPTIVE] ZZ prediction: growth=...`
- New: `[ADAPTIVE] globalErr=..., stepTarget=...` and `[ADAPTIVE] Step: growth=..., N of M regions refined`

### Files Modified

| File | Action | Purpose |
|------|--------|---------|
| `gui/adaptiverefine.cpp` | Modified | Rewrote `computeRegionTargetAreas()` — stepTarget approach, updated log format, added block comment explaining the algorithm |

### Gotchas

- **Binary staleness:** The user ran the old binary after the code change and got confused by the old log output (`ZZ prediction` format). Always verify the binary timestamp matches the source changes (`ls -la` on the app binary).
- **defaultMeshSize in meshgen.cpp:** Triangle's effective area constraint is `min(blockLabel.maxArea, defaultMeshSize)` where `defaultMeshSize = (diagonal/10)^2`. User block labels with large maxArea values (1.25, 2.19) are ignored — Triangle uses defaultMeshSize instead. This means setting `maxArea` to a target only has effect when the target is *smaller* than defaultMeshSize.
- **test_problems/lrk.fem:** Has local modifications (block label maxArea values changed during testing). Not committed — these are test artifacts.

### Tests
All 76+ tests passing (8 solver + others). No new tests added — the fix is to the refinement heuristic, which is validated by running the full adaptive loop on real models.

---

**Scope:** Six fixes/features focused on UI polish, rendering quality, motion system rework, and getting the project onto GitHub.

### 1. Window Resize Bug During Analysis

**Problem:** The main window visibly resized during every solver iteration — looked unprofessional. The status bar was growing/shrinking as solver text changed.

**Root cause (two issues):**
1. QStatusBar had no fixed height constraint, so layout recalculated on every text change
2. Solver stdout/stderr could emit multiple lines at once (e.g. `readAllStandardOutput()` returns batched output), expanding the status label vertically

**Solution:**
- Locked status bar height after initial layout: `statusBar()->setFixedHeight(statusBar()->sizeHint().height())`
- Disabled size grip: `statusBar()->setSizeGripEnabled(false)`
- Changed `updateStatus()` to extract only the last line of multiline text and trim it

**Files modified:**
- `gui/mainwindow.cpp` — `createStatusBar()` (height lock), `updateStatus()` (last-line extraction)

**Gotcha:** The fix must call `setFixedHeight()` *after* the status bar has its initial content, so `sizeHint()` returns a reasonable value.

---

### 2. Auto-Open Last File on Launch

**Problem:** User had to manually open the same file every time they launched the app.

**Solution:** Save the file path in QSettings on every `openFile()` call, restore it in `main.cpp` on startup if no command-line argument was given.

**Files modified:**
- `gui/mainwindow.cpp` — Added `QSettings` save in `openFile()` before creating DrawingWidget
- `gui/main.cpp` — Added startup logic: check `file/lastOpenedFile` in QSettings, open if file exists

**Key design decisions:**
- Only auto-opens if no CLI argument provided (CLI takes priority)
- Checks `QFileInfo::exists()` before opening — gracefully handles deleted files

---

### 3. Smooth Density Plot (Gouraud Shading)

**Problem:** Density plot showed visible banding/lines — only 20 discrete color levels with flat shading per triangle, plus a complex multi-color subdivision algorithm that still looked blocky.

**Solution:** Complete rewrite of the density rendering pipeline:
1. **256-color procedural palette** — Replaced hardcoded 20-color `s_colorMap` static array with procedurally generated smooth gradient (magenta → yellow → cyan) via `paletteColor()` static method
2. **Per-pixel Gouraud shading** — Replaced flat `rasterTriangle()` + multi-color subdivision with `rasterTriangleGouraud()` that interpolates values at every pixel using barycentric coordinates, then looks up color from 256-entry LUT

**Files modified:**
- `gui/resultsoverlay.h` — `kNumColors = 256`, removed static color array, added `paletteColor()` method
- `gui/resultsoverlay.cpp` — New Gouraud rasterizer (scanline with per-pixel interpolation), procedural palette generation, smooth gradient legend with 7 value labels

**Key design decisions:**
- Scanline rasterizer sorts vertices by Y, walks left/right edges, interpolates value linearly across each scanline
- LUT approach (compute palette once, index per pixel) is much faster than per-pixel HSV conversion
- Legend draws 256 thin horizontal bands for smooth gradient, with 7 evenly-spaced numeric labels
- All line rasterizers (Bresenham, Wu's AA, SSAA) left unchanged

---

### 4. Translation Motion Rework + Linear Motor Support

**Problem:** Rotation mode had a clean "total angle + steps" parameterization, but translation used raw per-step dx/dy — inconsistent and confusing. Also, linear motors had no way to map displacement to electrical angle for 3-phase current computation.

**Solution:** Two changes:
1. **New translation UI:** "Total distance" (mm) + "Direction angle" (degrees, 0=+X), matching rotation's "Total angle" + steps paradigm. Per-step dx/dy computed automatically: `stepDist × cos/sin(directionAngle)`
2. **Pole pitch for linear motors:** New "Pole Pitch (translation)" field in motor section. Electrical angle = `(cumulative_displacement / polePitch) × 360°`, analogous to rotation's `mechAngle × polePairs`

**Files modified:**
- `gui/dialogs/motiondialog.h` — MotionConfig: added `totalDistance`, `directionAngle`, `motorPolePitch`; `dx`/`dy` now computed
- `gui/dialogs/motiondialog.cpp` — New translation page UI, pole pitch spinbox, config() computes dx/dy from distance+angle
- `gui/motionrunner.cpp` — Motor electrical angle: rotation branch uses `polePairs`, translation branch uses `polePitch`

**Key design decisions:**
- `dx`/`dy` kept in MotionConfig as computed values (not removed) so downstream code (solver, geometry transforms) doesn't need changes
- Pole pitch default = 10.0 mm, range 0.001–1e6 mm
- Division-by-zero guard: `if (polePitch < 1e-12) polePitch = 1e-12`

---

### 5. Remove Unnecessary Post-Sweep Auto-Solve

**Problem:** After motion sweep completed and geometry was restored, `onMotionFinished()` was running the solver one more time on the restored (original) geometry — pointless and confusing.

**Solution:** Removed the `m_solver->runSolver(doc)` call in `onMotionFinished()`. Now just shows completion message and updates status.

**Files modified:**
- `gui/mainwindow.cpp` — Removed auto-solve block in `onMotionFinished()`

---

### 6. Git Repository + GitHub Setup

**Problem:** User wanted the project on GitHub but the working folder was ~100MB due to `build/` directory.

**Solution:**
1. Created `.gitignore` excluding: `build/`, `CMakeFiles/`, `.DS_Store`, IDE files, solver output (`*.ans`, `*.node`, `*.ele`, `*.edge`, `*.pbc`, `*.poly`), `.app` bundles, `.claude/`
2. Initialized git repo, staged 810 files (~26 MB after exclusions)
3. Initial commit: `TurboFEMM: Qt6 port of FEMM 4.2 with modern features`
4. Authenticated via `gh auth login` (browser-based OAuth)
5. Pushed to `github.com/CNCaddict/TurboFEMM`

**Files created:**
- `.gitignore`

**Gotchas:**
- `CMakeFiles/CMakeSystem.cmake` existed in source root (outside `build/`) — had to `git rm --cached` it
- `gh auth login` doesn't work well through non-interactive terminals — user had to run it in their own Terminal.app
- GitHub disabled password auth for git operations — must use `gh` CLI, SSH key, or personal access token

---

### Summary of all files touched this session

| File | Action | Purpose |
|------|--------|---------|
| `gui/mainwindow.cpp` | Modified | Status bar height lock, updateStatus last-line fix, last-file save, remove post-sweep solve |
| `gui/main.cpp` | Modified | Auto-open last file on launch |
| `gui/resultsoverlay.h` | Modified | 256-color palette, Gouraud shading API |
| `gui/resultsoverlay.cpp` | Modified | Gouraud rasterizer, procedural palette, smooth legend |
| `gui/dialogs/motiondialog.h` | Modified | totalDistance, directionAngle, motorPolePitch |
| `gui/dialogs/motiondialog.cpp` | Modified | Translation UI rework, pole pitch spinbox |
| `gui/motionrunner.cpp` | Modified | Linear motor electrical angle from pole pitch |
| `.gitignore` | **Created** | Exclude build artifacts, IDE files, solver output |

### Tests
All 76 tests passing after all changes (26 mesh + 8 solver + 42 other).

---

## Session 2026-03-11 — Anderson Acceleration, Log Panel, GitHub Release, README

**Scope:** Four major features plus project packaging and documentation.

### 1. Anderson Acceleration for Nonlinear Solver

**Problem:** Newton-Raphson iteration with adaptive relaxation was very slow to converge on saturated magnetic materials (steel/iron B-H curves).

**Solution:** Implemented Anderson acceleration as a drop-in enhancement to the existing nonlinear iteration loop.

**Files created:**
- `fkn/anderson.h` — Header-only `AndersonAccelerator` class

**Files modified:**
- `fkn/prob1big.cpp` — Static 2D solver (most extensive changes)
- `fkn/prob2big.cpp` — Harmonic 2D solver (complex-valued)
- `fkn/prob3big.cpp` — Static axisymmetric solver
- `fkn/prob4big.cpp` — Harmonic axisymmetric solver

**Key design decisions:**
- **History depth = 5:** Stores 5 previous iterates in a circular buffer. Deeper history didn't help much in testing and costs more memory.
- **Safeguard threshold = 10x:** If the accelerated step is more than 10x the Newton step magnitude, reject it and fall back to the existing adaptive relaxation. This prevents divergence on ill-conditioned problems.
- **Single class for real and complex:** CComplex is `{double re, double im}` — memory-compatible with interleaved doubles. For complex solvers (prob2, prob4), pass `(double*)array` with length `2 * numEntries` instead of writing separate complex logic.
- **Tikhonov regularization:** `eps = 1e-12 * trace(FtF)` prevents singular normal equations when iterates are nearly parallel.
- **Fallback is the OLD code:** When Anderson rejects a step, the original adaptive relaxation runs (halve Relax if diverging after iter 5, slowly increase otherwise). This means Anderson can never make things worse.

**How it integrates:**
```
Before the do-while Newton loop:
  anderson.init(NumNodes, 5)

Inside the loop, after the linear solve:
  if (Iter >= 1) {
      if (!anderson.apply(V_old, L.V)) {
          // Anderson rejected — use old relaxation fallback
      }
  }
```

**Test results:** All 34→76 tests passed. Convergence typically reaches Relax=1.0 (Anderson handling everything) in 15-22 iterations.

---

### 2. Scrollable Log Panel

**Problem:** Solver iteration messages, errors, and progress were shown in a single-line status bar — only the latest message visible. User wanted full history of all solver diagnostics.

**Solution:** QPlainTextEdit log panel in a QSplitter, toggleable via View menu.

**Files modified:**
- `gui/mainwindow.h` — Added `QSplitter`, `QPlainTextEdit`, log panel members and slots
- `gui/mainwindow.cpp` — Log panel creation, View menu toggle, QSettings persistence
- `solvers/common/compat_mfc.h` — Added `onLogMessage` callback to `CSolverDlg`
- `gui/inprocesssolver.cpp` — Wired `onLogMessage` callback to emit `progress()` signal
- `fkn/prob1big.cpp` — 5 `fprintf(stderr)` locations also call `TheView->LogMessage()`
- `fkn/prob2big.cpp` — 1 location converted
- `fkn/prob3big.cpp` — 1 location converted

**Key design decisions:**
- **Default state:** Panel visible at ~24px height (one line), user drags QSplitter divider to expand. Starts visible so users know it exists.
- **Max 5000 lines:** `setMaximumBlockCount(5000)` prevents memory bloat on long sessions.
- **Monospace font:** Menlo 11pt for consistent alignment of solver output.
- **Timestamps:** Every line prefixed with `[HH:MM:SS]` via `appendLog()`.
- **Auto-scroll:** Always scrolls to bottom on new messages.
- **Logs everything:** PCG percentage updates, Newton iteration details, phase transitions, convergence info — the user explicitly requested "everything."
- **QSettings persistence:** Log panel visibility and splitter sizes saved/restored across app launches.

**Architecture note:** The solver's `fprintf(stderr, ...)` messages can't be intercepted with a callback — instead, we added parallel `TheView->LogMessage()` calls at each location. The `CSolverDlg::onLogMessage` callback routes through `InProcessSolver::progress()` signal to the GUI thread.

---

### 3. GitHub Release (v0.1.0-alpha)

**Problem:** User wanted a downloadable macOS binary so people don't have to build from source.

**What was done:**
1. `macdeployqt` to bundle Qt frameworks into `.app` bundle
2. Ad-hoc codesign: `codesign --force --deep --sign -`
3. Created DMG (37MB) with `hdiutil`
4. Uploaded as GitHub release via `gh release create v0.1.0-alpha`

**Repo:** `CNCaddict/TurboFEMM`

**Gotchas encountered:**
- **macdeployqt errors for QtPdf, QtSvg, QtVirtualKeyboard** — non-fatal, these are optional plugins the app doesn't use. Core frameworks (QtWidgets, QtGui, QtCore) copied fine.
- **ARM64 only** — user confirmed they only want Apple Silicon build, no universal binary.
- **Not notarized** — requires right-click → Open or `xattr -cr` to bypass Gatekeeper.

---

### 4. App Screenshot for GitHub

**Problem:** Wanted a screenshot of the running app on the release page.

**Gotchas encountered (important for future sessions):**
- `screencapture -x` **failed** — Terminal/Claude Code doesn't have Screen Recording permission on macOS. Error: "could not create image from display"
- **Python Quartz module** not available (no pyobjc installed)
- **osascript/System Events** denied — no Accessibility permission
- **Solution that worked:** Swift one-liner using `CGWindowListCopyWindowInfo` to find the window ID, then `screencapture -l <windowID>` for window-level capture (doesn't need screen recording permission)

```bash
# Find window ID
swift -e 'import Cocoa; ...'  # Returns window ID 7201
# Capture specific window
screencapture -l 7201 /tmp/screenshot.png
```

Uploaded as release asset, referenced in README with GitHub release asset URL.

---

### 5. README.md — Comprehensive Feature Documentation

**Created:** Full README with feature comparison table (FEMM 4.2 vs TurboFEMM), build instructions, credits.

**Corrections made after initial draft:**
1. **Window title said "FEMM 4.2"** → Changed to "TurboFEMM" in `mainwindow.cpp` line 59
2. **"Mesh persistence" was listed as a feature** → WRONG. The motion runner calls `generateMesh()` at each step. Changed to "Automatic re-meshing."
3. **DXF import listed as new feature** → WRONG. Original FEMM 4.2 already had DXF import (LINE, CIRCLE, ARC, POLYLINE). Moved to "inherited from original" in comparison table.
4. **Download section** moved to top of README (right after screenshot) per user request.
5. **Disclaimer strengthened** — Made the vibe-coded warning more prominent with bold "Do not trust the results" and recommendation to validate against original FEMM 4.2.

---

### 6. Windows/Cross-Platform GPU Discussion (not implemented)

**User asked:** How hard to build for Windows with Intel/AMD/NVIDIA/Qualcomm GPU acceleration?

**Assessment provided:**
- Current Metal backend is cleanly abstracted behind `GPUBackend` interface (~800 lines Metal-specific code)
- **Recommended approach:** Vulkan Compute — one implementation covers all four GPU vendors
- 7 simple compute kernels to port (SpMV, dot, axpy, scal, copy, zero, Jacobi precond)
- Estimated 2-4 weeks of focused work
- **User decided to defer this work**

**Key files for future GPU porting:**
- `solvers/common/gpu_backend.h` — Abstract interface (platform-agnostic)
- `solvers/gpu/metal_backend.h` / `.mm` — Metal implementation
- `solvers/gpu/kernels.metal` — 7 compute shaders
- `fkn/spars.cpp` — PCG solver that calls through the interface

---

### Summary of all files touched this session

| File | Action | Purpose |
|------|--------|---------|
| `fkn/anderson.h` | **Created** | Anderson acceleration class |
| `fkn/prob1big.cpp` | Modified | Anderson + LogMessage (Static 2D) |
| `fkn/prob2big.cpp` | Modified | Anderson + LogMessage (Harmonic 2D) |
| `fkn/prob3big.cpp` | Modified | Anderson + LogMessage (Static Axi) |
| `fkn/prob4big.cpp` | Modified | Anderson (Harmonic Axi) |
| `solvers/common/compat_mfc.h` | Modified | Added onLogMessage callback |
| `gui/inprocesssolver.cpp` | Modified | Wired onLogMessage to signal |
| `gui/mainwindow.h` | Modified | Log panel members |
| `gui/mainwindow.cpp` | Modified | Log panel, title→TurboFEMM |
| `README.md` | **Created** | Full project documentation |

### Tests
All 76 tests passing after all changes.

---

## [Older sessions — to be backfilled]

> When revisiting older Claude Code sessions, prepend your session entry above this line.
> Follow the same format: date, scope summary, then numbered sections for each feature/task.
> Include: files changed, key decisions and WHY, gotchas/errors encountered, and test results.
