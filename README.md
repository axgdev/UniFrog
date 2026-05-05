# UniFrog

UniFrog builds a native SF2000 firmware payload, JavaScript frontend package,
native modules, and libretro core modules for an SD-card based setup.

The root Makefile is the authoritative entry point. It builds `bisrv.asd`,
`output/unifrog.bin`, and the complete `output/sdcard` layout.

## Requirements

- Alpine or another Unix-like host with `make`, a C compiler, Git, and `dtc`
- MIPS toolchain at `/opt/mipsel-mti-elf` by default
- HCRTOS SDK submodule at `unifrog-hcrtos-sdk`
- External source checkouts fetched under `.deps`

Install Alpine host packages with:

```sh
make deps-alpine
```

Fetch the SDK submodule, MQuickJS, libretro cores, and support libraries:

```sh
make deps
```

Local paths can be overridden in untracked `config.mk`:

```make
TOOLCHAIN := /opt/mipsel-mti-elf
SDK := unifrog-hcrtos-sdk
DEPS := .deps
```

## Build

```sh
make setup
make doctor
make quick-check
make
make verify
```

Useful targets:

```text
make help             Show the common workflow and focused entry points
make print-config     Show current paths, tools, and optimization settings
make setup            Fetch required external source inputs
make deps-status      Show pins vs each repository policy
make upgrade-deps     Bump pins by policy and fetch them
make doctor           Check tools, SDK, and fetched inputs
make quick-check      Run fast repository, core smoke, JS2300, and frontend checks
make                  Build firmware, modules, and core package
make verify           Build and verify firmware, fastboot, JS, and layout
make fastboot-only-check
                      Build and verify only the fastboot ASD
make boot-logo-check  Generate the stamped full-screen boot logo assets
make sd-zip           Build output/UniFrog-sdcard.zip
make clean            Remove generated build/output files
make distclean        Also clean sub-build outputs
```

`make deps` remains a supported alias for `make setup`; `make check` is the
firmware/package/layout build check used inside the fuller `make verify` gate.
Dependency status and upgrades follow the policy declared beside each pin; use
`MODE=head` or `MODE=tag` only for an explicit one-off override. Use
`make quick-check` while iterating. `make verify` is the normal
verification before handing off changes. If linker scripts or link libraries
change, run `make clean && make verify`.

If `ccache` is installed, the build uses it automatically. Set `CCACHE=` in
`config.mk` or on the command line to disable it.

The boot logo source lives at `assets/boot/unifrog-logo.png` and is converted
to a full-screen 320x240 RGB565 RLE include during the build. The version text
at the bottom is stamped from an exact git tag when available, otherwise from
the current commit hash with a `revision` label.

The default `HCRTOS_MEDIA=module` keeps the SDK FFmpeg/HCRTOS media player out
of the boot image and packages it as `/unifrog/modules/hcrtos-media.bin`; video
actions load that native module from SD. Use `HCRTOS_MEDIA=firmware` only when
you need the old fully linked boot image for diagnostics.

The SD-card package also installs `LICENSE.txt` and `THIRD_PARTY.md` under
`/unifrog`. Keep `THIRD_PARTY.md` current whenever adding, removing, or
replacing fetched or vendored third-party code.

Game discovery is directory based. The default frontend option
`rom_roots=/ROMS|/` scans system folders directly under `/ROMS` and directly
under the SD root, so both `/ROMS/gba` and `/gba` are valid. Files inside a
recognized system folder are treated as games without guessing from their file
extensions. Common retro-handheld folder aliases such as `gba`, `gb`, `gbc`,
`nes`, `snes`, `megadrive`, `pcengine`, and `psx` are matched
case-insensitively. Edit `/unifrog/user/frontend.opt` on the SD card to change
ROM roots without modifying packaged defaults.

SD builds boot with the reliable 1-bit profile. Frontend startup stays on that
boot profile by default. Fast SD read windows are reserved for ROM and native
module loading. The frontend `fast_sd` option intentionally exposes only
`boot` and `hs1`; wider and UHS modes can lose the filesystem on some cards
before the launcher can recover. In the safe build, Developer -> Storage test
runs a quick guarded runtime sweep: it buffers logs, shows progress on screen,
prefers `/ROMS/test.md` when present, verifies a safe remount first, switches
profiles through the SD bus suspend/resume hooks, tries `hs1`, `wide50`,
`uhs12`, `uhs25`, `wide`, and `uhs`, then restores the safe boot profile before
writing `/unifrog/storage-test-result.txt` and the frontend report. Developer
-> Storage full test uses `/ROMS/probes/test*.md`, restores the safe profile
after each experimental read, and checkpoints
`/unifrog/storage-full-test-result.txt` between modes. Developer -> Storage
mode test selects one profile, switches once, runs all probes, then restores
safe mode and writes `/unifrog/storage-mode-test-result.txt`. The on-screen
stage is the best freeze clue; warm reboot diagnostics may also keep it, but a
full power cycle can overwrite that memory. Fixed-profile diagnostic boot
builds are still available with
`SD_MODE=hs1`, `wide50`, `wide`, `uhs12`, `uhs25`, or `uhs`; these modes are
experimental. Device logs rotate to `log-prev.txt` when `log.txt` grows past
1 MiB. The board DTS in
`board/hc15xx/common/dts/sf2000_min.dts` is the single source used by both the
firmware and SDK kernel rebuild.

## Layout

```text
board/                 Local SF2000 device tree input
cores/                 Libretro source manifest, patches, and core build
docs/                  Hardware, ABI, loader, and diagnostics notes
frontend/              Modular JavaScript frontend and themes
include/unifrog/       Public UniFrog C interfaces
js2300/                MQuickJS embedding layer
linker/                HCRTOS/SF2000 linker scripts
output/sdcard/unifrog/modules/
                       Runtime-loaded native modules
src/                   Native runtime implementation
src/third_party/       Small vendored source libraries with local notices
THIRD_PARTY.md         Third-party source, binary, and attribution inventory
tools/asdpack.c        Host tool used to pack and verify ASD images
unifrog-hcrtos-sdk/    SDK submodule
```

Fetched third-party source checkouts live in untracked `.deps/` after
`make deps`. Generated files live in `build/`, `output/`, `cores/output/`,
`frontend/output/`, `js2300/output/`, and packaging directories. They are not
source.

## Component Docs

- `docs/README.md` maps the retained hardware and architecture notes.
- `cores/README.md` explains core source fetching, patching, and ABI rules.
- `frontend/README.md` covers SD-card JavaScript and theme packaging.
- `js2300/README.md` covers the embedded JavaScript runtime layer.

Each component Makefile has its own quick reference:

```sh
make -C cores help
make -C frontend help
make -C js2300 help
```
