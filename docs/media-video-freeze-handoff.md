# Native Video Freeze Handoff

## Scope

This note tracks the specific freeze seen when opening MP4/H.264 videos with
the native FFmpeg + `/dev/viddec` pipeline (no `hcplayer`).

## Freeze Signature (logs `v050/0088`)

- Media start reaches:
  - `native video open_viddec done`
  - `native video init begin fd=...`
- Then no `native video init done` is logged.
- Device appears frozen and only recovers after manual power cycle.
- The same freeze pattern appears on repeated attempts in the same log set.

This isolates the stall to `ioctl(fd, VIDDEC_INIT, &cfg)` in
`media_video_open_decoder()`.

## Root Cause Hypothesis Used for Fix

For MP4 H.264 (`codec_tag=0x31637661`, `avc1`), the decoder was being given
`avcC` bytes in `video_config.extra_data` before `VIDDEC_INIT`.

On this platform, the reliable path is:

1. initialize `VIDDEC_INIT` without avcC in `video_config.extra_data`
2. convert avcC to Annex-B
3. send converted bytes as `AV_PACKET_EXTRA_DATA` after successful init

This matches prior stable behavior and vendor-style decoder usage.

## Current Code Path

`src/unifrog_media.c` now does:

- `media_h264_extradata_annexb(...)` conversion for H.264 container extradata
- post-init extra packet send (`media_send_extra_packet`) before start
- start path blocked if post-extra send fails
- extra diagnostics around post-extra stage

## Validation Performed

- `make -j9`: pass
- `make quick-check`: pass
- `make verify`: pass

## If Freeze Still Reappears

Collect and compare these markers first:

- `native video open_viddec begin`
- `native video init begin`
- `native video init done`
- `native video post_extra begin/done`
- `native video start done`

If freeze occurs before `init done`, keep focus on `VIDDEC_INIT` config bytes.
If freeze occurs after `post_extra`, inspect packet write path and queue status
(`VIDDEC_GET_STATUS`) instead.
