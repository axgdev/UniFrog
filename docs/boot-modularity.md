# Boot Modularity And Startup Benchmarks

UniFrog's target is a small resident firmware that reaches the JavaScript UI
quickly, then loads optional work from the SD card only when needed.

## Current Split

Already external to `unifrog.bin`:

- JavaScript frontend: `/unifrog/main.js`, `/unifrog/app`, `/unifrog/themes`,
  and `/unifrog/scripts`
- libretro cores: `/unifrog/cores/*.bin`
- JS2300 reusable core artifact: `/unifrog/cores/js2300.bin`
- user settings, game indexes, media indexes, save files, themes, and scripts

Still resident in `unifrog.bin`:

- board/platform setup
- storage, logging, panic/recovery, fastboot handoff
- input, wireless input, framebuffer, GE presenter, backlight, battery
- libretro module loader and host
- compression helpers needed before a core has started
- media playback wrapper and the vendor media libraries linked by HCRTOS

The biggest remaining modularity candidate is media playback. Moving vendor
media libraries out of the resident image would reduce firmware size, but it
requires an external-module ABI for non-libretro native services or a
libretro-style media module. That is larger and riskier than the input/clock
fixes, so the current release keeps media resident and measures boot first.

## Startup Metrics

The firmware now emits low-volume boot timing lines:

- `unifrog boot_time stage=storage_ready ...`
- `unifrog boot_time stage=js_fb_ready ...`
- `unifrog boot_time stage=loading_screen ...`
- `unifrog js launch ... boot_ms=... relaunch=...`
- `unifrog boot_time stage=js_runtime_created ...`
- `frontend ready input_ms=...`

Use these to compare `unifrog.bin` size and frontend-ready time across builds.
The important user-facing metric is the time from boot to `frontend ready`,
which is after the first JS frame has been drawn and input polling is active.

## Next Safe Modularization Steps

1. Keep cores and JS assets external, as they are now.
2. Benchmark the resident firmware with the boot-time lines above.
3. If firmware size dominates startup, move media playback behind an external
   native module interface.
4. Keep module boundaries ABI-based. Do not expose raw firmware addresses.
5. Avoid asynchronous startup work that competes with the first UI input poll.
   If background indexing or preloading is added, it should start only after
   the first frontend-ready log line and should yield often.

This keeps the first release simpler while still leaving a measurable path to
shrinking the resident image.
