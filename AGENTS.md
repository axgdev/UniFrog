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
  `DEPS=.deps`, `SD_MODE=safe`, `SD_READ_MODE=uhs25`,
  `HCRTOS_MEDIA=firmware`
- SD diagnostics: the default `SD_MODE=safe` build can run Developer ->
  Storage test for a quick guarded sweep, or Storage full test for
  `/ROMS/probes/test*.md`. Full test restores safe mode and checkpoints its
  report after each experimental read. Storage mode test selects one profile,
  switches once, reads all probes, then restores safe mode. Use the on-screen
  stage as the primary freeze clue; power cycles can overwrite warm reboot
  diagnostics.
  Frontend startup stays on the safe boot profile. Runtime ROM and native
  module reads use `SD_READ_MODE=uhs25` by default, then restore the boot
  profile before normal UI writes or core init. Use `SD_READ_MODE=boot` to
  disable those runtime windows.
  `SD_MODE=hs1|wide50|wide|uhs12|uhs25|uhs` are fixed-profile experimental
  boot builds.
- Local overrides belong in untracked `config.mk`.

## Scope

- Build the native SF2000 `bisrv.asd` and SD-card package.
- Keep the boot frontend in `src/native_frontend.c` and
  `src/frontend_lvgl.c`. `frontend/quick-menu.js` is the in-game libretro
  quick menu. Native JS2300 bridge code in `src/frontend/js2300_frontend_*.c`
  exposes only standalone script bindings and diagnostics.
- Keep SDK FFmpeg/HCRTOS media firmware-linked by default; the SD-loaded
  `HCRTOS_MEDIA=module` mode is currently for loader diagnostics because
  `hcplayer_create()` can stall when the media stack runs from that module.
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
