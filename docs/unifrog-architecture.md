# UniFrog Architecture

UniFrog is an SF2000 runtime, a MuOS-inspired frontend, optional JS2300
scripts, and SD-loaded modules.

## Runtime Boundary

The native runtime owns hardware-specific and expensive work: framebuffer and
GE presentation, PCM output, controller input, battery sampling, logging,
storage paths, media playback, standby/reboot, fastboot firmware handoff, cache
maintenance, and libretro hosting.

Public C interfaces live under `include/unifrog/`. Code outside an owner should
not include its private implementation headers.

Build defaults and generated examples are owned by `config/options.mk`. The
root `Makefile` intentionally shows the main target graph and the commands that
connect it. Keep short recipes in the Makefile; move longer POSIX shell
workflows to `tools/`. The `mk/` directory is reserved for small shared
option/check fragments instead of broad target splits.
Default build output is intentionally quiet. Use `BUILD_PROGRESS=1` for compact
progress labels or `V=1` for full command echo; broad repository searches are
kept small by `.ignore`.

Source directories follow the main ownership boundaries: frontend code lives
under `components/frontend/`, libretro hosting under `components/libretro/`,
media playback under `components/media/`, and SF2000 board code under
`foundation/src/platform/sf2000/`.

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

UniFrog has one boot frontend: the frontend in
`components/frontend/src/app/frontend_app.c`. It renders through
`components/frontend/src/app/frontend_lvgl.c`. The UI keeps MuOS-inspired launcher, explore,
collection, history, information, and configuration flows without requiring the
upstream MuOS checkout.

The package keeps distribution files under `/unifrog` and mutable state under
`/unifrog_data`. Frontend code uses `unifrog/ui.h` for framebuffer presentation and
normalized menu input. Cold content launch, media playback, reader, script,
audio diagnostic, and bug-report actions go through
`unifrog_frontend_launch_services`; backlight, firmware handoff, and storage
diagnostics still use focused public runtime services.
The in-game quick menu is frontend presentation compiled into the
firmware, so no JavaScript frontend files are required on the SD card.

## JavaScript Boundary

`js2300/` supplies the UniFrog host adapter for the separately maintained
JS2300 engine and registers native bindings. It is no longer a frontend
implementation. The frontend exposes Apps -> JavaScript Scripts
for optional standalone scripts in `/unifrog_data/scripts`; the frontend,
quick menu, themes, and diagnostics stay in C/LVGL-side code.

The UniFrog-owned JS2300 host bridge is split by role under
`js2300/src/unifrog_host/`:

- `frontend.c`: JS2300 host wiring and native script entry
- `bindings.c`: script logging, timing,
  filesystem, battery, backlight, AV, and lightweight indexing bindings
- `actions.c`: script-triggered diagnostics and
  system checks
- `storage.c`: storage diagnostics

Updating optional scripts does not require relinking firmware. Updating native
bindings, runtime modules, or ABI headers requires a rebuild.

## Core Boundary

Generic emulator modules are libretro cores. UniFrog-specific modules, such as
`/unifrog/modules/hcrtos-media.bin`, use the same loader and the versioned ABI
table in `include/unifrog/abi.h` for device services.

Module binaries retain the licenses of their upstream sources. The active
inventory lives in `../THIRD_PARTY.md`; `cores/manifest.mk` pins upstream
commits, and generated patch queues in `cores/patches/` are the
reproducible source contract for libretro modules.

ABI rules:

- ABI-visible structs start with `size` and version or magic fields.
- Before a stable release, obsolete ABI fields may be removed instead of
  preserved.
- Additive callbacks go at the end when keeping older modules working is worth
  the complexity.
- Callers must tolerate missing callbacks.
- Private implementation symbols and raw linker addresses are not stable
  contracts.

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
