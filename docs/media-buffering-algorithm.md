# Media Buffering Algorithm

This document describes the native FFmpeg plus HCRTOS decoder buffering path in
`src/unifrog_media.c`. The goal is smooth SD-card playback without bringing
`hcplayer` back as a dependency.

## Problem Model

The SF2000 SD card path is the scarce resource. CPU should mostly demux and copy
compressed packets while `/dev/auddec` and `/dev/viddec` decode in hardware.
The file reader must therefore avoid repeated physical SD reads for bytes that
were already loaded.

The 0116 1080p logs showed the failure mode clearly:

- `blink182_1080p_h264.mp4` is `56,704,453` bytes.
- The single 512 KiB readahead window caused `1,248,987,478` physical SD bytes.
- That is a `22x` disk-read multiplier, with `2374` readahead fills and `5398`
  demux seeks.
- FFmpeg was alternating packet offsets such as video near `8.05 MiB` and audio
  near `8.96 MiB`, then later video near `35.9 MiB` and audio near `38.6 MiB`.
  A single 512 KiB window could not retain both active regions, so every seek
  discarded useful data and reread the other side.

This is not primarily an audio decoder problem or a raw bitrate problem. It is a
cache locality problem caused by one AVIO stream reading a seek-heavy MP4 packet
order from a slow block device.

## Current Algorithm

Native media uses a small LRU read-window cache in front of FFmpeg's AVIO
callbacks.

- `MEDIA_VIDEO_READAHEAD_SIZE` is the physical read size for one video window.
- `MEDIA_VIDEO_READAHEAD_SLOTS` is the number of recent windows retained.
- The default video cache is `4 x 1 MiB = 4 MiB` total.
- The default audio/file cache remains `1 x 2 MiB = 2 MiB` total.
- A seek into an existing window is a cache hit and does not call `lseek()`.
- A seek outside all windows only updates the logical AVIO position. The real
  `lseek()` is deferred until a following read miss needs disk data.
- A read miss fills one window from the current logical position, not the whole
  file and not an unbounded prebuffer.
- When all windows are occupied, the least recently used window is evicted.

This keeps the algorithm simple: no threads, no speculative parser, no second
copy of the demuxer, and no dependency on private `hcplayer` state. The only
policy is retaining several recent file regions instead of assuming the demuxer
will read monotonically.

## Why The Defaults Are Shaped This Way

A single large window reduces seek thrash but can block too long on bad cards. A
single small window keeps individual reads short but thrashes on MP4 files whose
audio and video packet offsets are far apart.

The default uses several medium windows:

- `1 MiB` physical reads are large enough to avoid tiny SD transactions.
- `4` retained windows allow separate active audio/video MP4 regions plus nearby
  reordered video packets to stay hot.
- `4 MiB` total video cache is larger than the audio cache, because video
  containers have higher byte density per second and more seek pressure.
- Hardware decoder ahead is still bounded so a long SD stall cannot be followed
  by a many-second packet dump into `/dev/auddec` or `/dev/viddec`.

The linked HCRTOS examples and `ffplayer` headers also point to decoder-side
buffer thresholds. UniFrog now uses `500/3000 ms` audio/video decoder buffering
thresholds by default, matching the stock player style more closely than the
previous `200/1000 ms` local values.

## Tuning Knobs

Set these in `config.mk` or on the `make` command line:

- `MEDIA_VIDEO_READAHEAD_SIZE`: bytes per physical video read window.
- `MEDIA_VIDEO_READAHEAD_SLOTS`: number of retained video windows, capped in
  code at `8`.
- `MEDIA_FILE_READAHEAD_SIZE`: bytes per audio/file read window.
- `MEDIA_FILE_READAHEAD_SLOTS`: number of retained audio/file windows.
- `MEDIA_VIDEO_MAX_HW_AHEAD_MS`: maximum video packets queued ahead of the
  hardware video clock.
- `MEDIA_AUDIO_MAX_HW_AHEAD_MS`: maximum audio packets queued ahead of the
  hardware audio clock.
- `MEDIA_VIDEO_BUFFERING_START_MS` and `MEDIA_VIDEO_BUFFERING_END_MS`: decoder
  buffering thresholds passed to `/dev/viddec`.
- `MEDIA_AUDIO_BUFFERING_START_MS` and `MEDIA_AUDIO_BUFFERING_END_MS`: decoder
  buffering thresholds passed to `/dev/auddec`.
- `MEDIA_FILE_SLOW_READ_LOG_MS`: threshold for logging slow physical reads.

Practical tuning rules:

- If `disk_bytes / file` is high and `evict` is high, increase slots before
  increasing window size.
- If `slow_read max_disk_ms` is too high, reduce window size and increase slots
  to keep the same total cache with shorter individual SD reads.
- If `slow_read` is rare but `ahead_a` or `ahead_v` reaches zero during monitor
  lines, increase hardware ahead or decoder buffering thresholds cautiously.
- If `/dev/viddec` or `/dev/auddec` write retries become frequent, decrease the
  hardware ahead caps; that indicates decoder rings are being overfilled.

## Device Log Contract

For a default build, native video should log something like:

```text
unifrog media buffered_io readahead enabled tag=native_video mode=video slot=1048576 total=4194304 slots=4 ...
```

The close line now includes cache-efficiency counters:

```text
unifrog media buffered_io close ... readahead=4194304 slot=1048576 slots=4 ... misses=... fills=... evict=... seek_hits=... disk_bytes=...
```

For 1080p, the important target is that `disk_bytes` should be much closer to
the file size than the 0116 `22x` multiplier. Some overhead is expected because
FFmpeg still probes and seeks, but repeated multi-hundred-megabyte rereads mean
the cache is still missing the active offset set.

Slow physical reads are logged as:

```text
unifrog media buffered_io slow_read tag=native_video ms=... pos=... want=... got=... disk_reads=... seeks=...
```

Hardware feed caps are logged as:

```text
unifrog media hw_ahead wait kind=... ahead=... max=... clock=... feed=...
unifrog media hw_ahead done kind=... clock=... feed=... ahead=...
```

Occasional `hw_ahead` waits are normal. Constant waits plus audio/video stutter
mean the feeder is oscillating between SD stalls and decoder-ring catch-up.
