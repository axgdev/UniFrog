# Agent Notes

Keep this repository small and direct.

## Build Contract

- Primary command: `make`
- Health check: `make doctor`
- Fast local gate: `make quick-check` (includes core smoke)
- Handoff verification: `make verify`
- Discovery: `make help`, `make print-config`, and component-local
  `make -C <component> help`
- Dependency pins: `make deps-status` and `make upgrade-deps`; use
  `MODE=head|tag` only to override repository policy.
- Defaults: `TOOLCHAIN=/opt/mipsel-mti-elf`, `SDK=unifrog-hcrtos-sdk`,
  `DEPS=.deps`, `SD_MODE=safe`
- SD diagnostics: the default `SD_MODE=safe` build can run Developer ->
  Storage test as a quick guarded runtime sweep. It buffers logs, verifies a
  safe restart first, requires clean unmounts between profiles, restores the
  safe boot profile, then writes the report.
  `SD_MODE=hs1|wide50|wide|uhs12|uhs25|uhs` are fixed-profile experimental
  boot builds.
- Local overrides belong in untracked `config.mk`.

## Scope

- Build the native SF2000 `bisrv.asd` and SD-card package.
- Keep UI screens in `frontend/quick-menu.js` and `frontend/app/*.js`;
  `frontend/main.js` is only the loader. Native JS2300 bridge code in
  `src/frontend/js2300_frontend_*.c` exposes fast bindings, launch actions,
  diagnostics, and crash handling.
- Package JavaScript source with `.js.mqbc` bytecode and
  `bytecode-manifest.txt`; do not rely on file timestamps for freshness.
- Keep the SF2000 board DTS in `board/hc15xx/common/dts/sf2000_min.dts`; the
  SDK rebuild consumes that file instead of carrying a second copy.
- Keep the mixed-license HCRTOS SDK in the `unifrog-hcrtos-sdk` submodule.
- Fetch external source dependencies with `make deps`; do not commit `.deps`,
  generated outputs, downloaded toolchains, or upstream core/MQuickJS checkouts.
- Preserve `tools/asdpack.c` as a host-built tool.

## Editing Rules

- Prefer Makefile changes over shell scripts.
- Avoid Python, CMake, Autotools, and generated configure layers.
- Keep dependencies to a host C compiler, the MIPS cross toolchain, `make`,
  `dtc`, the SDK, fetched dependencies, and normal Unix utilities.
- Run `make quick-check` after focused edits. Run `make verify` before handoff.
- If changing link libraries or linker scripts, run `make clean && make verify`.
