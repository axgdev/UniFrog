# SF2000 MMC Driver Notes

This document records the current storage-driver findings for the SF2000/GB300
HCRTOS build. It is intentionally factual: separate confirmed behavior from
planned replacement work.

## Current Driver Stack

The build links the vendor HCRTOS MMC core and HC15xx host driver:

- `unifrog-hcrtos-sdk/lib/vendor/libmmc.a`
- `unifrog-hcrtos-sdk/lib/vendor/libmmchosthc15.a`

The host driver is binary-only in the current SDK. UniFrog should avoid early
boot hooks into this path unless the hook is proven safe on hardware, because a
failure before the frontend storage code starts can leave no persistent log.

## Reverse-Engineered Findings

These observations came from `output/sf2000.out`, the vendor libraries, `rizin`,
`rz-ghidra`, and `mipsel-mti-elf-objdump`.

- `hc_mmc_probe` allocates a normal `struct mmc_host` with `mmc_alloc_host`.
- It advertises these request limits before `mmc_add_host`:
  - `max_seg_size = 4096`
  - `max_segs = 256`
  - `max_req_size = 1048576`
  - `max_blk_size = 512`
  - `max_blk_count = 2048`
- `mmc_init_queue` consumes those host limits to configure the block queue and
  to size the queue scatterlist tables.
- The HC15xx DMA setup path calls `dma_map_sg`, but programs the controller with
  only one DMA address and the full transfer length.
- A rizin/rz-ghidra pass on `output/sf2000.out` confirmed this at the register
  level: `hc_mmc_host_hw_set_dma` writes one DMA address register and one byte
  count register per direction, while `hc_mmc_prepare_data` still calls
  `dma_map_sg` for the full scatterlist and then reads only the first
  scatterlist entry for the DMA address.
- The completion path increments `data->bytes_xfered` by only the first
  scatterlist entry length.
- Logs from `../latest_log/v050/0242` showed this signature during boot:
  `WRITE_MULTIPLE_BLOCK`, `blocks=29`, `blksz=512`, `bytes_xfered=4096`.
- Logs from `../latest_log/v050/0245` showed the same issue under diagnostics:
  `READ_MULTIPLE_BLOCK` and `WRITE_MULTIPLE_BLOCK`, `blocks=64`,
  `blksz=512`, `sg_len=8`, `bytes_xfered=4096`, `expected=32768`.

That combination strongly suggests the vendor host driver exposes a
multi-segment DMA contract that its hardware programming path does not actually
implement.

## Observed Register Clues

The following HC15xx register uses are inferred from the binary driver. Treat
them as working notes until validated with a source driver and hardware tests.

| Offset | Observed use |
| --- | --- |
| `0x03` | clock divider low byte |
| `0x08` | block size |
| `0x0a` | block count low register |
| `0x0b` | bus width |
| `0x20` | DMA address for one transfer direction |
| `0x24` | DMA address for the other transfer direction |
| `0x28` | DMA byte count for one transfer direction |
| `0x2c` | DMA byte count for the other transfer direction |
| `0x30` | DMA start/control, value `3` observed |
| `0x34` | clock divider high byte |
| `0x36` | block count high register |
| `0x50` | timing/speed mode |

`hc_mmc_host_hw_set_clock` was also decompiled with `rz-ghidra`. The divider
is a 16-bit value split between registers `0x03` and `0x34`; for divider values
above one the effective clock is approximately `source_hz / ((divider - 1) *
2)`. Register dumps now include both the raw divider and that decoded clock so
hardware reports can distinguish "requested" clock from the clock the host
actually programmed.

## Safer Default Policy

Do not mutate `mmc_add_host` in the default build. A test build that wrapped
`mmc_add_host` to clamp the host limits before queue creation failed before
UniFrog could persist logs on hardware. That is useful evidence, but it is not a
safe production fix.

The default build keeps the request-completion wrapper idle unless a storage
diagnostic explicitly enables it at runtime. Normal boot and normal frontend use
call through to the vendor completion function without reading MMC request
fields.

Runtime MMC diagnostics are enabled by the JavaScript storage actions. While
enabled they record a small in-memory ring of recent requests, including block
count, reported bytes transferred, expected bytes, scatterlist count, and first
scatterlist entry details.

The default build now also keeps a source-owned SG repair at the
`dma_map_sg`/`dma_unmap_sg` boundary for the SD host after it has been
discovered. UniFrog copies supported multi-SG SD requests into one page-aligned
32 KiB buffer, maps that single SG entry, unmaps that same single entry, then
restores the original scatterlist. This avoids the vendor driver's unsupported
multi-SG DMA contract without taking over card enumeration, block-device
registration, or the filesystem stack. The repair is restricted to the cached
SD host parent device; other DMA users still call through unchanged unless a
diagnostic action is active.

This is still a bridge, not the final storage driver. It is small enough to run
in the default build and directly addresses the confirmed one-address DMA
limitation while the larger source-host replacement remains experimental.

Failed runtime profile switches now roll back to the saved boot profile before
returning failure. Earlier storage test code restored after a failure, but that
made the API fragile: any caller that forgot the second step could leave the
card unmounted, in low-speed enumeration, or in a partially switched high-speed
state. The rollback path uses the same direct boot-profile reclock/recovery
logic as the explicit restore path and reports both the failed profile details
and the rollback result.

Logs from `../latest_log/v050/0256` show the remaining high-speed failure is
not the multi-SG DMA bug. Wide25 and the repaired bounce path completed normal
reads and writes without fallback, conflict, or abort counters. `wide50` reached
50 MHz, then a one-block read and repeated CMD13 status requests failed with
`-138`; the host then dropped into the 400 kHz enumeration state where CMD52
and CMD5 timeouts are expected probe noise. A direct boot-profile reclock
recovered the card in roughly 690 ms. This points at the pure SD-HS switch/link
path, not filesystem writes and not scatterlist handling.

Logs from `../latest_log/v050/0257` narrowed the next fault. The quick mode
sweep passed from `safe` through `wide25`, and the benchmark still measured
about 6.6 MiB/s reads and 6.3 MiB/s synced writes at the boot profile. `hs1`
and `wide50` both failed around 50 MHz with the same pattern: CMD18/CMD17
reported `-138`, repeated CMD13 status requests failed, the card pointer became
NULL, and the host fell back into low-speed enumeration. A later `safe` stress
run failed at 25 MHz only after sustained read/write traffic, which means the
high-speed profiles are fragile but contact/power loss must still be recovered
cleanly even on the conservative profile.

One important detail in the `hs1` failure was `bounce active=1` in the debug
dump after the card disappeared. Recovery paths now abort any stale bounce
mapping after unmount or after storage is known not ready, before reclocking or
forcing re-detect. The storage stress script also closes its files and runs the
same I/O-fault recovery path before writing the failure report, instead of
issuing extra report writes while the card is still detached.

Logs from `../latest_log/v050/0258` are mostly positive for the conservative
profiles. The quick benchmark stayed at roughly 6.6 MiB/s read and 6.2 MiB/s
synced write on `wide25`. The quick sweep passed every profile from `safe`
through `wide25`. The recovered crashlog shows `stress-all-modes.js`
completing `safe`, `wide1`, `wide2`, and `wide4`, then running `wide8` cleanly
for about 50 seconds before the manual reboot. There were no request errors,
short transfers, fallback paths, conflicts, or bounce aborts in those completed
stress runs.

The final `storage-stress-result.txt` in that capture still showed an older
`hs1` failure even though the recovered crashlog was in the middle of `wide8`.
That is a diagnostic persistence bug rather than an MMC request failure: the
report writer used `fclose` and `rename`, but did not force the checkpoint file
to storage. Storage reports now use direct writes with `fsync` before the rename
and a best-effort `fsync` of the renamed file, so a manual power cycle should
preserve the most recent storage-test stage more reliably.

Logs from `../latest_log/v050/0259` confirm that report checkpoint fix. The
quick benchmark again reached about 6.6 MiB/s reads and 6.3 MiB/s synced writes
on the default `wide25` boot profile. The quick sweep passed every profile from
`safe` through `wide25`, and the recovered crashlog shows the stress sweep
completing the lower profiles before continuing through `wide8` without request
errors. The current synced stress report then captured the real next failure:
`hs1` failed after about 6.4 seconds with a read error and recovered back to the
boot profile in about 480 ms.

That keeps the strongest current hypothesis on the high-speed timing path.
`hs1` is still 1-bit, so it separates the failure from 4-bit bus width and
points at the SD high-speed switch, sample/drive timing, or clock-transition
state. Stress I/O failures now force an MMC diagnostic checkpoint before the
debug dump and another checkpoint after successful recovery, then flush the log
after the synced report is written. The next failed high-speed run should retain
the request-level trace that was lost from the large recovered ring in 0259.

Logs from `../latest_log/v050/0260` show the conservative path is still good:
the quick benchmark and quick mode sweep passed through `wide25` again, with
the same roughly 6.6 MiB/s read rate. The latest synced stress report captured
a later risky-profile failure in `wide`, with zero transferred bytes and a
successful return to `wide25` in about 330 ms. The retained ring still ended
inside the earlier `wide8` stress window because the failure-path log flush was
called while the stress action still had log deferral and disk logging
suspended. That made the report current but left the request trace deferred.
The failure path now temporarily unsuspends storage logging after successful
recovery and uses `unifrog_log_flush_force`, then re-suspends logging before
returning to the still-running stress action.

Another symbol pass over `output/sf2000.out` confirmed the source-host
replacement boundary. `libmmchosthc15.a` only exports `hc_mmc_clock_gate`,
`hc_mmc_ip_reset`, and the `hc_mmc_host_hw_ops` table; the rest of the host
driver is local text. It depends on the reusable MMC core entry points in
`libmmc.a`, including `mmc_alloc_host`, `mmc_add_host`, `mmc_remove_host`, and
`mmc_request_done`. That keeps the source-owned target narrow: replace the
HC15xx host probe/request/IRQ/DMA code while still using the vendor MMC core,
block queue, card enumeration, and filesystem integration.

## Source-Owned Driver Strategy

The practical replacement target is `libmmchosthc15.a`, not the whole MMC stack.
Reusing the existing `libmmc.a` lets UniFrog keep the card enumeration, block
device, and filesystem integration while replacing the fragile HC15xx host ops.

The first source driver should:

1. Own the HC15xx host register access and host ops in UniFrog source.
2. Reuse stable HCRTOS services such as `mmc_alloc_host`, `mmc_add_host`,
   `mmc_request_done`, IRQ registration, DMA allocation, timers, and platform
   device matching.
3. Reuse exported vendor helpers such as `hc_mmc_ip_reset` and
   `hc_mmc_clock_gate` only when doing so keeps the source implementation
   smaller and observable.
4. Advertise conservative queue limits until DMA is proven:
   `max_seg_size=4096`, `max_segs=1`, `max_req_size=4096`,
   `max_blk_size=512`, `max_blk_count=8`.
5. Implement PIO or single-buffer DMA first. Multi-segment DMA should stay
   disabled unless the controller is proven to support a descriptor list.
6. Log request opcode, block count, block size, DMA address, byte count,
   interrupt status, command error, data error, and timeout outcome through a
   bounded ring that can be dumped later by the frontend.
7. Keep recovery explicit: on timeout or CRC/error status, stop the command,
   reset the host IP, restore clock/bus width/timing, and ask the MMC core to
   re-detect the card.

## Why Not Copy an Open Linux Driver Directly

Linux MMC host drivers are the best reference for structure, recovery policy,
and queue limits, but the HC15xx controller programming model here is
vendor-specific and the HCRTOS APIs are not Linux APIs. A direct copy would
likely add porting complexity without proving the unknown register contract.

The useful open-source pattern is architectural:

- keep the `mmc_host_ops` boundary small;
- advertise only what the host really supports;
- complete every request exactly once;
- fail requests on real hardware errors rather than silently correcting them;
- reset and re-detect after card removal or host timeout.

## Suggested Rollout

1. Restore the default build to the vendor host driver with passive diagnostics.
2. Add a separate source-host build path for hardware experiments.
3. Bring up card detect, reset, low-speed 1-bit command flow, and single-block
   reads first.
4. Add single-buffer DMA reads and writes.
5. Run storage scripts against fixed modes from safest to riskiest.
6. Make the source host the default only after it boots repeatedly, survives
   SD disconnect/reconnect tests, and completes write/read/verify probes without
   screen tearing or wedging.
