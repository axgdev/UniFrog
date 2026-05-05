# JS2300

`js2300/` is UniFrog's MQuickJS embedding layer. It owns VM setup, native
binding registration, bytecode execution, and the `JS2300.*` API used by the
SD-card frontend.

## Layout

```text
include/js2300/      Public C interface for embedding JS2300
src/                 Runtime and binding implementation
Makefile             Static-library build and syntax check entry point
```

The root build links `js2300/output/libjs2300.a` into the firmware. The direct
JS2300 default is `../.deps/mquickjs`, which is the same checkout the root
Makefile fetches as `.deps/mquickjs`.

## Commands

```sh
make -C js2300 help
make -C js2300 print-config
make -C js2300
make -C js2300 check
make -C js2300 MQUICKJS_DIR=/path/to/mquickjs check
```

`make check` builds the runtime and verifies that the embedded JavaScript
standard-library source compiles with the configured MQuickJS checkout.
The global `load("relative/path.js")` helper evaluates scripts relative to the
current app root, which lets the SD-card frontend stay modular without a
bundling step.

When a packaged `.js.mqbc` file and `bytecode-manifest.txt` are present,
JS2300 verifies source and bytecode fingerprints before executing bytecode.
This avoids relying on FAT timestamps and falls back to source if a file was
edited without rebuilding bytecode or if the VM rejects bytecode for the current
atom-table state.
To keep `load("...")` fast and reliable, JS2300 preloads manifest-matched
entry scripts and literal `load()` dependencies before attaching the `JS2300.*`
API. The JS2300-owned MQuickJS build raises the ROM atom-table limit for these
preloaded bytecode files.

## Contract

- Keep hot paths in C and expose batched JavaScript APIs.
- Avoid exposing HCRTOS details directly to JavaScript.
- Keep user customization in SD-card files, not firmware rebuilds.
- Keep bindings versioned while UniFrog is pre-1.0.
- Match frontend syntax to the MQuickJS parser used on device.
