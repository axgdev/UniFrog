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
