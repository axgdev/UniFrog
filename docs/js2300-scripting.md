# JS2300 Scripting

JS2300 is an optional scripting runtime, not a frontend. The frontend can
run standalone scripts from Apps -> JavaScript Scripts. Put scripts under:

```text
/unifrog_data/scripts/
```

`.js` and `.mjs` files are selectable. If a matching `.js.mqbc` and
`bytecode-manifest.txt` are present beside the script, JS2300 may use verified
bytecode; otherwise it runs source.

## Runtime Limits

The standalone JS2300 engine uses its vendored MQuickJS implementation with a
small fixed device profile:

- default heap: 8 MiB
- default stack: 256 KiB
- maximum source script: 512 KiB
- maximum text file read/write: 2 MiB
- maximum binary file read through `readBytesInto()`: 4 MiB
- maximum `JS2300.fs.list()` result: 1024 entries
- no DOM, browser APIs, promises/timers, network, dynamic runtime modules, or
  threads
- `setTimeout()` is intentionally disabled

Scripts should do short batches of work and sleep between polling or storage
operations.

## Global API

`load("relative.js")` evaluates another script relative to the current script
directory. Absolute paths are accepted by the runtime, but scripts intended for
users should stay inside `/unifrog_data/scripts`.

The `JS2300` object exposes:

- `JS2300.info()`, `JS2300.log(message)`, `JS2300.flushLog()`, `JS2300.now()`,
  `JS2300.sleep(ms)`, `JS2300.gc()`, `JS2300.exit(reason)`,
  `JS2300.mode()`
- `JS2300.system.battery()`, `backlight(level)`, `avOutput(mode)`,
  `cpu(mhz?)`, `action(id)`
- `JS2300.fs.list(path)`, `index(root, gameIndexPath, mediaIndexPath)`,
  `readText(path)`, `readBytesInto(path, uint8Array, maxBytes?)`,
  `writeText(path, text)`, `writeBytes(path, uint8Array, maxBytes?)`,
  `stat(path)`, `mkdir(path)`, `remove(path)`, and `rename(from, to)`
- `JS2300.core.*` when the script is running as the JS2300 libretro core

`JS2300.info()` returns the runtime/API version, mode, heap size, and capability
flags. Check a capability before using an optional host service. Filesystem
mutation functions return `0` on success and `-1` when unavailable or failed.
`remove()` removes one file or one empty directory; it is intentionally not a
recursive delete.

`JS2300.system.cpu()` returns the current CPU clock. Passing a supported MHz
value applies it for a standalone/extension script; UniFrog restores the prior
clock when the script exits, including after a JavaScript exception. CPU clock
changes are unavailable in libretro-core mode because the frontend owns the
core session clock.

`JS2300.video.image(path, x, y, width?, height?)` decodes and draws a supported
image into the current frame. `JS2300.video.font(path)` loads a theme/user font
for subsequent text calls. `JS2300.input.poll()` returns the same normalized
12-button mask in standalone, extension, and libretro-core modes.

`JS2300.mode()` returns `standalone`, `extension`, or `libretro-core`. Use it
as the first runtime check when a file could be launched from more than one
place.

`JS2300.system.action(id)` is the bridge to UniFrog runtime actions. Useful
actions include `script:<path>`, `storage:recover`, `toast:<message>`, and
developer diagnostics such as `developer:system_check`,
`developer:display_benchmark`, `developer:display_color_test`,
`developer:audio_test`, `developer:storage_test`,
`developer:storage_full_test`, `developer:storage_quick_benchmark`, and
`developer:storage_stress:<profile>`.

Bounded playback actions are available to diagnostics:

- `run+core=gpsp,audio=1,frames=900:/path/to/game.gba`
- `media+mode=native,audio=1,ms=15000:/path/to/video.mp4`
- `media+mode=ffmpeg,audio=1,ms=15000:/path/to/video.mp4`
- `media+mode=ffmpeg,audio=1,video=0,ms=15000:/path/to/video.mp4`

The media action accepts `native`, `ffmpeg`, or `hcplayer`; unavailable modes
return failure instead of silently substituting another requested mode. Use
`video=0` when probing audio from a video file without exercising the software
video presentation path.

`js2300/scripts/audio-quality-matrix.js` uses this config file:

```ini
/media/mmcblk0/unifrog_data/config/audio-quality-matrix.ini
```

First run creates a template. Replace the two paths before rerunning:

```ini
game_path=/media/mmcblk0/ROMS/GBA/REPLACE_ME.gba
game_core=gpsp
video_path=/media/mmcblk0/VIDEOS/REPLACE_ME.mp4
duration_s=15
game_fps=60
scpu=918
frameskip=1
video_modes=native,ffmpeg_audio
allow_software_video=0
run_route_diagnostics=1
require_route_diagnostics=0
```

For `audio-quality-matrix.js`, `ffmpeg_audio` probes FFmpeg audio decode from
the video file without presenting software video. `native` is the production
video path and should show video while audio plays. `ffmpeg_video` can still be
added to `video_modes`, but it is skipped unless `allow_software_video=1`
because software H.264 video is a diagnostic path on the target and may be too
limited for real playback. `run_route_diagnostics=1` runs audible route probes,
and `require_route_diagnostics=1` or
`run_route_diagnostics=strict` makes those probes fatal on failure.

## Libretro Core Mode

The SD package includes `unifrog/cores/js2300.bin`, a libretro core whose
content can be a `.js`, `.mjs`, `.ch8`, or `.chip8` file. Launching a script as
content, or selecting core id `js2300`, runs the script inside the libretro
frontend instead of as a standalone app script.

Core scripts should start with:

```js
// unifrog: type=libretro-core
```

The native Apps script browser hides files with that marker so users do not
accidentally run a libretro core script as a standalone tool. Standalone scripts
belong directly under `/unifrog_data/scripts`; packaged core scripts belong
under `/unifrog_data/scripts/js2300-cores`.

A JS2300 core script can define these optional global callbacks:

```js
function retroLoad(path) {
  if (JS2300.mode() !== "libretro-core")
    return false;
  JS2300.core.clear(0);
  return true;
}

function retroRun() {
  var buttons = JS2300.input.poll();
  JS2300.core.fillRect(0, 0, 320, 240, buttons ? 0xffff : 0);
  JS2300.core.present();
}

function retroUnload() {
}
```

The initial core profile is fixed at 320x240 RGB565, 60 fps, and 32000 Hz
stereo audio. `JS2300.core.info()` returns `{ width, height, pitch, format,
frame }`, where format `0` is RGB565.
`JS2300.core.contentPath()` returns the original content path. For `.ch8`
content, UniFrog loads the packaged CHIP-8 core script and `contentPath()`
points at the ROM.

Use bulk operations in frame code:

- `JS2300.core.clear(color565)`
- `JS2300.core.fillRect(x, y, w, h, color565)`
- `JS2300.core.blitRGB565(x, y, w, h, pixels)`
- `JS2300.core.blitIndexed8(x, y, w, h, pixels, palette565)`
- `JS2300.core.blitIndexed4(x, y, w, h, packedPixels, palette565)`
- `JS2300.core.blitIndexed8Scaled(x, y, w, h, scale, pixels, palette565)`
- `JS2300.core.blitIndexed4Scaled(x, y, w, h, scale, packedPixels, palette565)`
- `JS2300.core.audioS16(samples, channels)`
- `JS2300.core.present()`

`pixels`, `palette565`, and `samples` should be typed arrays for performance:
`Uint16Array` for RGB565 and palettes, `Uint8Array` for indexed pixels, and
`Int16Array` or `Uint16Array` for signed audio samples. Plain arrays work, but
they are copied element by element and are intended for tests or small buffers.

`blitIndexed4()` and `blitIndexed4Scaled()` expect two pixels per byte, high
nibble first. `audioS16()` accepts mono (`channels = 1`) or interleaved stereo
(`channels = 2`).

`JS2300.fs.readBytesInto(path, uint8Array, maxBytes?)` fills an existing
`Uint8Array` or `Uint8ClampedArray` and returns the number of bytes read, or
`-1` on error. This avoids allocating/copying a JavaScript array for ROMs and
binary assets.

`JS2300.fs.writeBytes()` is the matching allocation-free binary write call.
The UniFrog host writes through a temporary file and commits it atomically;
the JS2300 libretro core provides the same API for save data and assets. Both
binary operations are bounded to 4 MiB per call. Use chunks or a native C core
for larger streaming workloads.

## CHIP-8 Example Core

`js2300/scripts/js2300-cores/chip8.js` is the reference JS2300 libretro core. It keeps
the CHIP-8 CPU, timers, keypad mapping, and packed 64x32 framebuffer in
JavaScript typed arrays, then relies on C for binary ROM loading, scaled
indexed blits, audio submission, input polling, and presentation.

Any `.ch8` or `.chip8` ROM launched from UniFrog is routed to this packaged
script automatically. Launching `chip8.js` directly as libretro content runs a
small built-in demo ROM, which is useful when testing the core without an SD
card ROM set.

For external test ROMs, the Timendus CHIP-8 test suite is a good starting
point:

```sh
git clone https://github.com/Timendus/chip8-test-suite.git
cp chip8-test-suite/bin/1-chip8-logo.ch8 /media/mmcblk0/ROMS/CHIP8/
```

The example is intentionally small and readable rather than a cycle-perfect
compatibility target. It exists to show the preferred performance shape for
future JS2300 cores: keep emulator state and decode logic in JavaScript, keep
large byte moves and frame/audio submission in C.

Avoid `setPixel()` in real frame loops; it is only for debugging or tiny
overlays. JavaScript on MIPS32r1 without an FPU will not match optimized C for
emulator hot loops, so JS2300 core performance depends on doing as much work as
possible in typed arrays and calling C bulk helpers once per tile, row, frame,
or audio chunk.

## Script Modes

Scripts default to **standalone** mode. Standalone scripts run outside the
frontend screen flow and can request native actions.

Scripts that start with this header run as **extension** scripts:

```js
// unifrog: mode=extension
```

Extension scripts run embedded in the frontend. They can automate or
extend UniFrog without replacing the frontend UI. The bundled `frontend-driver.js`
uses this mode.

Library/helper scripts should be named with a leading underscore, for example
`_fd-lib.js`. The native script browser hides leading-underscore entries so
users do not accidentally run libraries directly, while other scripts can still
load them with `load("path/_library.js")`.

## Script Style

Use C-side services for expensive work. JavaScript is appropriate for
diagnostics, small tools, and experiments. It should not replace hot-loop UI,
ROM scanning, image decoding, or emulation code. Long file reads should be
delegated to native actions where possible because SD-card stability is better
when the native runtime can defer and flush logs around storage work.
