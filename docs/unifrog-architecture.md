# UniFrog Architecture

UniFrog is a native SF2000 runtime, an embedded JavaScript layer, an editable
SD-card frontend, and SD-loaded core modules.

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
output/sdcard/unifrog/            SD-card frontend and core package
```

## JavaScript Boundary

`js2300/` embeds MQuickJS and registers native bindings. `frontend/` contains
the editable JavaScript UI, quick menu, themes, and diagnostics installed under
`/unifrog` on the SD card. Native code provides high-throughput bindings; UI
screens belong in JavaScript.

Updating JavaScript or themes should not require relinking firmware. Updating
native bindings, runtime modules, or ABI headers requires a rebuild.
Packaged JavaScript ships as source plus manifest-verified MQuickJS bytecode.

## Core Boundary

Generic emulator modules are libretro cores. UniFrog-specific modules may use
the versioned ABI table in `include/unifrog/abi.h` for device services.

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
- `unifrog/perf.h`: timing, cache, and address helpers
- `unifrog/platform.h`: board setup and fallback loop
- `unifrog/png.h`: PNG loading helpers
- `unifrog/presenter.h`: high-throughput RGB565 presentation
- `unifrog/runtime.h`: runtime identity and screen constants
- `unifrog/text.h`: small text helpers
