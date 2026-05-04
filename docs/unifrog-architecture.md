# UniFrog Architecture

UniFrog is split into a native runtime library, a JavaScript runtime layer, and
an editable JavaScript frontend.

The native runtime owns expensive or hardware-specific work: framebuffer access,
GE presentation, PCM output, wireless controller polling, battery/ADC sampling,
standby/reboot behavior, fastboot firmware handoff, media playback, file I/O,
and any batching needed to keep the SF2000 responsive. Those features should be
exposed through small C headers under `include/unifrog/`.

The build produces `output/libunifrog.a`, a static MIPS archive containing the
native runtime modules. HCRTOS links the base application statically into the
final `bisrv.asd` image, so this remains the practical firmware boundary. A
future `unifrog_ui` repository should consume `include/unifrog/*.h` and
`libunifrog.a` at build time, but it should not include files from `src/` or
duplicate HCRTOS hardware code.

Dynamic replacement is the normal UI model: mquickjs scripts live on the SD
card and call a stable native binding layer. Updating JavaScript files should
not require relinking. Updating the native C API, runtime modules, or JS2300
bindings still requires rebuilding the SF2000 image.

The JavaScript work is split into two repositories:

- `js2300`: the embeddable MQuickJS runtime, bytecode/cache policy, and native
  binding registration layer.
- `unifrog-frontend`: the default user-facing JavaScript UI, themes, layout,
  and interaction code.

This separation lets JS2300 evolve as reusable infrastructure while the
frontend remains easy for users to customize or replace.

Libretro cores and the future UI are both application guests. Generic upstream
cores can stay pure libretro. UniFrog-specialized cores and a UniFrog UI core may
call device services through the versioned `unifrog/abi.h` table. That ABI table
is the compatibility contract; private symbols in `src/`, raw linker addresses,
and current static archive layout are not stable.

The long-term loader should keep the UI and game cores outside the main firmware
image. At boot, the resident UniFrog runtime loads the UI app into the
reclaimable application arena. When content is selected, the UI asks the runtime
to replace the active app with the matching core binary, or to suspend the core
and load a small menu overlay if that policy proves useful. The persistent
runtime, hardware services, log buffer, ABI table, and recovery path stay outside
that arena. This allows the base firmware, UI, and cores to be updated
independently as long as the ABI remains compatible.
See `docs/boot-modularity.md` for the current resident/external split and the
boot-time metrics used to decide whether further modularization is worth the
loader complexity.

Pre-1.0 ABI policy: while the runtime is still experimental, `unifrog/abi.h`
uses version `0.1.0` and can change incompatibly. Once the loader, UI core, and
external cores have survived enough device testing, UniFrog can declare `1.0.0`
and treat that ABI as stable.

API design rules:

- Cores use `unifrog/abi.h` for stable binary calls. Other headers are
  source-level APIs unless they explicitly say they are ABI-stable.
- ABI-visible structs start with `size` and version or magic fields so callers
  can survive additive growth.
- Public headers stay HCRTOS-light unless a hardware type is unavoidable.
- Module names use `unifrog_<concern>` in C and `unifrog/<concern>.h` for
  public includes.
- APIs return plain integer status codes and fill caller-owned structs or
  buffers; avoid heap ownership across the JavaScript boundary.
- Hot paths should provide batch operations before exposing per-item calls.
- Logs use the `unifrog` prefix so hardware reports remain searchable.
- Keep hardware-facing modules behind C APIs; the current C frontend is only a
  test harness for exercising the runtime on device.

Current public modules:

- `unifrog/abi.h`: stable ABI table, semantic version fields, and the
  reclaimable application arena advertised to external UniFrog cores and future
  script/UI runtimes.
- `unifrog/runtime.h`: API version, screen constants, runtime identity.
- `unifrog/audio.h`: low-latency S16 PCM output through the native I2S/DMA
  audio device.
- `unifrog/boot.h`: one-shot reset-mediated firmware handoff for booting an ASD
  from the SD card through the tiny fastboot stage-1 loader. See
  `docs/firmware-switching.md`.
- `unifrog/log.h`: buffered logging with explicit and automatic flush support.
- `unifrog/battery.h`: battery status sampling with `/dev/queryadc0` and direct
  ADC fallback.
- `unifrog/fb.h`: RGB565 framebuffer open/map/flush/vsync/pan helpers.
- `unifrog/ge.h`: HCGE accelerated fill, blit, stretch-blit, sync, and GE clock
  control.
- `unifrog/gfx.h`: RGB565 surface drawing primitives and a compact built-in
  text renderer for lightweight device UI.
- `unifrog/input.h`: local and wireless controller polling, normalized button
  masks, snapshots, and RF diagnostic helpers. Local shift-register scanning
  and RF wireless transport live in separate runtime source files.
- `unifrog/path.h`: bounded path composition helpers.
- `unifrog/platform.h`: SF2000 boot-time board setup, storage readiness checks,
  and the fallback idle loop used if the active frontend returns.
- `unifrog/perf.h`: CP0 timing, cache/address helpers, and a runtime hardware
  capability snapshot.
- `unifrog/presenter.h`: the high-throughput RGB565 frame presenter combining
  framebuffer pages, GE stretch, cache flush, optional vsync, and aspect policy.
- `unifrog/text.h`: bounded text copy and ASCII case-insensitive suffix checks.
