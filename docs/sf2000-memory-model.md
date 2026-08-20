# SF2000 Memory Model

This document describes the SF2000/GB300 memory layout used by UniFrog. The goal
is to keep only the boot/runtime foundation fixed and make the rest of RAM
available to whichever high-memory subsystem is currently active.

## Physical RAM Split

The board DTS declares 128 MiB of physical RAM. UniFrog now gives that RAM these
owners:

| Owner | Start | Size | End | Purpose |
| --- | ---: | ---: | ---: | --- |
| `sysmem` | `0x00000000` | 32.00 MiB | `0x02000000` | HCRTOS, drivers, filesystem, UI, logs, recovery paths |
| `appmem` | `0x02000000` | 94.00 MiB | `0x07e00000` | Reclaimable arena for cores, JS2300, loaders, and transient media MMZ |
| `mmz0` | `0x07e00000` | 0 bytes | `0x07e00000` | Placeholder node; id 0 is created dynamically for video |
| `recovery_log_reserved` | `0x07e00000` | 1.875 MiB | `0x07fe0000` | Warm reboot diagnostics |
| `fastboot_reserved` | `0x07fe0000` | 128 KiB | `0x08000000` | Fastboot handoff |

Cached addresses are the physical addresses plus the KSEG0 base:
`sysmem` is `0x80000000..0x82000000`, `appmem` is
`0x82000000..0x87e00000`, retained logs are
`0x87e00000..0x87fe0000`, and fastboot is
`0x87fe0000..0x88000000`. `bootmem` is a subreservation inside `sysmem`, not a
separate top-level owner.

## Reclaimable Arena

`appmem` is reported through `unifrog_abi_application_memory_slot()` and
`unifrog_abi_memory_layout()`. Callers must query the ABI instead of assuming a
fixed address. Core modules are packaged for the current `0x82000000` slot and
must be rebuilt when the firmware memory map moves.

The arena has mutually exclusive owners in normal use:

- libretro core modules and optional ROM content reservations
- JS2300 script heaps
- native media `mmz0` while hardware video is active

`unifrog_abi_application_memory_reserve_top()` can temporarily reserve the top of
the arena. JS2300 uses that for its heap, and the ABI then reports only the
remaining lower part to other callers. This keeps accidental overlap out of the
common paths without introducing a complex allocator.

## Dynamic Media MMZ

The hardware decoder and display pipeline still require physically contiguous
decoded frame/display memory. Instead of reserving `mmz0` forever, UniFrog leaves
the DTS `mmz0` size at zero and creates MMZ id 0 from `appmem` when native video
starts:

1. Query the current reclaimable arena.
2. Align it to a 4 KiB boundary.
3. Call HCRTOS `mmz_create()` with the physical base.
4. Open and run `/dev/viddec`/`/dev/vidsink`.
5. Close playback and call `mmz_delete(0)`.

The DTS keeps the upstream hc15xx 1080p media sizing properties for the media
drivers, but they are not a fixed boot reservation. Device logs should show
`unifrog media mmz lease` before native video opens and
`unifrog media mmz release` after it closes.

HCRTOS KSHM remains separate from decoded frame memory. Because the DTS does not
define a named `kshm` MMZ pool, compressed `viddec`/`auddec` packet rings still
fall back to normal aligned heap allocations while playback is active.

## Sysmem Consumers

The packed firmware size undercounts fixed runtime memory. The linked image
includes BSS and no-load sections through `_ebss`, and HCRTOS also needs heap
space above that for services that must survive while guests run. The main
fixed users are:

- firmware text/data/BSS/no-load sections
- HCRTOS heap, threads, filesystems, and driver state
- LVGL/theme assets and the UI needed to return from a guest
- input, audio, display, storage, and recovery services
- compressed media packet rings, SD read-ahead, and FFmpeg/parser allocations
  while media playback is active
- retained logging and temporary diagnostic buffers

The earlier 32 MiB sysmem experiment in `f7e3221` made the menu sluggish
because the framebuffer DTS reserved about 10 MiB of extra system heap at boot.
The current layout keeps sysmem at 32 MiB after reducing the framebuffer spare
allocation to one 320x240 RGB565 layer and keeping large JS/core/media heaps in
`appmem`.

## Invariants

- `sysmem` is the only fixed general-purpose runtime heap.
- Crash/recovery and fastboot areas stay fixed at the top of RAM.
- High-memory subsystems must release the arena when they exit.
- Media can borrow the arena, but media state needed for recovery must stay in
  `sysmem`.
- Core modules are not relocatable yet, so the packaged cores must match the
  firmware's appmem base.
