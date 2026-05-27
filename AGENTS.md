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
  `DEPS=.deps`, `SD_MODE=wide20`, `SD_READ_MODE=boot`,
  `HCRTOS_MEDIA=native`
- SD diagnostics: the default `SD_MODE=wide20` build runs storage tests on the
  boot profile. Build with `SD_MODE=safe` for runtime profile sweeps. Storage
  full test uses `/ROMS/probes/test*.md`, restores the boot mode after each
  experimental read, and checkpoints its report. Storage mode test selects one
  profile, switches once, reads all probes, then restores the boot mode. Use
  the on-screen stage as the primary freeze clue; power cycles can overwrite
  warm reboot diagnostics.
  Frontend startup, ROM loading, and native module loading stay on the boot
  profile by default. Use non-`boot` `SD_READ_MODE` values only for runtime
  storage diagnostics.
  `SD_MODE=hs1|wide50|wide|uhs12|uhs25|uhs` and other non-default profiles are
  fixed-profile experimental boot builds.
- Local overrides belong in untracked `config.mk`.

## Scope

- Build the native SF2000 `bisrv.asd` and SD-card package.
- Keep the boot frontend in `src/native_frontend.c` and
  `src/frontend_lvgl.c`. The in-game libretro quick menu lives in
  `src/unifrog_libretro_host.c`. Native JS2300 bridge code in
  `src/frontend/js2300_frontend_*.c`
  exposes only standalone script bindings and diagnostics.
- Keep the native FFmpeg/HCRTOS media path as the default. The SD-loaded
  `HCRTOS_MEDIA=module` mode is for loader diagnostics.
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
