# Filesystem and MMC Notes

This document records confirmed facts about UniFrog logging, FatFs, and the
HCRTOS SD/MMC path. Keep it factual: separate confirmed observations from
working theories.

## Confirmed From Source

- `fsync(fd)` goes through the NuttX VFS shim:
  `fsync()` -> `file_fsync()` -> mountpoint `sync()`.
- For the HCRTOS FAT mount, mountpoint `sync()` is `fat_sync()` in
  `third_party/FatFs/source/fs_fat32.c`.
- `fat_sync()` calls FatFs `f_sync()`.
- FatFs `f_close()` also calls `f_sync()` for writable files. Therefore closing
  a file after writing is already a metadata/data commit point.
- In this SDK, FatFs `sync_fs()` calls `disk_ioctl(CTRL_SYNC)`, but
  `third_party/FatFs/source/diskio.c` treats `CTRL_SYNC` as a successful no-op.
  Explicit `fsync()` does not force an additional controller-level cache flush;
  it mostly forces FatFs dirty data, directory, FAT, and FSInfo updates.
- Normal UniFrog log flushes write the retained RAM log buffer to disk and close
  the file. Explicit fsync is disabled by default for normal, sync, reset, and
  recovery log writes.

## Confirmed From Device Logs

- With the former `wide16` boot profile, repeated Storage -> Write Probe runs completed without
  crashes in `logmuos0151`.
- Typical 512 KiB sequential write phase:
  - total around 208-225 ms;
  - write time around 166-183 ms;
  - close time around 4-5 ms.
- Typical 512 KiB append phase:
  - total around 205-234 ms;
  - write time around 168-195 ms;
  - close time around 4-5 ms.
- 48 small 512-byte create/write/close operations complete, but close/metadata
  pressure is visible:
  - total around 800 ms;
  - accumulated close time around 220-240 ms.
- Explicit fsync after a 512 KiB write completed in about 4-5 ms in the tested
  run, because `CTRL_SYNC` is a no-op and most of the work was already FatFs
  writeback.
- Log flushes in the tested run completed in about 24-31 ms with fsync disabled.

## Working Theory

- Not every write is equally risky. Large sequential writes are relatively
  stable. The default build now boots `wide25`; nearby lower-speed profiles
  remain available at runtime to locate the stability knee on weak cards or SD
  extenders.
- Many small files stress FAT directory/FAT metadata more than sequential data
  writes.
- The visible screen flicker during writes suggests electrical load, shared
  power rail, or signal-integrity sensitivity. Some past freezes may be power
  or SD signal integrity failures rather than deterministic filesystem logic
  errors.
- SD extenders can degrade signal integrity. UniFrog should still be resilient,
  but no single 4-bit default has proven universal yet. The current policy is
  to keep libretro-session sync logs in retained RAM, avoid SD appends during
  core-side loads, and use the storage tests to compare the nearby fixed
  profiles before deciding whether to move the default again.

## Current Logging Policy

- Default auto flush is 16 KiB. This is frequent enough to preserve recent logs
  after a hard power switch while avoiding the heavy pressure of flushing every
  line.
- `SELECT+Y` remains the manual "save logs now" shortcut. It writes and closes
  the current log data so the file is visible after power-off.
- Retained RAM logging remains the crash-recovery path. It is not a replacement
  for disk logs because hard power-off can happen at any moment.

## Open Questions

- Whether the low-level MMC block driver has an unbounded wait path during
  rare card-busy or signal-error conditions.
- Whether some SD cards need a lower-frequency or 1-bit fallback after write
  errors, not only after read/mount failures.
- Whether keeping a single append log file open and checkpointing metadata at
  controlled intervals improves reliability enough to justify the risk that a
  sudden power-off can lose the final file-size update.
