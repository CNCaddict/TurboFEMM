# TurboFEMM Development Log

> This file is the authoritative record of all AI-assisted development sessions on TurboFEMM.
> Each session is documented in reverse chronological order (newest first).
> Future AI sessions: **read this file first** to understand project history, decisions, and gotchas.

---

## Session 2026-03-11 — Adaptive Refinement: Fix Runaway Mesh Growth

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
