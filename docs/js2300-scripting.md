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
- maximum `JS2300.fs.list()` result: 1024 entries
- no DOM, browser APIs, promises/timers, network, dynamic native modules, or
  threads
- `setTimeout()` is intentionally disabled

Scripts should do short batches of work and sleep between polling or storage
operations.

## Global API

`load("relative.js")` evaluates another script relative to the current script
directory. Absolute paths are accepted by the runtime, but scripts intended for
users should stay inside `/unifrog_data/scripts`.

The `JS2300` object exposes:

- `JS2300.log(message)`, `JS2300.flushLog()`, `JS2300.now()`,
  `JS2300.sleep(ms)`, `JS2300.gc()`, `JS2300.exit(reason)`
- `JS2300.system.battery()`, `backlight(level)`, `avOutput(mode)`,
  `action(id)`
- `JS2300.fs.list(path)`, `index(root, gameIndexPath, mediaIndexPath)`,
  `readText(path)`, `writeText(path, text)`

`JS2300.system.action(id)` is the bridge to UniFrog runtime actions. Useful
actions include `script:<path>`, `storage:recover`, `toast:<message>`, and
developer diagnostics such as `developer:system_check`,
`developer:display_benchmark`, `developer:display_color_test`,
`developer:audio_test`, `developer:storage_test`,
`developer:storage_full_test`, `developer:storage_quick_benchmark`, and
`developer:storage_stress:<profile>`.

## Script Modes

Scripts default to **standalone** mode. Standalone scripts run outside the
native frontend screen flow and can request native actions.

Scripts that start with this header run as **extension** scripts:

```js
// unifrog: mode=extension
```

Extension scripts run embedded in the native frontend. They can automate or
extend UniFrog without replacing the native UI. The bundled `frontend-driver.js`
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
