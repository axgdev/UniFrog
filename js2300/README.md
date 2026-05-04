# JS2300

JS2300 is the script runtime layer for UniFrog. It embeds MQuickJS and exposes
small native bindings for hardware-backed services while keeping UI code in
editable JavaScript files on the SD card.

This repository is intentionally separate from the UniFrog firmware and from
the UniFrog JavaScript frontend:

- UniFrog owns hardware, boot, libretro hosting, media, input, audio, graphics,
  logging, and recovery.
- JS2300 owns the JavaScript VM lifecycle, bytecode/cache policy, native binding
  registration, and the `JS2300.*` JavaScript API surface.
- UniFrogFrontend owns the user-facing JavaScript interface and can be changed
  without rebuilding firmware.

The old multicore JS2300 tree is useful history, but this repository starts
from a clean UniFrog-oriented contract.

## Design Goals

- Keep hot paths in C and expose batched JavaScript APIs.
- Avoid HCRTOS details in JavaScript.
- Make user customization a file edit, not a firmware rebuild.
- Keep the native binding ABI versioned while UniFrog is pre-1.0.
- Treat JavaScript as UI orchestration, not as an emulator hot path.

## Planned JavaScript Surface

The global object is `JS2300`.

Core runtime:

- `JS2300.version()`
- `JS2300.log(...items)`
- `JS2300.flushLog()`
- `JS2300.now()`
- `JS2300.exit(reason)`

Display and presentation:

- `JS2300.video.size()`
- `JS2300.video.clear(color)`
- `JS2300.video.rects(items)`
- `JS2300.video.text(x, y, text, style)`
- `JS2300.video.image(path, x, y, width, height)`
- `JS2300.video.present()`

Input:

- `JS2300.input.poll()`
- `JS2300.input.pressed(name)`
- `JS2300.input.repeated(name, delayMs, intervalMs)`

Filesystem and content:

- `JS2300.fs.list(path)`
- `JS2300.fs.stat(path)`
- `JS2300.fs.readText(path, maxBytes)`
- `JS2300.content.roots()`
- `JS2300.content.scan(options)`
- `JS2300.content.launch(path, options)`

System:

- `JS2300.system.battery()`
- `JS2300.system.reboot()`
- `JS2300.system.standby()`
- `JS2300.system.bootFirmware(name)`

Native bindings should prefer arrays/buffers of work over one call per pixel,
row, file, or widget.

## Repository Layout

```text
.
|-- include/js2300/      Public C interface for embedding JS2300
|-- src/                 Native runtime and binding implementation
|-- docs/                Binding and frontend integration notes
`-- Makefile             Small static-library build entry point
```

The first checkpoint is an interface skeleton. The mquickjs integration and
UniFrog binding implementation will be added in follow-up commits.
