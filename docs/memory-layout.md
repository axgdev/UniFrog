# UniFrog Memory Layout

UniFrog uses two long-lived memory ownership classes: persistent runtime memory
and a reclaimable high-memory arena. The SF2000/GB300 board-specific split is
documented in `docs/sf2000-memory-model.md`.

Fixed runtime reservations such as JIT caches and DMA buffers are part of the
same ownership story. See `docs/link-layout-diagnostics.md` for the linker
invariant that keeps allocatable no-load sections covered by `_ebss` instead of
letting the HCRTOS heap overlap them.

## Persistent Runtime

The base firmware, HCRTOS, drivers, filesystem state, input/audio/video
services, JS2300 host bindings, and enough UI state to return to
the menu must stay alive while a core runs. This memory comes from `sysmem`.

External applications must not own this region directly. They can request
services through the versioned `unifrog/abi.h` table.

## Reclaimable High-Memory Arena

The high-memory arena is for one active high-level guest at a time, with small
top reservations allowed when a loader needs a temporary buffer:

- a libretro core image, BSS, and fixed work buffers
- or a JavaScript/script runtime when no game core is running
- or a transient `mmz0` pool while hardware video playback is running
- or scratch buffers owned by a loader step

On exit, UniFrog can wipe or reuse the whole arena. Nothing needed to redraw the
recovery UI, return to the loader, flush logs, or power-cycle cleanly should
live only in this arena.

The arena is reported through `unifrog_abi_application_memory_slot()` and
`unifrog_abi_memory_layout()`. Cores and UI bundles must query it instead of
assuming a fixed address.

On the current SF2000/GB300 DTS the arena is backed by
`/hcrtos/memory-mapping/appmem`. The default policy is:

- keep 32 MiB of `sysmem` for UniFrog/HCRTOS
- reserve only the retained recovery log and fastboot handoff at the top of RAM
- give the remaining middle RAM to one reclaimable arena

This currently exposes 94 MiB of contiguous arena space on the 128 MiB layout:
cached `0x82000000..0x87e00000`, physical
`0x02000000..0x07e00000`. Devices with less RAM can expose a smaller arena or
no arena; loaders must reject cores that do not fit.

## Media Memory

The SF2000 DTS declares a zero-size `mmz0` node so HCRTOS leaves MMZ id 0 free at
boot. Native video creates `mmz0` from the reclaimable arena immediately before
opening `/dev/viddec` and deletes it after playback closes. Hardware video still
gets physically contiguous decoded frame/display memory, but that memory is not
kept away from cores or scripts while no media session is active.

HCRTOS KSHM is separate from decoded frame memory. Because the DTS does not
define a named `kshm` MMZ pool, KSHM falls back to normal aligned heap
allocation for compressed packet rings while playback is active and releases
them when `VIDDEC_RLS`/`AUDDEC_RLS` runs. The media DTS advertises a 16 MiB
`viddec.kshm_size`, but native video defaults to an 8 MiB compressed ring so
high-resolution decode leaves more normal heap available for decoded surfaces
and SD readahead. Native audio currently opens `/dev/auddec` with the
stock-style `0xa0000` compressed ring used by HCRTOS cast examples. These rings
are dynamic playback allocations, not permanent DTS reservations.

UniFrog media playback uses streaming buffers instead of loading the whole file
into memory. The native FFmpeg custom AVIO buffer is intentionally a small
64 KiB read chunk, with a 16 KiB allocation fallback if memory is fragmented.
A separate post-probe read-ahead cache can allocate `16 x 512 KiB = 8 MiB` for
video, with smaller fallback windows if the decoder rings have consumed normal
heap, to reduce SD transactions during playback without letting MP4 probing
over-read tens of MiB before playback starts.

## Compatibility Rule

External binaries do not link against raw UniFrog addresses. UniFrog passes a
semantic-versioned ABI table. Before a stable release, breaking ABI changes are
allowed when they remove experimental interfaces or make the design clearer.
