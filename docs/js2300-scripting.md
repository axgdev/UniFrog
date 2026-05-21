# JS2300 Scripting

JS2300 is an optional scripting runtime, not a frontend. The native frontend can
run standalone scripts from Apps -> JavaScript Scripts. Put scripts under:

```text
/unifrog_data/scripts/
```

Only `.js` files are selectable. If a matching `.js.mqbc` and
`bytecode-manifest.txt` are present beside the script, JS2300 may use verified
bytecode; otherwise it runs source.

## Runtime Limits

JS2300 embeds MQuickJS with a small fixed device profile:

- default heap: 8 MiB
- default stack: 256 KiB
- maximum source script: 512 KiB
- maximum text file read/write: 2 MiB
- maximum `JS2300.video.rects()` batch: 128 rectangles
- maximum `JS2300.fs.list()` result: 1024 entries
- no DOM, browser APIs, promises/timers, network, dynamic native modules, or
  threads
- `setTimeout()` is intentionally disabled

Scripts should do short batches of work, poll input explicitly, and call
`JS2300.video.present()` only when the screen changed. Avoid tight animation
loops on the SF2000 unless the script sleeps between frames.

## Global API

`load("relative.js")` evaluates another script relative to the current script
directory. Absolute paths are accepted by the runtime, but scripts intended for
users should stay inside `/unifrog_data/scripts`.

The `JS2300` object exposes:

- `JS2300.log(message)`, `JS2300.flushLog()`, `JS2300.now()`,
  `JS2300.sleep(ms)`, `JS2300.gc()`, `JS2300.exit(reason)`
- `JS2300.video.size()`, `clear(color)`, `rects(rectArray)`,
  `text(x, y, text, color)`, `image(path, x, y, w, h)`, `font(path)`,
  `present()`
- `JS2300.input.poll()`, `mask()`, `down(button)`, `pressed(button)`,
  `repeated(button, delayMs, intervalMs)`
- `JS2300.system.battery()`, `backlight(level)`, `avOutput(mode)`,
  `action(id)`
- `JS2300.fs.list(path)`, `index(root, gameIndexPath, mediaIndexPath)`,
  `readText(path)`, `writeText(path, text)`

`JS2300.system.action(id)` is the bridge to UniFrog runtime actions. Useful
actions include `run:<path>`, `script:<path>`, `video:<path>`,
`firmware:<name>`, `reboot`, `storage:recover`, `toast:<message>`, and
developer diagnostics such as `developer:system_check`.

## Script Modes

Scripts default to **standalone** mode. Standalone scripts own the JS2300
framebuffer, may draw their own UI, and behave like a separate app space.

Scripts that start with this header run as **extension** scripts:

```js
// unifrog: mode=extension
```

Extension scripts run embedded in the native frontend. They do not open or
reopen the JS2300 framebuffer, so they can automate or extend UniFrog without
showing the boot logo or replacing the native UI. The bundled
`frontend-driver.js` uses this mode.

Library/helper scripts should be named with a leading underscore, for example
`_fd-lib.js`. The native script browser hides leading-underscore entries so
users do not accidentally run libraries directly, while other scripts can still
load them with `load("path/_library.js")`.

## Script Style

Use C-side services for expensive work. `JS2300.video.clear()`,
`JS2300.video.rects()`, and opaque `JS2300.video.image()` draws use the device
GE when available, with CPU fallback for unsupported cases such as
alpha-blended PNGs. JavaScript is appropriate for diagnostics, small tools, and
experiments. It should not replace hot-loop UI, ROM scanning, image decoding,
or emulation code unless that code is moved into a native module and only
orchestrated from JS.

For libretro-like experiments, keep the frame loop cooperative: poll input,
draw or compute one bounded frame, present, then sleep. Long file reads should
be delegated to native actions where possible because SD-card stability is
better when the native runtime can defer and flush logs around storage work.
