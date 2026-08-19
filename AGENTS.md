# Agent Notes

## 1. Environment & Target Architecture
- **Environment:** Alpine Linux workspace.
- **Target:** MIPS32r1 (Little-Endian / `mipsel` cross-compiled / No floating-point support (fpu) / No MMU).

## 2. Build & Verification Contract
The Makefile is the source of truth. Do not bypass it.
- **Build:** `make`
- **Sanity Check:** `make quick-check` (run after focused edits).
- **Full Verification:** `make verify` (run before any task handoff).
- **Rebuild Rules:** Always run `make clean && make verify` if altering Makefiles, linker scripts, or dependencies.
- **Discovery:** If lost or looking for specific module commands, run `make help` or `make print-config`.

## 3. Workflow & Git Standards
- **Scope:** Keep changes minimal and direct. Focus only on files relevant to the active instruction.
- **Dependency Cleanliness:** Never commit third-party dependencies, `.deps`, generated binaries, or downloaded toolchains. Run `make deps` to restore.
- **Handoff Commits:** Commit incrementally. Use multiple `-m` flags to document the technical handoff to the next agent:
  ```bash
  git commit -m "Short summary" -m "Detailed technical justification and impact and handoff information"
  ```

## 4. Editing Rules & Tooling Limits
- **Keep it Simple:** Prefer plain C, assembly, GNU `make`, POSIX shell adjustments.
- **No Build Bloat:** Do not introduce CMake, Autotools, or extra build tool layers. Rely strictly on existing host tools and the MIPS cross-toolchain.
