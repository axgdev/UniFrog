# UniFrog Core ABI

UniFrog cores are normal libretro cores plus optional UniFrog extensions.
Generic upstream cores should stay as close to stock libretro as practical.
Specialized cores may include `unifrog/abi.h` and call UniFrog device services
through the stable ABI table when that gives a real performance or integration
benefit.

This keeps core updates manageable without blocking specialized work:

- UniFrog owns device services: video presentation, audio output, input,
  storage, logging, timing, cache maintenance, and frontend policy.
- `libretro-common` is built once by `cores/Makefile` as
  `cores/output/libretro-common-sf2000.a`.
- Each core builds independently and exports the standard `retro_*` symbols or,
  for future external modules, a UniFrog core entry point that returns those
  exports.
- Core-specific changes stay on the source branches listed in
  `cores/manifest.mk`, with overlays preferred for integration files that do
  not need to edit upstream source.
- Specialized cores call only the ABI table. They must not depend on private
  frontend symbols, `src/` headers, or one firmware build's raw link addresses.

## Versioning Rule

UniFrog ABI versions follow semantic versioning:

- major: breaking ABI change. Old cores need a compatibility layer or rebuild.
- minor: additive ABI change. Existing fields and callbacks stay valid.
- patch: behavior fixes with the same ABI surface.

ABI-visible structs must be designed for compatibility:

- start with a `size` field and a version or magic field
- keep existing fields stable
- add new callbacks at the end
- keep `NULL` callback handling valid
- let callers check the table size before reading newer fields

The stable ABI surface is the versioned table from `unifrog/abi.h`. Other
UniFrog headers are source-level APIs unless they explicitly say they are
ABI-stable.

## External Cores

The long-term target is for cores to live outside the main firmware binary.
UniFrog will load a matching core binary from SD into a fixed safe memory slot,
validate its required ABI version, pass the ABI table to the core entry point,
and then run the returned libretro exports.

Static archives remain useful while the loader is being built, but they are not
the final distribution format for independently updateable cores.

## Static Build Contract

Until external loading is the default, UniFrog may link more than one libretro
core into the firmware. In that mode each additional core must avoid public
`retro_*` symbol collisions by using wrapper-provided preprocessor renames such
as `gpsp_retro_run`. Keep those renames in `cores/Makefile`, not in upstream
source branches.

The preferred order for porting an upstream core is:

1. Build the stock Makefile with SF2000 toolchain, endian, ABI, and static-link
   overrides from `cores/Makefile`.
2. Exclude host-only source files in the wrapper when HCRTOS newlib lacks the
   matching POSIX API.
3. Add compatibility in shared support libraries before editing a core.
4. Change upstream core source only for real runtime behavior changes, and keep
   each branch commit focused enough to review against future upstream updates.

## Long Load Progress

Libretro has no generic progress callback for long synchronous
`retro_load_game()` work. UniFrog therefore exposes an optional
`unifrog_core_load_progress(stage, current, total)` symbol for statically linked
cores that must read or prepare content for more than a few seconds.

Use this only from non-hot load paths such as ROM chunk reads, BIOS fallback,
memory-map setup, or emulator reset. Do not call it from `retro_run()`, dynarec
translation, audio callbacks, or per-frame code. The frontend uses it as a
watchdog heartbeat, a loading-screen update, and low-volume load progress logs,
so a core can remain close to upstream while still making load stalls
diagnosable.
