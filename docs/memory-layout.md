# UniFrog Memory Layout

UniFrog uses two memory ownership classes with different lifetimes: persistent
runtime memory and a reclaimable application arena. Media playback now uses the
same normal allocation path as the rest of the runtime; there is no separately
reserved MMZ pool in the default layout.

Fixed runtime reservations such as JIT caches and DMA buffers are part of the
same ownership story. See `docs/link-layout-diagnostics.md` for the linker
invariant that keeps allocatable no-load sections covered by `_ebss` instead of
letting the HCRTOS heap overlap them.

## Persistent Runtime

The base firmware, HCRTOS, drivers, filesystem state, input/audio/video
services, log buffers, mquickjs host bindings, and enough UI state to return to
the menu must stay alive while a core runs. This memory comes from `sysmem`.

External applications must not own this region directly. They can request
services through the stable `unifrog/abi.h` table.

## Reclaimable Application Arena

The application arena is for one active high-level guest at a time:

- a libretro core image, BSS, and fixed work buffers
- or a JavaScript/script runtime when no game core is running
- or the JavaScript quick menu while a game core is suspended
- or scratch buffers owned by a loader step

On exit, UniFrog can wipe or reuse the whole arena. Nothing needed to redraw the
recovery UI, return to the loader, flush logs, or power-cycle cleanly should
live only in this arena.

The arena is reported through `unifrog_abi_application_memory_slot()` and
`unifrog_abi_memory_layout()`. Cores and UI bundles must query it instead of
assuming a fixed address.

On the current SF2000/GB300 DTS the arena is backed by
`/hcrtos/memory-mapping/appmem`. The default policy is:

- keep 48 MiB of `sysmem` for UniFrog/HCRTOS
- give the rest of RAM to one reclaimable application arena
- keep `mmz0` at size zero

This currently targets about 80 MiB of contiguous application arena on the
128 MiB layout. Devices with less RAM can expose a smaller arena or no arena;
loaders must reject cores that do not fit.

## Media Memory

MMZ is intentionally size zero in the default UniFrog layout. Vendor media code
still calls MMZ allocation APIs, so the SDK routes zero-sized MMZ allocations to
normal sysmem instead of requiring a reserved driver pool. This keeps MP4
playback working while leaving the high-memory arena available for applications.

UniFrog media playback should use an application-owned streaming buffer instead
of loading the whole file into memory. The current default stream cache is 1 MiB
inside the app arena; it can be tuned lower after device testing confirms SD
refill latency stays hidden.

## Compatibility Rule

External binaries do not link against raw UniFrog addresses. UniFrog passes a
semantic-versioned ABI table. If the table grows, new callbacks are appended and
old fields keep their meaning after v1.0. While the ABI is still v0.x, breaking
changes are allowed when they remove experimental interfaces or make the design
cleaner.
