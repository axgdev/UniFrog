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

The 0117 logs validated the direction but exposed the next limit:

- The 1080p test dropped to about `76.9 MiB` of physical reads for the same
  `56.7 MiB` file, so retaining multiple windows fixed the worst reread storm.
- It still logged `68` evictions and one `1010 ms` read for a 1 MiB window.
- Lower resolutions were close to file-size physical reads, but a single slow
  window read was still audible because it happened synchronously during
  playback.

The current defaults therefore spend more RAM on retained windows while making
each physical read smaller.

The 0119 logs added a low-resolution-specific finding:

- 240p and 360p had clean SD counters, but hardware audio `ahead_a` dropped to
  `0-32 ms` at monitor points, which is too close to audible underrun.
- All resolutions still requested a 16 MiB video KSHM ring, so after decoder
  init the opportunistic video read cache fell back to `16 x 256 KiB = 4 MiB`.
- 16 MiB is useful for 720p/1080p, but it is oversized for 426x240 and 640x360
  compressed packets. HCRTOS example decoders use 8 MiB KSHM successfully.

## Current Algorithm

Native media uses a small LRU read-window cache in front of FFmpeg's AVIO
callbacks.

- `MEDIA_VIDEO_READAHEAD_SIZE` is the physical read size for one video window.
- `MEDIA_VIDEO_READAHEAD_SLOTS` is the number of recent windows retained.
- The default video cache is `16 x 512 KiB = 8 MiB` total.
- The default audio/file cache remains `1 x 2 MiB = 2 MiB` total.
- A seek into an existing window is a cache hit and does not call `lseek()`.
- A seek outside all windows only updates the logical AVIO position. The real
  `lseek()` is deferred until a following read miss needs disk data.
- A read miss fills one window from the current logical position, not the whole
  file and not an unbounded prebuffer.
- When all windows are occupied, the least recently used window is evicted.
- Native video allocates and pre-fills its cache only after `/dev/auddec` and
  `/dev/viddec` have reserved their KSHM buffers. This keeps the cache
  opportunistic: it can shrink or fail without preventing hardware decode.
- For video streams at or below 640x360, native video asks `/dev/viddec` for an
  8 MiB compressed KSHM ring by default instead of the 16 MiB high-resolution
  ring. This leaves normal heap available for the full 8 MiB SD cache without
  changing the fixed `mmz0` decoded-surface pool.
- Before packet feeding starts, native video pre-fills a bounded startup
  cushion. The default target is 5 seconds of file bitrate, clamped between
  512 KiB and 2 MiB. This moves the first SD stall into the loading screen
  rather than the first seconds of playback.
- `MEDIA_VIDEO_PRELOAD_MAX_BYTES` can enable whole-file preload for small
  videos. It defaults to `0` because full preload trades stutter resistance for
  a longer startup wait. If whole-file allocation fails after the decoders have
  reserved memory, playback falls back to the normal window cache.

This keeps the algorithm simple: no threads, no speculative parser, no second
copy of the demuxer, and no dependency on private `hcplayer` state. The only
policy is retaining several recent file regions instead of assuming the demuxer
will read monotonically.

## Why The Defaults Are Shaped This Way

A single large window reduces seek thrash but can block too long on bad cards. A
single small window keeps individual reads short but thrashes on MP4 files whose
audio and video packet offsets are far apart.

The default uses several medium windows:

- `512 KiB` physical reads are large enough to avoid tiny SD transactions while
  reducing worst-case blocking time on flaky cards compared with 1 MiB reads.
- `16` retained windows allow separate active audio/video MP4 regions plus
  nearby reordered video packets to stay hot.
- `8 MiB` total video cache is larger than the audio cache, because video
  containers have higher byte density per second and more seek pressure.
- Hardware decoder ahead is still bounded so a long SD stall cannot be followed
  by a many-second packet dump into `/dev/auddec` or `/dev/viddec`.
- Low-resolution video gets a slightly deeper audio feed lead by default
  (`MEDIA_VIDEO_LOWRES_AUDIO_FEED_LEAD_MS=1500`). These streams have the same
  packet cadence as higher resolutions but much less SD work per packet, so the
  demux loop can run audio too close to the hardware clock unless the audio
  side gets extra cushion.

The linked HCRTOS examples and `ffplayer` headers also point to decoder-side
buffer thresholds. UniFrog now uses `500/3000 ms` audio/video decoder buffering
thresholds by default, matching the stock player style more closely than the
previous `200/1000 ms` local values.

## Tuning Knobs

Set these in `config.mk` or on the `make` command line:

- `MEDIA_VIDEO_READAHEAD_SIZE`: bytes per physical video read window.
- `MEDIA_VIDEO_READAHEAD_SLOTS`: number of retained video windows, capped in
  code at `16`.
- `MEDIA_VIDEO_LOWRES_KSHM_SIZE`: video decoder compressed KSHM ring size used
  for streams at or below 640x360. The default is 8 MiB; high-resolution streams
  still use the 16 MiB ring.
- `MEDIA_VIDEO_LOWRES_AUDIO_FEED_LEAD_MS`: hardware-audio feed lead used only
  for low-resolution video. The default is 1500 ms and is clamped so it never
  reduces the normal `MEDIA_AUDIO_FEED_LEAD_MS` value.
- `MEDIA_VIDEO_PREFILL_TARGET_MS`: target media time for startup prefill.
- `MEDIA_VIDEO_PREFILL_MIN_BYTES` and `MEDIA_VIDEO_PREFILL_MAX_BYTES`: clamp the
  startup prefill size so low-bitrate clips do not under-buffer and slow cards
  do not force an excessive wait.
- `MEDIA_VIDEO_PRELOAD_MAX_BYTES`: if non-zero, files at or below this byte size
  are read fully into RAM before playback.
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
- If startup stutter remains but mid-play is smooth, increase
  `MEDIA_VIDEO_PREFILL_MAX_BYTES` first.
- If low-resolution video has clean `slow=0` cache close stats but monitor
  lines show `ahead_a` approaching zero, increase
  `MEDIA_VIDEO_LOWRES_AUDIO_FEED_LEAD_MS` before increasing SD cache sizes.
- If a card is fast and users prefer stutter-free small videos over fast start,
  set `MEDIA_VIDEO_PRELOAD_MAX_BYTES` to a safe cap such as `16777216`.
- If `VIDDEC_INIT` returns `errno=12`, do not increase the startup cache before
  checking log order. The `native_video readahead enabled` line should appear
  after `native video_open done fd=...`; a cache allocated before decoder init
  can starve the decoder heap even when total RAM looks sufficient.
- If `slow_read` is rare but `ahead_a` or `ahead_v` reaches zero during monitor
  lines, increase hardware ahead or decoder buffering thresholds cautiously.
- If `/dev/viddec` or `/dev/auddec` write retries become frequent, decrease the
  hardware ahead caps; that indicates decoder rings are being overfilled.

Developer -> Storage -> Storage quick bench writes a media buffer suggestion
based on measured read speed. Treat it as a starting point for `config.mk`, not
as an automatic runtime profile.

## Device Log Contract

For a default build, native video should log something like:

```text
unifrog media buffered_io readahead enabled tag=native_video mode=video preload=0 slot=524288 total=8388608 slots=16 ...
```

Low-resolution H.264 should also show the selected dynamic decoder and audio
feed profile:

```text
unifrog media native video open_viddec begin ... 426x240 ... kshm=8388608 kshm_policy=lowres ...
unifrog media native video clock ... audio_feed_lead_ms=1500 audio_feed_profile=lowres ...
```

The close line now includes cache-efficiency counters:

```text
unifrog media buffered_io close ... readahead=8388608 slot=524288 slots=16 preload=... misses=... fills=... evict=... seek_hits=... disk_bytes=...
```

For 1080p, the important target is that `disk_bytes` should be much closer to
the file size than the 0116 `22x` multiplier. Some overhead is expected because
FFmpeg still probes and seeks, but repeated multi-hundred-megabyte rereads mean
the cache is still missing the active offset set.

Slow physical reads are logged as:

```text
unifrog media buffered_io slow_read tag=native_video ms=... pos=... want=... got=... disk_reads=... seeks=...
```

Startup prefill is logged as:

```text
unifrog media buffered_io prefill tag=native_video stage=buffering start=... target=... cached=... disk_bytes=... disk_ms=... kib_s=...
```

Hardware feed caps are logged as:

```text
unifrog media hw_ahead wait kind=... ahead=... max=... clock=... feed=...
unifrog media hw_ahead done kind=... clock=... feed=... ahead=...
```

Occasional `hw_ahead` waits are normal. Constant waits plus audio/video stutter
mean the feeder is oscillating between SD stalls and decoder-ring catch-up.
