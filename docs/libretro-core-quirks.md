# Libretro Core Quirks and Diagnostics

This document captures recent SF2000-family issues that looked like core
breakage but were caused by host/device integration details. Keep fixes in
UniFrog where practical so upstream libretro cores stay easy to update.

## External core calls and `$gp`

External core modules run from the SD-loaded application memory slot. Their
small-data area is not the same as UniFrog's, so every call into a core must use
the module header `gp_addr`, and every callback back into UniFrog must restore
UniFrog's expected `$gp` before touching host globals.

Diagnostics that matter:

- Log the UniFrog build commit, SDK commit, cores commit, dirty bit, core module
  address range, and core `gp_addr` before `retro_init()`.
- When a core crashes after `retro_load_game()` or during dynarec setup, check
  whether the faulting PC is inside the external module range and whether the
  callback path restored `$gp`.
- Do not assume address-only fixes are enough. A valid load address can still
  crash if callbacks run with the wrong `$gp`.

## QuickNES unaligned reads

QuickNES hit an address error in PPU sprite rendering. The failing address was
inside the core's static PPU buffer, and the faulting instruction was a plain
32-bit load from an unaligned address.

The SF2000 branch change keeps the core behavior upstream-friendly by enabling
the existing packed `unaligned_uint32_t` path under `SF2000`, and aligning the
static video buffer. After the fix, the MIPS output uses unaligned-safe load
and store sequences instead of plain `lw`/`sw` at the hot callsite.

Diagnostic pattern:

- Use `addr2line` on the built core module for EPC/RA.
- Use `objdump -d` around EPC. If BADV is a plausible in-core data address and
  EPC is an `lw`/`sw`, look for upstream `NO_UNALIGNED_ACCESS` paths before
  adding SF2000-specific logic.

## Gearboy pixel format

Gearboy rejected loading because it requested `RETRO_PIXEL_FORMAT_XRGB8888`.
The host previously accepted only RGB565, so `retro_load_game()` failed and the
frontend returned to the menu.

The host now accepts XRGB8888 and converts frames to RGB565 before passing them
to the presenter. This keeps the core unmodified and makes the behavior useful
for other cores that prefer 32-bit video.

Diagnostic pattern:

- Watch `RETRO_ENVIRONMENT_SET_PIXEL_FORMAT` logs during load.
- If `retro_load_game()` fails immediately after a pixel format request, confirm
  the host accepts or deliberately rejects that format.

## Local and wireless input

The SF2000 local controls and wireless RF path share pin configuration state.
Wireless polling must never become authoritative for player 1 or contaminate
the debounced local state.

The input layer now keeps local buttons and wireless buttons separate, restores
the local bus before scanning the physical keypad, polls RF afterward, and then
builds the public combined mask. Libretro port 0 receives local controls plus
wireless state so physical controls always remain player 1; port 1 exposes the
second wireless controller.

Diagnostic pattern:

- Use the limited `unifrog input sources local=... wireless=... combined=...`
  logs to confirm local input is still present when wireless is active.
- If physical controls stop working after returning from a core, check that the
  core cleanup and JS relaunch paths call `unifrog_input_clear()`.

## Save RAM

In-game saves are libretro memory blocks and should use
`retro_get_memory_data()` plus `retro_get_memory_size()`.

UniFrog auto-loads and auto-saves `RETRO_MEMORY_SAVE_RAM` and
`RETRO_MEMORY_RTC` under `/media/mmcblk0/unifrog/saves`. The runtime loads
them right after `retro_load_game()`, periodically hashes the exposed memory
while the core runs, writes changed save memory in the background, and writes
again on clean core exit.

The JavaScript quick menu uses native bindings for libretro save states.
State slots are stored next to battery saves as `.state0` through `.state9`.
Keep pause-menu UI in JavaScript; native code should expose only the fast
operations needed by that UI.

## Compressed ROM loading

Large compressed ROMs must not require both the full compressed file and the
full decompressed ROM to be allocated at the same time. Free memory is less
important than a large contiguous aligned block for the core-visible ROM.

UniFrog therefore uses stream-to-final-buffer paths for wrapped compression:

- LZ4: measure output with a streaming pass, allocate the aligned ROM buffer,
  then reopen and stream-decompress into that buffer.
- Zstd: use the frame content size when available, otherwise measure with a
  streaming pass, then stream-decompress into the final buffer.
- Zip: read only the end/central directory metadata, allocate the selected
  entry's final buffer, then copy or inflate from the archive stream.

The log should report `mode=stream_ram` when this path is used. Seeing
`compressed_cache_read` for a GBA-sized ROM means the in-memory path failed and
the allocator diagnostics immediately before it matter more than FAT seek
behavior.

Do not add a persistent ZIP-entry cache for byte-loading cores unless device
logs prove it wins on the target SD path. A first-launch cache write adds a full
ROM-sized SD write, and reading a cached GBA-sized ROM can be slower than
streaming inflate on this device.

## Exception screen

The panic and exception screens poll raw local START plus wireless START and
then arm the hardware watchdog reset path. This makes manual reset testing
possible without power cycling even when normal input debounce or `reset()` is
not usable from the crash screen.

The developer test should draw the exception screen directly. Avoid using an
intentional bad load/store as the UI diagnostic: on this runtime a synthetic
fault can stop before the normal panic drawing path, which looks like a frozen
device rather than a useful test. The Developer menu has a separate CPU
exception action using a MIPS `break` instruction only for validating the
low-level exception vector.

## Media and Audio Gates

The SF2000 speaker gate must stay closed unless real audio is being produced.
Videos with no audio track should keep the gate closed, otherwise the hardware
background noise is audible even though the media file is silent. Emulator
fast-forward also closes the gate while frames run uncapped; normal audio
resumes when fast-forward is turned off and non-silent samples arrive again.
For streamed media, metadata can be late. Treat a zero audio-track count as
silent, retry unknown metadata, and only fall back to audio output for files
that are explicitly audio-only by extension. Unknown audio on video/image files
should keep the speaker gate closed to avoid static on silent media.

## SF2000 QPSX and PMP cores

The qpsx and PMP/video cores are SF2000-oriented projects rather than normal
upstream libretro cores. Treat them as external modules and keep their
adaptation commits additive where possible.

QPSX contains MIPS GPU inner-loop assembly that assumes instructions beyond the
baseline `-march=mips32` target. That code is opt-in through
`QPSX_ENABLE_MIPS32R2`; the default UniFrog build uses the C fallbacks so it
does not emit instructions the SF2000 runtime may fault on. If qpsx crashes
inside GPU drawing, first confirm whether that flag was enabled before chasing
module loader or ABI state.

Both projects also carried multicore-style filesystem assumptions such as
`/mnt/sda1`. The UniFrog module support layer maps those paths to the current
SD layout and exposes ABI-backed `stat`, `mkdir`, and directory iteration. Keep
that compatibility in the module support layer rather than editing every
project-local file operation.

QPSX also carried a multicore-specific fixed PSX RAM address experiment at
`0x85000000`. Under UniFrog that address can overlap the app/module arena, so
the UniFrog branch disables the fixed-address path and lets qpsx allocate PSX
RAM normally. If qpsx hangs at `CORE LOAD`, the bounded `QPSX_LOAD:` messages
inside `retro_load_game()` identify the last completed load stage before the
core starts running frames.

QPSX's own startup menu is also multicore-oriented. UniFrog disables automatic
entry into that menu and relies on the UniFrog JavaScript launch flow instead.
Holding START during the first few qpsx frames still opens qpsx's internal menu
as a recovery path. If qpsx stalls after `step=run_loop`, the host run watchdog
should produce a bounded `UNIFROG CORE HANG` screen with the last frame marker.
