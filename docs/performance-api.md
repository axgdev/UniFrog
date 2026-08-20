# UniFrog Performance API

This pass exposes the fast paths that are confirmed by the current HCRTOS SDK
and by device testing.

## Public Performance Building Blocks

- `unifrog/perf.h`
  - CP0 count reads for low-overhead profiling.
  - RTOS-tick wall-clock helpers for time that must survive runtime SCPU
    changes.
  - `unifrog_perf_delay_us()` for short hardware delays calibrated from the
    current SCPU clock registers.
  - Cache flush, invalidate, and flush-invalidate wrappers.
  - Cached, uncached, and physical address helpers/macros for device engines.
  - A runtime capability query that reports framebuffer, GE, display, video,
    audio, ADC, storage, backlight, wireless input, and SCPU clock clues.
- `unifrog/fb.h`
  - Opens and maps `/dev/fb0` as RGB565 without exposing HCRTOS framebuffer
    structs to callers.
  - Returns both CPU drawing surfaces and GE-compatible surfaces.
  - Supports explicit framebuffer cache flush, vsync wait, and y-pan buffer
    switching when extra framebuffer memory is available.
- `unifrog/ge.h`
  - Wraps the HCGE accelerator as a UniFrog display API.
  - Supports queued fill, blit, and stretch-blit operations.
  - Leaves synchronization explicit with `unifrog_ge_sync()` so callers can
    batch operations.
  - Supports explicit source/destination cache flush flags for DMA-style use.
  - Provides `unifrog_ge_set_fast_clock()`, currently mapped to the 198 MHz
    selector because the latest device run showed it is faster than the
    225/238 selectors for fill and stretch workloads.
- `unifrog/presenter.h`
  - Combines framebuffer and GE setup into the preferred RGB565 presentation
    path for emulator frames.
  - Supports GE-backed clear, source cache flush, stretch-to-screen,
    optional aspect preservation, optional vsync, and y-pan buffering.
- `unifrog/audio.h`
  - Wraps `/dev/sndC0i2so` PCM output for low-latency S16 interleaved audio.
  - Defaults to 512-byte periods and four periods, matching the lowest-latency
    result from the current audio DMA benchmark.

## Fast Presentation Path For Libretro

The preferred 16-bit emulator frame path is `unifrog_presenter_present_rgb565()`.
It performs the following sequence internally:

1. Keep the core output in RGB565.
2. Flush the cached source frame before GE reads it.
3. Stretch-blit through GE into a non-current framebuffer page when available.
4. Sync the GE queue once for the submitted frame.
5. Wait for vsync if requested, then pan to the completed page.

Avoid CPU-side full-screen scaling for common handheld resolutions such as
160x144, 240x160, 256x224, and 320x240. GE stretch-blit is the hardware path
already shown to work on device.

The presenter caches invariant aspect-fit geometry and pans using the tracked
framebuffer mode, avoiding 64-bit scale division and an extra framebuffer mode
ioctl on every emulated frame. Source cache flush and GE synchronization remain
mandatory: cores may reuse their frame buffer immediately after the video
callback returns.

## Build Optimization Policy

UniFrog keeps optimization policy in the parent build instead of carrying
extra source changes in upstream libretro cores. The default split is:

- `OPT_SIZE=-Os` for most firmware and runtime code.
- `OPT_FAST=-O2` for shared hot presentation/cache helpers and the libretro
  run-loop/runtime callbacks.
- `OPT_AUDIO=-Os` for the low-level, device-specific audio driver objects.

This keeps the measured-safe SF2000/GB300 audio hardware paths on `-Os` while
letting all cores benefit from faster shared frame, callback, mixing, and video
presentation code. Local experiments should use untracked `config.mk`
overrides, for example `OPT_FAST=-O3`, before promoting a new default.

## Runtime Performance Logs

Normal libretro runs emit low-volume `unifrog perf`, `unifrog perf_cpu`, and
`unifrog perf_audio` lines every 600 frames and one final set on exit. The
report is split across short lines so it fits UniFrog's in-memory log line
buffer without clipping. It is intended for comparing builds without drawing an
on-screen debug overlay or continuously writing to SD while a core is running.

UniFrog paces `retro_run()` from the core's advertised video refresh rate. The
pacer uses the OS tick/usleep clock for real-time speed control. CP0 Count is
calibrated against the OS tick after runtime SCPU changes and then used for
relative cost measurements.

Long `retro_load_game()` calls are monitored separately. The load watchdog now
allows a longer no-progress window for uninstrumented cores, and UniFrog-linked
core modules can call `unifrog_core_load_progress(stage, current, total)` from
slow load-only stages to feed the watchdog without adding per-frame overhead.
The module support glue forwards that hook through the UniFrog ABI, so it still
updates the loading screen while `retro_load_game()` is running.
For cores that set `need_fullpath=false`, UniFrog can display frontend file-read
progress without core changes. For full-path cores such as gpSP, true internal
ROM/decompression/reset progress requires this additive UniFrog hook because
the standard libretro API has no load-progress callback.
If a watchdog panic still fires, the screen labels `PHASE`, `MARK`, `BEAT`, and
`STABLE` identify whether it stopped during load or run.

Key fields:

- `actual_fps_x100`, `wall_ms`, `frame_wall_avg_us`, `pace_period_us`:
  real-time speed over the report window. `actual_fps_x100=6000` means 60.00
  FPS. Wall-clock fields are authoritative after runtime clock changes.
- `run_avg` / `run_max`: total host frame time in CP0 counts.
- `active_avg` / `active_max`: host frame time after subtracting presenter
  vsync wait, so this is the primary slowdown metric.
- `options_audio`, `audio_gain`, `frameskip`, `scpu_target`, `scpu_now`, `ge_clock`,
  `backlight`: launch options active for this run. `frameskip=0` means off,
  `1` means auto, and `2`/`3` mean fixed interval 1/2. `scpu_target=0` means
  keep the boot clock. `audio_gain` is fixed at 1x on SF2000-family builds and
  remains in logs only to catch accidental regressions.
- `pace_wait`, `pace_wait_avg_us`, `pace_late`, `pace_reset`: how often the
  frontend had spare time to sleep, how much it slept, and whether the core
  missed real-time deadlines.
- `count_hz` / `count_cal` / `budget` / `slow`: CP0 count rate, whether it was
  calibrated from the OS tick, per-frame count budget, and how many frames in
  the interval exceeded it. Treat these as CPU-cost fields; the real-time speed
  fields above are authoritative for "too fast" or "too slow" behavior.
- `slow125`, `slow150`, `slow200`, and `active_max_frame`: how many frames
  exceeded 125%, 150%, or 200% of the calibrated budget, and which frame was
  worst in the report window. Sparse `perf_slow` lines are emitted for severe
  spikes and include recent presenter GE/sync/pan timing plus audio delay, so a
  benchmark can separate core CPU from display or audio backpressure.
- `present_avg`, `ge`, `sync`, `vsync`, `pan`: presenter timing split.
  Libretro presentation avoids mandatory vsync waits by default; a nonzero
  `vsync` field means a caller explicitly requested synchronized pan.
- `audio_delay`, `audio_fail`, `audio_write_avg`, `audio_write_max`, `gain`,
  `peak`, `clip`, `gate`, `quiet_frames`: audio backlog, write-wait pressure,
  fixed software gain, output level, clipping, and silence-gate state for
  crackle or analog-noise diagnosis. `quiet_frames` is duration-independent of
  the core's callback batch size.
- `status`, `status_active`, `status_under`, `occ_avg`, `occ_min`, `occ_max`:
  libretro audio-buffer-status callback activity. These fields show whether a
  core with upstream frameskip support is receiving enough information to skip
  expensive video work when the frontend is late or the audio buffer is low.

Compare the same game section across builds. `actual_fps_x100` near the target,
some `pace_wait`, low `pace_late`, lower `active_avg`, lower `active_max`, and
stable nonzero `audio_delay` point in the right direction.

## Runtime Launch Options

The frontend opens launch and pause options around libretro games.
These options are handled by UniFrog's generic libretro host and are
intentionally outside individual core source changes:

- Audio can be disabled for CPU-bound tests. UniFrog skips opening the audio
  device and advertises disabled audio through
  `RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE` so cooperative cores can avoid
  synthesis work.
- Audio gain used to be a generic software multiplier after stereo-to-mono
  mixing. The SF2000 output is already loud enough and higher gain can clip, so
  UniFrog now fixes the libretro path at 1x and uses hardware volume/mute plus
  the mono left route for quality.
- Libretro audio is opened at the core-reported sample rate when it is in the
  safe 8-48 kHz hardware range. This avoids the old fixed-32 kHz
  drop/duplicate resampling path for normal cores. Output is duplicated to both
  I2SO channels after software mono mixing because the SF2000 speaker route is
  effectively mono.
- Silence gating measures quiet duration in PCM frames instead of callback
  count and preserves low-level samples instead of zeroing them. Opening the
  physical route waits briefly before the first real PCM write, which keeps
  AUDSINK transfers at the normal chunk size and preserves GB300's rule that
  the route stays muted until real PCM exists. The SND layer also holds mute
  across brief gaps and uses separate SF2000/GB300 one-shot settle windows,
  without delaying steady-state writes.
- Frameskip can be left off, set to auto, or set to fixed intervals. UniFrog
  implements the standard libretro audio-buffer-status callback so upstream
  cores that already support frameskip can react without SF2000-specific core
  changes. For gpSP, the generic launch option is translated to the upstream
  `gpsp_frameskip` variables. If audio is disabled or the driver cannot report
  delay, auto frameskip falls back to frontend lateness instead of reporting an
  unusable inactive buffer.
- The in-game quick menu can change frameskip and fast-forward speed at runtime.
  Fast-forward speed defaults to off and can be set to 2x, 4x, 8x, or 16x. For
  gpSP and gpsp-gbac-prosty, enabled fast-forward speed maps to a temporary fixed
  frameskip interval, so higher values reduce core rendering work instead of
  only dropping already-rendered frames.
- SCPU can be kept at the boot profile or switched to guarded known profiles
  at 198, 297, 396, 594, 702, 756, 810, 864, or 918 MHz. Runtime changes are
  captured before launch and restored after the core exits. The native
  launcher default is 918 MHz for libretro games; a saved user setting still
  wins until the user changes Runtime Settings -> CPU.
- GE clock can be selected between the driver-supported 198, 148, 225, and
  238 MHz selectors. This tunes presentation only; it will not reduce core CPU
  time unless presentation is the bottleneck.
- Backlight can be set for the game session and restored on return to the
  frontend. This is a power/comfort knob, not a speed knob.

## Benchmark Script

`js2300/scripts/libretro-benchmark.js` runs one core/game for a fixed frame count and
writes `/media/mmcblk0/unifrog_data/logs/reports/libretro-benchmark.txt`. By
default it benchmarks the first GBA ROM it finds with `gpsp`, audio enabled,
918 MHz SCPU, and auto frameskip. To target a specific game, create
`/media/mmcblk0/unifrog_data/config/libretro-benchmark.ini`:

```ini
core=gpsp
path=/media/mmcblk0/ROMS/GBA/example.gba
frames=3600
audio=1
scpu=918
frameskip=1
```

Use `frameskips=0,1,2,3` instead of `frameskip=...` to run the same ROM
through all frontend modes in one script invocation. The values are the
frontend enum: off, auto, fixed 1, fixed 2. That is the quickest way to compare
behavior for a scene that visibly slows down.

Use `cores=gpsp,gpsp-gbac-prosty` instead of `core=...` to benchmark multiple
compatible core implementations against the same ROM and settings. It can be
combined with `frameskips`; the script runs every core/mode pair and identifies
the core on each `RUN` and `PASS` log line. When core execution time already
dominates the frame budget, selecting a faster core is the remaining way to
improve gameplay without changing core code.

The benchmark script uses a smaller normal JS heap, not an application-memory
heap, because the JS runtime remains alive while each `run+...` action loads a
core and ROM. This leaves the single top-reserved application-memory slot free
for large zipped ROMs, which is required for 32 MiB GBA images.

Use the retained log from the same run for `perf_cpu`, `perf_audio`, and
`perf_slow` lines; the report file records the exact action and elapsed time.

When interpreting slow-core results, compare `run_avg`/`active_avg` against
`budget` first. If active core time is already far over budget while
`present_avg` is small, the bottleneck is emulation CPU time rather than GE or
framebuffer presentation. After a large stall, the frontend pacer preserves a
bounded three-frame catch-up window so cheap skipped frames can recover time
instead of being throttled by an artificial post-stall sleep. The bound avoids
long unpaced bursts after loading or menus.

## Confirmed Hardware Paths

- SCPU: stable known boot profiles around 594, 810, and 918 MHz; runtime PLL
  changes are possible but still risky outside guarded diagnostics.
- Cache: explicit cache control is required before hardware engines read cached
  source buffers.
- Framebuffer: `/dev/fb0` RGB565, mmap, pan, and vsync are available.
- GE: `/dev/ge` supports accelerated fill, blit, and stretch-blit. GE clock
  control is available through the driver and affects the accelerator, not SCPU.
  In `loghcrtos38.txt`, selector 198/148 was faster than selector 225/238 for
  the measured workloads, so `UNIFROG_GE_CLOCK_FAST` intentionally selects 198.
- Display controller: `/dev/dis` supports layer ordering and zoom used by the
  hardware video plane.
- Video: HCRTOS `/dev/viddec` can decode media through hardware when UniFrog
  creates MMZ id 0 before decoder init and provides `viddec.kshm_size`.
- Audio: `/dev/sndC0i2so` supports DMA-style PCM output.
- Storage: SD/MMC is functional; current board DTS defaults to a resilient
  4-bit 16 MHz boot profile, with 1-bit safe mode and faster profiles available
  as explicit diagnostics.
- Analog/power: PWM backlight and standby-related control are present in the
  current image. `/dev/adc` and `/dev/queryadc0` were missing in
  `loghcrtos38.txt`, so battery sampling still uses the direct fallback path.
- Wireless: UniFrog owns the stock-compatible RF gamepad polling path.
- MMZ/DSC: `/dev/mmz` and `/dev/dsc` opened in `loghcrtos38.txt`; UniFrog
  exposes them as runtime capability bits. Native video leases `mmz0` from the
  reclaimable arena for decoded video/display surfaces, while compressed KSHM
  packet rings still allocate dynamically because no named `kshm` MMZ pool is
  defined.

## On-Device Diagnostics

The JavaScript Developer menu includes `Performance test`. It records JS screen
draw, directory-list, file-read, and module-load timings to
`/unifrog/perf-test-result.txt` and `PERF ...` log lines. Libretro hosting also
logs periodic `unifrog perf_*` timing buckets while a core runs.

## Leads Not Yet Wrapped

The wider HCRTOS tree exposes more engines than the current small SDK exports:

- Additional MMZ allocator coverage, if a future device needs a dedicated
  media-memory pool again.
- DMA copy helpers from the video driver libraries.
- JPEG encoder APIs.
- DSC AES/DES/SHA hardware APIs.
- USB host/device, I2C, SPI, UART, IR, HDMI/VIN paths.

Those should be promoted only after the matching headers and device nodes are
available in `hcrtos-sdk` and after hardware tests confirm the SF2000 board
actually exposes the path safely.
