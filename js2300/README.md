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

The root build links `js2300/output/libjs2300.a` into the firmware. The default
MQuickJS checkout is `.deps/mquickjs`, fetched by `make deps`.

## Commands

```sh
make -C js2300
make -C js2300 check
make -C js2300 MQUICKJS_DIR=/path/to/mquickjs check
```

`make check` builds the runtime and verifies that the embedded JavaScript
standard-library source compiles with the configured MQuickJS checkout.

## Contract

- Keep hot paths in C and expose batched JavaScript APIs.
- Avoid exposing HCRTOS details directly to JavaScript.
- Keep user customization in SD-card files, not firmware rebuilds.
- Keep bindings versioned while UniFrog is pre-1.0.
- Match frontend syntax to the MQuickJS parser used on device.
