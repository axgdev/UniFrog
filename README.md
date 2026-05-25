# UniFrog

UniFrog builds a native SF2000 firmware payload, MuOS-style frontend package,
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
make quick-check      Run fast repository, core smoke, frontend, and JS2300 checks
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

The default `HCRTOS_MEDIA=firmware` links the SDK FFmpeg libraries and HCRTOS
codec plugins into the boot image. `HCRTOS_MEDIA=module` still packages
`/unifrog/modules/hcrtos-media.bin` for loader diagnostics of the native media
stack; frontend video playback fails safely in unsupported module builds and
returns to the menu instead of entering known black-screen paths.

The SD-card package also installs `LICENSE.txt` and `THIRD_PARTY.md` under
`/unifrog`. Keep `THIRD_PARTY.md` current whenever adding, removing, or
replacing fetched or vendored third-party code.

Game discovery is directory based. The `rom_root` setting defaults to
`/ROMS`, which means the frontend browses system folders directly under
`/ROMS`. Older `rom_roots` settings are accepted for compatibility and use the
first configured root. Files inside a recognized system folder are treated as
games without guessing from their file extensions. Common retro-handheld folder
aliases such as `gba`, `gb`, `gbc`, `nes`, `snes`, `megadrive`, `pcengine`,
and `psx` are matched case-insensitively. Edit
`/unifrog_data/settings.ini` on the SD card to change user options without
modifying packaged defaults.

SD builds boot in `wide20` by default: 4-bit SD, 20 MHz, high-speed disabled,
and no UHS/1.8 V negotiation. ROM and native module loading stay on that boot
profile; the old runtime fast-read mount/remount windows are disabled by the
default `SD_READ_MODE=boot`. The UHS profiles remain available as diagnostics,
but they advertise 1.8 V/UHS capability to the vendor MMC stack and can wedge
under repeated runtime switching on SF2000-class boards. The frontend
`fast_sd` option and MuOS Storage -> Mode expose `boot`, `wide1`, `wide2`,
`wide4`, `wide8`, `wide10`, `wide12`, `wide14`, `wide16`, `wide18`,
`wide20`, `wide22`, `wide24`, `wide25`, `hs1`, `wide50`, `wide`, `uhs12`,
`uhs25`, and `uhs` because card behavior varies.
The low `wide*` profiles keep 4-bit 3.3 V signaling with high-speed timing
disabled; only the bus clock changes. In theory, throughput scales with clock
and bus width, and 1.8 V/UHS can reduce SD I/O switching power, but card and
host reliability dominate here. At these clocks SD-card power is usually small
next to the SoC, DRAM, backlight, audio, and emulator load, so 1.8 V is not
expected to be a meaningful heat or battery win on this board. If a ROM load
stalls while using an experimental runtime profile, the load watchdog attempts
to restore the boot storage profile once before showing the core hang screen.
Developer -> Storage test runs a quick guarded runtime sweep:
it buffers logs, shows progress on screen, prefers `/ROMS/test.md` when
present, verifies a safe remount first, switches profiles through the SD bus
suspend/resume hooks, tries the low-speed 4-bit `wide1`, `wide2`, `wide4`,
and `wide8` profiles, then restores the boot profile before writing
`/unifrog/storage-test-result.txt` and the on-device report. Developer ->
Storage full test uses `/ROMS/probes/test*.md`, restores the boot profile after
each experimental read, and checkpoints
`/unifrog/storage-full-test-result.txt` between modes. Developer -> Storage
mode test selects one profile, switches once, runs all probes, then restores
the boot profile and writes `/unifrog/storage-mode-test-result.txt`. The on-screen
stage is the best freeze clue; warm reboot diagnostics may also keep it, but a
full power cycle can overwrite that memory. Fixed-profile diagnostic boot
builds are still available with `SD_MODE=safe`, `wide1`, `wide2`, `wide4`,
`wide8`, `wide10`, `wide12`, `wide14`, `wide16`, `wide18`, `wide20`,
`wide22`, `wide24`, `wide25`, `wide37`, `hs1`, `wide50`, `wide`, `uhs12`,
`uhs25`, or `uhs`; non-default modes are diagnostic. Device logs
rotate to `log-prev.txt` when `log.txt` grows past 1 MiB. Buffered UniFrog log
flushes and SDK file UART drains use synchronous SD writes before closing the
log file, except during libretro core sessions and storage quiet windows where
sync log records stay in retained RAM until a safer flush point. The board DTS in
`board/hc15xx/common/dts/sf2000_min.dts` is the single source used by both the
firmware and SDK kernel rebuild.

## Layout

```text
board/                 Local SF2000 device tree input
cores/                 Libretro source manifest, patches, and core build
docs/                  Hardware, ABI, loader, and diagnostics notes
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
`js2300/output/`, and packaging directories. They are not
source.

## Component Docs

- `docs/README.md` maps the retained hardware and architecture notes.
- `cores/README.md` explains core source fetching, patching, and ABI rules.
- `docs/js2300-scripting.md` covers optional JS2300 scripts.
- `js2300/README.md` covers the embedded JavaScript runtime layer.

Each component Makefile has its own quick reference:

```sh
make -C cores help
make -C js2300 help
```
