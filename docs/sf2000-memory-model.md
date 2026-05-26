# SF2000 Memory Model

This document describes the SF2000 media-capable layout used by UniFrog. It is
separate from the generic memory notes because the video decoder has stricter
physical-memory requirements than libretro cores and optional scripts.

## Physical RAM Split

The SF2000 DTS declares 128 MiB of physical RAM. UniFrog splits that RAM into
owners with non-overlapping lifetimes:

- `sysmem`: fixed HCRTOS, drivers, UniFrog runtime, heap, and services.
- `appmem`: one reclaimable arena for the frontend, loaders, and native/core guests.
- `mmz0`: physically contiguous media surfaces for viddec/vidsink.
- `recovery_log_reserved`: retained crash and recovery log window.
- `fastboot_reserved`: fixed handoff area at the top of RAM.

Current 1080p media layout, from `board/hc15xx/common/dts/sf2000_min.dts`:

| Owner | Start | Size | End | Purpose |
| --- | ---: | ---: | ---: | --- |
| `sysmem` | `0x00000000` | 48.00 MiB | `0x03000000` | HCRTOS and UniFrog runtime |
| `appmem` | `0x03000000` | 43.98 MiB | `0x05bf9c60` | Reclaimable application arena |
| `mmz0` | `0x05bf9c60` | 34.93 MiB | `0x07ee7000` | Decoder/display frame memory |
| `recovery_log_reserved` | `0x07ee7000` | 1.00 MiB | `0x07fe7000` | Warm reboot diagnostics |
| `fastboot_reserved` | `0x07fe7000` | 100 KiB | `0x08000000` | Fastboot handoff |

`bootmem` is a subreservation inside `sysmem`, not a separate top-level owner.

## Media Heap Formula

`mmz0` is sized with the same shape as upstream hc15xx HCRTOS boards that support
1080p H.264 decode:

- maximum decoded width: `1920`
- maximum decoded height: `1088`
- reference frame slots: `7`
- extra frame slots: `3`
- dual-output video conversion buffer enabled
- 4 KiB MMZ blocks with allocator metadata overhead

The DTS keeps the formula instead of hardcoding a byte value so it remains
traceable to the HCRTOS media model. With the current constants it resolves to
`0x022ed3a0` bytes, or about 34.93 MiB.

## Why `mmz0` Is Reserved

The hardware decoder and display pipeline need physically contiguous surfaces.
Normal heap allocations can be aligned, but they are not a reliable substitute
for an MMZ pool because they can be fragmented and may not satisfy the media
subsystems that allocate frame buffers internally during `VIDDEC_INIT`.

The 0105 device logs showed the failure mode directly: with `mmz0` set to size
zero, every H.264 test failed `VIDDEC_INIT` with `errno=1` before any compressed
video packet was fed. The software-video fallback then repeatedly failed
`VIDSINK_DISPLAY_FRAME`, so the visible artifacts were downstream symptoms of a
missing media-memory owner.

A nonzero `mmz0` is therefore not a playback leak. It is a deterministic owner
for decoded frame/display memory. Individual viddec/vidsink allocations are made
inside that pool during playback and released when the decoder and sink close;
the pool itself stays reserved so the next playback session can get the same
contiguous hardware memory without racing the general heap.

## What Stays Dynamic

The compressed packet rings are a different kind of memory from decoded frame
surfaces.

UniFrog does not define a named `kshm` MMZ pool in the DTS. In HCRTOS, KSHM first
looks for that optional pool and otherwise falls back to normal aligned heap
allocation. That means the `viddec.kshm_size` and `auddec.kshm_size` packet rings
are created only while playback runs and are released on close.

This preserves the useful part of the old behavior: playback does not reserve a
large compressed-stream ring forever. The part that must be reserved is only the
media hardware's decoded-surface pool.

Current native playback sizes are 16 MiB for the high-resolution video
compressed ring, 8 MiB for streams at or below 640x360, and `0xa0000` bytes for
the audio compressed ring. File IO buffering is separate from these driver
rings. The low-resolution video ring is intentionally smaller because those
compressed packets do not need a 16 MiB ring, and the saved normal heap lets the
post-probe SD cache keep its full retained window set.

## Application Arena Contract

`appmem` is not hardcoded by callers. UniFrog reports it through
`unifrog_abi_application_memory_slot()` and `unifrog_abi_memory_layout()`, so
loaders and cores see the actual arena exposed by the DTS.

The current 1080p media layout leaves about 43.98 MiB of contiguous `appmem`.
That is smaller than the zero-MMZ experiment, but it is stable: the decoder
cannot steal from it, and app/core allocations cannot fragment the decoder's
surface memory.

## SD Read Buffering

Media file IO has two separate buffers:

- FFmpeg AVIO chunk: 64 KiB, with a 16 KiB fallback.
- Post-probe video read-ahead cache: normally `16 x 512 KiB = 8 MiB`, with
  smaller slot/count fallbacks if normal heap is tight after decoder init.
- Post-probe audio/file read-ahead cache: normally one 2 MiB window, with a
  512 KiB fallback.

The AVIO chunk stays small because MP4 probing and seeking can over-read whatever
chunk size the callback exposes. The larger read-ahead cache is enabled only
after `avformat_open_input()` and `avformat_find_stream_info()` finish, so
startup probing stays bounded while playback gets fewer, larger SD reads.

During media sessions UniFrog also suspends normal log writes around explicit SD
read windows. This avoids fighting the SD card with high-frequency read/write
traffic while audio/video playback is trying to keep real-time.

## Why This Is Not Frail

The layout is solid because each subsystem has exactly one owner for each memory
class:

- HCRTOS and UniFrog runtime state live in `sysmem`.
- Guests use the ABI-reported `appmem` arena and can be rejected if they do not fit.
- The media decoder gets a fixed contiguous `mmz0` pool sized for the maximum
  supported video plane.
- Crash/recovery and fastboot areas are fixed at the top of RAM and never overlap
  playback or guest memory.
- FFmpeg buffering changes SD transaction shape without changing physical memory
  ownership.

The important invariant is that decoded-frame memory is never borrowed from the
same heap that app/core code fragments over time. That removes the runtime race
that made the zero-MMZ layout flaky on real hardware.
