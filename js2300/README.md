# UniFrog JS2300 integration

The JS2300 engine is maintained in the standalone
[`frog2k-javascript`](https://github.com/axgdev/frog2k-javascript-private)
repository and is fetched by the root Makefile. This directory contains only
UniFrog's consumer-owned host adapter, the JS-backed libretro core, and the
scripts shipped on the SD card. Keeping the OS bindings here preserves the
separation between a portable engine and HCRTOS policy.

The root build pins the engine by commit and links its
`output/libjs2300.a`; there is no second MQuickJS checkout in UniFrog.

```sh
make deps-js2300
make js2300-check
```

The host adapter owns HCRTOS display, input, storage, battery, filesystem,
and launch actions. The standalone engine owns bytecode validation, script
loading, and the public `JS2300.*` bindings. Scripts can inspect
`JS2300.mode()` when the adapter supplies an execution context.
