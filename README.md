# TurboFEMM

A modernized, GPU-accelerated port of [FEMM 4.2](http://www.femm.info/) (Finite Element Method Magnetics) by David Meeker. Rebuilt with Qt6 for macOS, with major new features including parametric motion analysis, adaptive mesh refinement, and Metal GPU-accelerated solving.

> **⚠️ Warning:** This is an experimental, "vibe-coded" project — built rapidly with AI assistance and only lightly tested. **Do not trust the results** for anything critical. There are almost certainly bugs, numerical inaccuracies, and edge cases that have not been caught. Always validate against known analytical solutions or the original FEMM 4.2 before relying on any output.

![TurboFEMM Screenshot](https://github.com/CNCaddict/TurboFEMM/releases/download/v0.1.0-alpha/turbofemm-screenshot.png)

## Download

Pre-built macOS binary (Apple Silicon):
**[Download TurboFEMM-macOS-arm64.dmg](https://github.com/CNCaddict/TurboFEMM/releases/latest)**

### Installation
1. Open the DMG and drag **TurboFEMM.app** to Applications
2. First launch: right-click the app and select Open (Gatekeeper warning — not notarized)
3. Or run: `xattr -cr /Applications/TurboFEMM.app`

**Requirements:** macOS 13+ (Ventura or later), Apple Silicon (M1/M2/M3/M4)

---

## What's New vs. Original FEMM 4.2

The original FEMM 4.2 is a Windows-only, single-threaded, CPU-based magnetostatics tool with no motion analysis and no adaptive meshing. TurboFEMM adds:

### Parametric Motion & Rotation
The original FEMM can only analyze a single static geometry. TurboFEMM adds a full motion sweep engine:
- **Translation sweeps** — move geometry along any direction, solve at each step
- **Rotation sweeps** — rotate around any center point with configurable angle steps
- **Automatic re-meshing** — generates a fresh mesh at each position after geometry transforms
- **Automatic animation** — exports GIF animations and individual PNG frames
- **CSV data export** — flux density, energy, force, and torque at every step

### 3-Phase Motor Analysis
- Computes instantaneous Phase A/B/C currents from electrical angle and RMS current
- Sweeps electrical angle to find peak torque operating point
- Supports configurable pole pairs and phase reversal
- Rotor stays fixed while stator field rotates (efficient for torque curves)

### Metal GPU-Accelerated Solver (macOS)
- Sparse matrix-vector products, AXPY, DOT, and Jacobi preconditioning on Apple GPU
- Persistent GPU backend reused across consecutive solves
- Custom Metal compute shaders compiled at build time
- Falls back to CPU automatically when Metal is unavailable

### Anderson Acceleration for Nonlinear Materials
- Stores recent iterate history and finds optimal linear combination via least-squares
- Typically 2-3x fewer Newton iterations on saturated steel/iron B-H curves
- Automatic safeguard rejects bad steps and falls back to relaxation
- Works for both real (magnetostatic) and complex (AC harmonic) problems

### H-Adaptive Mesh Refinement
- **Phase 1 (Refine):** Zienkiewicz-Zhu error estimator identifies high-error regions, halves global error each step
- **Phase 2 (Coarsen):** Geometric bisection finds the minimum mesh density that meets tolerance
- Per-region area targeting with configurable tolerance, marking fraction, and iteration limits
- Runs on background thread with live mesh visualization updates

### In-Process Solver
- Mesh generation and FEM solve run entirely in-memory within the GUI process
- Eliminates disk I/O overhead (critical for 10-20 step motion sweeps)
- Persists GPU backend and Lua state across solves (~20ms savings per step)

### Cross-Platform Qt6 GUI
- Native macOS application (replaces Windows-only MFC)
- Tabbed MDI interface for multiple open models
- Scrollable solver log panel showing iteration details, residuals, and diagnostics
- Persistent UI settings (window layout, rendering quality, panel visibility)

### Advanced Results Visualization
- Smoothed B-field recovery (ZZ nodal averaging vs. element-constant)
- Density plots: |B|, Re(B), Im(B), |H|, |J| with 256-color smooth palette
- Configurable contour lines and mesh overlay
- Multi-level anti-aliasing (Off / Low / High / Ultra / Extreme)
- Point queries for field values at arbitrary locations
- Torque computation via Maxwell stress tensor

### Automated Test Suite
- Qt Test framework with 76 tests covering document I/O, geometry, mesh generation, and solver integration
- Benchmark suite for performance tracking
- No automated tests existed in the original FEMM 4.2

---

## Feature Comparison

| Feature | FEMM 4.2 | TurboFEMM |
|---------|-----------|-----------|
| Platform | Windows only (MFC) | macOS (Qt6), Linux planned |
| Motion/rotation analysis | No | Yes (translate + rotate sweeps) |
| Motor optimization | No | Yes (3-phase torque search) |
| Animation export | No | Yes (GIF + PNG frames + CSV) |
| Adaptive mesh refinement | No | Yes (2-phase ZZ + coarsening) |
| GPU acceleration | No | Yes (Apple Metal) |
| Nonlinear acceleration | Basic relaxation | Anderson acceleration (2-3x faster) |
| DXF import | Yes | Yes (inherited from original) |
| In-process solver | No (disk-based IPC) | Yes (in-memory) |
| Anti-aliased rendering | No | Yes (multi-level SSAA) |
| Solver log panel | No | Yes (scrollable, timestamped) |
| Automated tests | No | Yes (76 tests + benchmarks) |
| Multi-document interface | Single document | Tabbed MDI |
| Build system | Visual Studio .sln | CMake (cross-platform) |
| .fem file compatibility | Native | Fully compatible (reads/writes same format) |

---

## Building from Source

### Prerequisites
- macOS 13+ with Xcode Command Line Tools
- Qt 6.x (`brew install qt`)
- CMake 3.20+ (`brew install cmake`)

### Build
```bash
mkdir build && cd build
cmake ..
make -j8
```

### Run Tests
```bash
make -j8 femm-tests
./tests/femm-tests
```

### Run Benchmarks
```bash
make -j8 femm-bench
./tests/femm-bench
```

The built application is at `build/gui/femm-gui.app`.

---

## Credits

- **Original FEMM 4.2** by [David Meeker](http://www.femm.info/) — the core finite element solver, Triangle mesh library, and .fem file format
- **TurboFEMM** modernization, new features, and Qt6 port by CNCaddict
- Built with [Qt 6](https://www.qt.io/), [Triangle](https://www.cs.cmu.edu/~quake/triangle.html), [Lua 5.1](https://www.lua.org/), and Apple [Metal](https://developer.apple.com/metal/)

---

## License

This project is based on FEMM 4.2 which was released under the [Aladdin Free Public License](http://www.femm.info/wiki/License). See the original license terms for details.
