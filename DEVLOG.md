# TurboFEMM Development Log

> This file is the authoritative record of all AI-assisted development sessions on TurboFEMM.
> Each session is documented in reverse chronological order (newest first).
> Future AI sessions: **read this file first** to understand project history, decisions, and gotchas.

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
