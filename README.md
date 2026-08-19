# UniFrog

UniFrog is an OS/libretro frontend for the SF2000/GB300 device family. The
target is MIPS32r1 little-endian, 128 MiB RAM, no FPU, no MMU, and HCRTOS /
FreeRTOS with several closed-source hardware libraries.

The root `Makefile` is the source of truth. Use it for setup, builds, checks,
packaging, and dependency maintenance.

## First Build

```sh
make deps-alpine        # Alpine host packages, if needed
make setup              # SDK submodule and external source checkouts
make doctor             # toolchain, SDK, and dependency sanity checks
make dev-check          # fast path: Linux tests, firmware link, one quick core
make                    # firmware, SD-card tree, and selected cores
make verify             # full verification before handoff
```

The default MIPS toolchain is downloaded from frog-toolchain v1.3.2 into
`.deps/` by `make setup`. Override local paths in an untracked `config.mk`:

```make
TOOLCHAIN := .deps/frog-toolchain-v1.3.2-$(TOOLCHAIN_HOST_ARCH)/mipsel-mti-elf
SDK := unifrog-hcrtos-sdk
DEPS := .deps
```

Use `make help`, `make help-options`, and `make print-config` when in doubt.

## Fast Checks

```sh
make dev-check QUICK_CORE=quicknes
make quick-check
make frontend-structure-check
make architecture-check
```

`dev-check` and `quick-check` intentionally build one quick core instead of the
full core set. Run `make clean && make verify` after changing Makefiles, linker
scripts, or dependency/link wiring.

## Source Map

```text
apps/                    Firmware, Linux runner, and fastboot entrypoints
foundation/src/          Hardware backends and reusable runtime services
components/frontend/     Device UI, settings, themes, reader UI, quick menu
components/libretro/     Core loading, sessions, content, saves, pacing
components/media/        Media classification, buffering, playback
components/diagnostics/  Device test algorithms and reports
js2300/                  JS2300 host bridge, libretro core, and scripts
cores/                   Libretro source manifest, build rules, patches
include/unifrog/         Public C interfaces, one header per API area
mk/                      Shared Makefile fragments
tools/                   Host tools and procedural helpers
docs/                    Architecture and hardware notes worth keeping
```

Generated output lives in `build/`, `output/`, `cores/output/`, and
`js2300/output/`. Third-party source checkouts live in untracked `.deps/`.
Do not commit generated files, `.deps`, SDK build products, or downloaded
toolchains.

## Dependency Model

- The HCRTOS SDK is the only Git submodule because it has separate licensing
  and release ownership.
- Libretro cores and support libraries are manifest-pinned `.deps` checkouts
  with generated patch queues.
- Core source is expected to be replaced or refreshed from upstream; UniFrog
  owns the pins, build rules, and patches.

Use `make -C cores help` and `docs/dependency-workflow.md` for dependency
maintenance.

## Packaging

Important build outputs:

```text
bisrv.asd                         fastboot ASD image
fastboot.asd                      fastboot-only diagnostic ASD
output/unifrog.bin                raw firmware image
output/sdcard/                    SD-card package tree
output/UniFrog-sdcard.zip         distributable SD-card archive
```

The SD package installs firmware under `unifrog/firmware/`, libretro core
modules under `unifrog/cores/`, runtime modules under `unifrog/modules/`, and
user-owned state under `unifrog_data/`.

## Reading Next

- `docs/source-layout.md`: source ownership and boundary rules.
- `docs/developer-onboarding.md`: how to make a first change safely.
- `docs/dependency-workflow.md`: dependency pins and patch queues.
- `docs/external-core-loading.md`: module ABI and loader details.
- `docs/sf2000-hardware-findings.md`: hardware notes that are not obvious from
  source.
