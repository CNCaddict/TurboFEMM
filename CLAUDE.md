# TurboFEMM — Claude Code Instructions

## Project History
**Always read `DEVLOG.md` first** at the start of every session. It contains the full history of all AI-assisted development — decisions made, files changed, gotchas discovered, and reasoning behind design choices. Before making changes, check the devlog to avoid repeating mistakes or undoing intentional decisions.

When your session ends, **append your work to DEVLOG.md** following the existing format (newest entry first).

## Release & Changelog Requirements
Every time code is pushed to GitHub:
1. **Update `DEVLOG.md`** with all changes from the session (newest entry first)
2. **Update `CHANGELOG.md`** (if it exists) with user-facing changes — bug fixes, new features, UI improvements
3. **Post the changelog to GitHub** as a release or update the existing release notes
4. Never push without updating both the devlog and changelog first

## Testing Requirements
- **Run unit tests after every code change:** `cd build && make -j8 femm-tests && ./tests/femm-tests`
- **When changing any GUI/runtime code, also rebuild the actual app bundle:** `cd build && make -j8 femm-gui`
- **Do not assume `femm-tests` rebuilds the app the user is launching.** If the user is testing in the GUI, verify `/build/gui/femm-gui.app` was rebuilt.
- **All 89+ tests must pass** before considering any change complete (1 known edge-case failure in `adaptiveSolveFailureRecovery` is acceptable)
- **Add new tests** when implementing new features or fixing bugs — cover the new behavior
- **Update existing tests** if behavior intentionally changes — don't just delete failing tests
- Test files are in `tests/` using the Qt Test framework

## Benchmarking
- **Run benchmarks when performance-related changes are made:** `cd build && make -j8 femm-bench && ./tests/femm-bench`
- This includes changes to: solver code, mesh generation, GPU backend, matrix operations, or anything in the hot path
- Note benchmark results in DEVLOG.md when relevant (before/after comparisons)

## Build Commands
```bash
cd build
make -j8              # Build GUI app
make -j8 femm-gui     # Rebuild the actual app bundle the user launches
make -j8 femm-tests   # Build tests
./tests/femm-tests    # Run tests
make -j8 femm-bench   # Build benchmarks
./tests/femm-bench    # Run benchmarks
```

## Key Architecture Notes
- Metal GPU backend abstracted behind `GPUBackend` interface (`solvers/common/gpu_backend.h`)
- GPU PCG uses float32 for speed; auto-falls back to CPU double-precision if GPU diverges (fine meshes)
- Solver code uses MFC compat shims on non-Windows (`solvers/common/compat_mfc.h`)
- "Analyze" button uses in-process solver (same as motion sweep) — no external fkn process
- In-process solver runs mesh + FEM solve in GUI process (no disk I/O)
- Anderson acceleration for nonlinear materials (`fkn/anderson.h`) with relaxation fallback
- Triangle library for mesh generation (PSLG mode)
- .fem file format fully compatible with original FEMM 4.2

## Known Issues
- `adaptiveSolveFailureRecovery` test can crash when adaptive refinement pushes mesh to extreme density (~165k+ elements) and the CPU PCG times out or runs out of memory
- Metal GPU PCG (float32) can diverge on very fine meshes (>80k nodes) — the automatic CPU fallback handles this transparently
