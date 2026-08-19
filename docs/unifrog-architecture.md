# UniFrog Architecture

UniFrog is a native SF2000 runtime, a MuOS-inspired native frontend, optional JS2300
scripts, and SD-loaded modules.

## Runtime Boundary

The native runtime owns hardware-specific and expensive work: framebuffer and
GE presentation, PCM output, controller input, battery sampling, logging,
storage paths, media playback, standby/reboot, fastboot firmware handoff, cache
maintenance, and libretro hosting.

Public C interfaces live under `include/unifrog/`. Code outside the native
runtime should not include private headers from `src/`.

The build emits:

```text
bisrv.asd                         Native SF2000 firmware payload
output/unifrog.bin                Raw firmware binary
output/libunifrog.a               Runtime archive used by modules
output/sdcard/bios/bisrv.asd      Fastboot image installed as stock firmware
output/sdcard/unifrog/firmware/unifrog.bin
                                  Raw firmware loaded by fastboot
output/sdcard/unifrog/            Distribution-managed firmware data and cores
output/sdcard/unifrog_data/       User-owned state and extensions
output/sdcard/unifrog/LICENSE.txt UniFrog license notice
output/sdcard/unifrog/THIRD_PARTY.md
                                  Third-party attribution and source notes
```

## Frontend Boundary

`FRONTEND_IMPL=native` is the only boot frontend. It builds UniFrog native
frontend code in `src/native_frontend.c` and renders through
`src/frontend_lvgl.c`. The UI keeps MuOS-inspired launcher, explore,
collection, history, information, and configuration flows without requiring the
upstream MuOS checkout.

The package keeps distribution files under `/unifrog` and mutable state under
`/unifrog_data`. Native frontend code uses `unifrog/ui.h` for framebuffer presentation and
normalized menu input, and calls public runtime services such as content
launch, media playback, backlight, firmware handoff, and storage diagnostics.
The in-game quick menu is compiled into the libretro host, so no JavaScript
frontend files are required on the SD card.

## JavaScript Boundary

The pinned `.deps/frog2k-javascript/` checkout embeds MQuickJS and registers the
portable JS2300 runtime. It is no longer a frontend implementation. The native frontend exposes Apps -> JavaScript Scripts
for optional standalone scripts in `/unifrog_data/scripts`; the native frontend,
quick menu, themes, and diagnostics stay in C/LVGL-side code.

The JS2300 host bridge is split by role:

- `src/frontend/js2300_frontend.c`: JS2300 host wiring and native script entry
- `src/frontend/js2300_frontend_bindings.c`: script logging, timing,
  filesystem, battery, backlight, AV, and lightweight indexing bindings
- `src/frontend/js2300_frontend_actions.c`: script-triggered diagnostics and
  system checks
- `src/frontend/js2300_frontend_storage.c`: storage diagnostics

Updating optional scripts does not require relinking firmware. Updating native
bindings, runtime modules, or ABI headers requires a rebuild.

## Core Boundary

Generic emulator modules are libretro cores. UniFrog-specific modules, such as
`/unifrog/modules/hcrtos-media.bin`, use the same loader and the versioned ABI
table in `include/unifrog/abi.h` for device services.

Module binaries retain the licenses of their upstream sources. The active
inventory lives in `../THIRD_PARTY.md`; `cores/manifest.mk` and
`cores/patches/` are the reproducible source contract for libretro modules.

ABI rules:

- ABI-visible structs start with `size` and version or magic fields.
- Existing fields stay stable; additive callbacks go at the end.
- Callers must tolerate missing callbacks.
- Private `src/` symbols and raw linker addresses are not stable contracts.

External loading details live in `external-core-loading.md`; fixed memory
regions live in `memory-layout.md`.

## Current Public Modules

- `unifrog/abi.h`: versioned ABI table for external modules
- `unifrog/audio.h`: S16 PCM output
- `unifrog/backlight.h`: display backlight control
- `unifrog/battery.h`: ADC-backed battery sampling
- `unifrog/boot.h`: fastboot firmware handoff
- `unifrog/fb.h`: framebuffer helpers
- `unifrog/ge.h`: HCGE fill/blit/stretch helpers
- `unifrog/gfx.h`: RGB565 drawing primitives
- `unifrog/input.h`: local and wireless controller input
- `unifrog/log.h`: buffered device logging
- `unifrog/path.h`: bounded path helpers
- `unifrog/paths.h`: canonical SD-card distribution and user-data paths
- `unifrog/perf.h`: timing, cache, and address helpers
- `unifrog/platform.h`: board setup and fallback loop
- `unifrog/png.h`: PNG loading helpers
- `unifrog/presenter.h`: high-throughput RGB565 presentation
- `unifrog/runtime.h`: runtime identity and screen constants
- `unifrog/text.h`: small text helpers
