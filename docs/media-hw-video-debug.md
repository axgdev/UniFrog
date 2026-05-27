## Native HW Video Debug Notes

### Symptom Pattern (v050 logs around 0097)
- Audio path keeps running, but video stays black/green/static.
- `VIDDEC_INIT` / `VIDDEC_START` return success.
- `VIDDEC_GET_STATUS` often stays near:
  - `decoded=0 displayed=0`
  - `hdr=1 pic=0 show=0`
- More likely when hardware `auddec` open fails and playback falls back to software audio decode.

### Root-Cause Direction
- Reverse-engineering `libffplayer.a` (`write_packet*` / `decoder_init`) shows that sync behavior differs when hardware audio is absent.
- In native path, software-audio playback still sent timestamped video packets in non-freerun mode.
- Without hardware `auddec`, there is no hardware AV sync clock source for video timing, so viddec can stall waiting on sync.

### Applied Fixes
- Commit: `de5d47c`
- Changes in `src/unifrog_media.c`:
  1. Force freerun packet timing (`PTS=-1`, `dur=0`) whenever video playback is in freerun mode.
  2. Treat `auddec` absence as the freerun boundary even if software audio decode is active.
  3. Align H264 post-init extradata send with ffplayer-style ES packet delivery in the post-extra phase.

### If Issue Persists
- Capture new logs and compare:
  - `unifrog media native video clock freerun=...`
  - first `video packet idx=... pts=... dur=...`
  - monitor/final status (`decoded/displayed/hdr/pic/show`)
- If `pic/show` still never rises with freerun forced, inspect:
  - H264 packetization and AUD/SPS/PPS ordering
  - `video_config` fields vs ffplayer (`sync_mode`, `decode_mode`, `codec_tag`, `kshm_size`)
  - first-frame path around `DIS_SET_LAYER_ORDER` / `DIS_SET_ZOOM` and layer visibility transitions.

### v050 Follow-Up
- Latest logs around `../latest_log/v050/0101` show freerun is already active
  when hardware audio falls back to software audio.
- The remaining failure is H.264 feed format: 240p never reaches
  `frames_decoded`, while 480p reaches only one displayed frame.
- Reverse-engineering `libffplayer.a` shows MP4/AVCC H.264 is not converted to
  Annex-B before feeding `/dev/viddec`. The player keeps raw `avcC`
  extradata in `video_config.extra_data`, preserves `codec_tag`, and writes
  FFmpeg packet bytes directly.
- `libffplayer.a` only writes H.264 extradata as a post-init ES packet when the
  extradata already starts with Annex-B `00 00 00 01`.
- The native path now logs `h264_config`, packet `mode=avcc|annexb`,
  `nal_len`, `mask`, and `trunc` so target logs identify the exact access-unit
  format and where SPS/PPS/IDR appear.

### v050 0102 Follow-Up
- Logs from `../latest_log/v050/0102` show 240p, 360p, 480p, and 720p H.264
  now decode and display frames.
- The zoom/crop problem came from programming the native `VIDDEC_SET_DISPLAY_RECT`
  and `DIS_SET_ZOOM` source rectangles to the decoded stream size. On this
  display pipeline the source rectangle must describe the full `1920x1080` HD
  video-plane canvas; smaller source rectangles crop the canvas before the LCD
  scaling step.
- The native viddec path now keeps `pic_width`/`pic_height` as the codec
  dimensions, but uses a `1920x1080` source rectangle and `1920x1080`
  destination rectangle for the hardware display rect and visible layer.
- Native viddec quick mode is disabled to match the lower-ghosting hcplayer
  presets that previously behaved best. The packet feed still logs enough
  status to decide later whether actual buffering is needed.

### v050 0103 Follow-Up
- Logs from `../latest_log/v050/0103` confirm the display geometry fix:
  native `VIDDEC_SET_DISPLAY_RECT` and `DIS_SET_ZOOM` use `1920x1080` source
  and destination rectangles.
- The test MP4 total bitrates are about 229 kbps, 304 kbps, 391 kbps,
  605 kbps, and 2.19 Mbps. Playback now uses a small AVIO chunk plus a
  post-probe multi-window read cache instead of treating the AVIO chunk as the
  playback prebuffer.
- Native FFmpeg demux now opens video files through a custom `AVIOContext`
  and logs read/seek statistics on close.
- Native viddec defaults to an 8 MiB KSHM buffer even though the SF2000 DTS
  advertises `viddec.kshm_size = <0x1000000>`. Keeping the compressed ring
  bounded leaves more normal heap available for decoded surfaces and the SD
  read cache.
- The same logs still show `decoded=0 displayed=0` for 720p and 1080p native
  attempts. If that persists with the larger buffers, investigate quick-mode
  behavior and 1080p/profile/MMZ requirements separately from SD refill.

### v050 0104 Follow-Up
- Logs from `../latest_log/v050/0104` show the 2 MiB custom AVIO buffer was
  acting as FFmpeg's read chunk, not as a useful playback prebuffer. MP4
  probing/seeking over-read most files before playback, including about 43 MiB
  before the 1080p native open completed.
- The native custom AVIO chunk is now 64 KiB with a 16 KiB fallback. Keep using
  the close log's `reads`, `bytes`, `requested`, and `seeks` fields to confirm
  whether startup IO is bounded.
- Hardware `auddec` still fails in these logs, so native video runs freerun
  while audio is decoded through the software FFmpeg/SND path. The native path
  now paces video packet submission against the software audio clock and logs
  `swsync wait`, `vtime`, `atime`, and `sw_audio_ms`.
- H.264 viddec config now uses the stream frame rate instead of forcing
  60 fps. This is intended to distinguish high-resolution timing/feed problems
  from SD refill or display geometry.

### v050 0105 Follow-Up
- Logs from `../latest_log/v050/0105` show the zero-MMZ experiment was invalid
  for this hardware path. Every tested H.264 file failed native
  `VIDDEC_INIT` immediately with `errno=1`, before any video packets were fed.
- The visible artifact path was then the software-video fallback repeatedly
  failing `VIDSINK_DISPLAY_FRAME`, not confirmed hardware decode corruption.
  The fallback now aborts after a small number of display failures instead of
  continuing to spam the display path.
- The DTS now restores a deterministic `mmz0` pool sized from the upstream
  hc15xx 1080p media formula. KSHM compressed packet rings remain dynamic
  because no named `kshm` MMZ pool is defined.
- AVCC H.264 is back to the known-good feed contract from 0102/0103:
  `avcC` extradata in `video_config.extra_data` and raw FFmpeg packet bytes to
  `/dev/viddec`. Converted SPS/PPS post-extra delivery is kept only for
  Annex-B extradata.
- FFmpeg file IO now separates probe and playback buffering. The AVIO callback
  chunk remains 64 KiB to keep MP4 probing bounded, then a post-probe
  multi-window read-ahead cache reduces SD read frequency during playback.

### v050 0120 Follow-Up
- Logs from `../latest_log/v050/0120` show the remaining 240p and 360p
  stutters were not storage-cache misses. Their close counters had clean SD
  behavior, but monitor lines still showed hardware audio `ahead_a` dipping
  close to underrun.
- Native playback now uses one conservative audio feed lead for audio-only and
  all video resolutions: `MEDIA_AUDIO_FEED_LEAD_MS=3000`. This removes the
  low-resolution-only audio lead branch. `MEDIA_VIDEO_KSHM_SIZE` is the default
  viddec compressed-ring knob, while `MEDIA_VIDEO_LOWRES_KSHM_SIZE` can still
  override low-resolution streams.
- The screen scramble/stale black-bar symptom matched a display handoff issue:
  `/dev/viddec` made the video layer visible before the first displayable frame
  (`decoded=0 displayed=0 show=0`). UniFrog now clears the framebuffer after
  the final loading progress draw, then defers video-layer visibility until the
  first decoded/displayed frame path logs `native video reveal`.
- Native audio and native video now support LEFT/RIGHT seek through the FFmpeg
  demuxer with `/dev/auddec` and `/dev/viddec` flush/start diagnostics. The
  framebuffer progress overlay is always enabled during native media playback;
  press `A` to hide or show it. Routine overlay refreshes no longer pan the
  framebuffer or log every update, which keeps display-layer writes quieter
  while preserving an on-screen progress indicator.

Expected log markers:

```text
unifrog media fb_clear tag=native_video_play ret=0 ...
unifrog media native video layer deferred fd=... reason=wait_first_frame
unifrog media native video reveal ...
unifrog media native video clock ... audio_feed_lead_ms=3000 ... overlay=1 ...
unifrog media seek video request ...
unifrog media seek auddec_flush tag=video ...
unifrog media seek viddec_flush tag=video ...
unifrog media seek demux tag=video ...
```
