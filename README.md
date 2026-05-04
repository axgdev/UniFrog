# UniFrog

UniFrog is a native SF2000 frontend and libretro runtime built for the HCRTOS
firmware environment.

This repository is intentionally source-focused. It keeps the permissively
licensed UniFrog app code, JS2300 runtime bindings, JavaScript frontend, build
rules, linker scripts, device-tree input, and small permissive third-party
sources together so changes can be reviewed as one product.

Large or mixed-license build inputs are kept outside this repository:

- `../unifrog-hcrtos-sdk` contains the HCRTOS headers, static libraries, kernel
  objects, and license notices needed to link the SF2000 application.
- `.deps/mquickjs` is fetched from upstream by `make deps`.
- `.deps/unifrog-cores` is fetched by `make deps` as a temporary compatibility
  bridge for the current libretro core build.

## Quick Start

On Alpine:

```sh
make deps-alpine
make deps
make doctor
make
```

On Ubuntu:

```sh
make deps-ubuntu
# run the printed apt command
make deps
make doctor
make
```

The default MIPS toolchain path is:

```text
/opt/mipsel-mti-elf
```

Override local paths with untracked `config.mk`:

```make
TOOLCHAIN := /opt/mipsel-mti-elf
SDK := ../unifrog-hcrtos-sdk
```

## Build Targets

```sh
make              # build bisrv.asd, unifrog.bin, and staged SD files
make deps         # fetch external dependencies into .deps/
make doctor       # check host tools, SDK, toolchain, and fetched dependencies
make check        # build and verify firmware, fastboot, layout, and JS UI
make sd-zip       # create output/UniFrog-sdcard.zip
make ci-sd-zip    # local equivalent of the CI artifact build
make clean
```

Generated files are written under `build/`, `output/`, and `.deps/`.

## Repository Layout

```text
board/       local SF2000 device tree
docs/        development notes and hardware findings
dts/         minimal device-tree include files
frontend/    editable JavaScript frontend
include/     public UniFrog C headers
js2300/      JavaScript runtime bindings around MQuickJS
linker/      HCRTOS app and core-module linker scripts
src/         native frontend, runtime, platform, media, and libretro host code
tools/       host-side build tools
```

## Dependency Policy

The app repository should stay small and easy to clone. Do not vendor generated
objects, downloaded toolchains, core upstream checkouts, MQuickJS checkouts, or
release artifacts.

The HCRTOS SDK is intentionally separate because it contains mixed-license
headers, static libraries, vendor code, and binary objects. Over time, those
pieces can be replaced with permissively licensed source without changing the
UniFrog app layout.

The libretro core flow is still being simplified. The current fresh repository
uses `.deps/unifrog-cores` to preserve the working build while removing Git
submodules from this repo. The intended next step is to replace that bridge with
direct core manifests and small UniFrog overlays.

## History

This repository was restarted from the SF2000/HCRTOS experiments and the older
split UniFrog repositories. Curated commits reference original commit hashes in
their commit bodies so the source of each grouped change can still be traced in
the private history.
