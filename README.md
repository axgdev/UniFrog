# UniFrog

UniFrog builds a native SF2000 firmware payload, JavaScript frontend package,
and libretro core modules for an SD-card based setup.

The root Makefile is the authoritative entry point. It builds `bisrv.asd`,
`output/unifrog.bin`, and `output/sdcard/unifrog`.

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
make                  Build firmware and core package
make verify           Build and verify firmware, fastboot, JS, and layout
make sd-zip           Build output/UniFrog-sdcard.zip
make clean            Remove generated build/output files
make distclean        Also clean sub-build outputs
```

`make deps` and `make check` remain supported aliases for the same workflow.
Dependency status and upgrades follow the policy declared beside each pin; use
`MODE=head` or `MODE=tag` only for an explicit one-off override. Use
`make quick-check` while iterating. `make verify` is the normal
verification before handing off changes. If linker scripts or link libraries
change, run `make clean && make verify`.

SD builds default to the reliable 1-bit profile. Diagnostic profiles are
`SD_MODE=hs1` for 1-bit high-speed, `wide50` for lower-clock 4-bit
high-speed, `wide` for full 4-bit high-speed, and `uhs12`, `uhs25`, or `uhs`
for increasing 1.8V UHS capability levels. These modes are experimental.
Run Developer -> Storage test on-device to measure read-only SD behavior; it
writes `/unifrog/storage-test-result.txt` and mirrors the result into the
frontend report screen. Device logs rotate to `log-prev.txt` when `log.txt`
grows past 1 MiB. The board DTS in
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
src/                   Native runtime implementation
tools/asdpack.c        Host tool used to pack and verify ASD images
unifrog-hcrtos-sdk/    SDK submodule
```

Generated files live in `build/`, `output/`, `cores/output/`, `.deps/`, and
subproject output directories. They are not source.

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
