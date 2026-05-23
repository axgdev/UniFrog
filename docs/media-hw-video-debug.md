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
