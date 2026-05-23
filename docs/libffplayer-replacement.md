# Media Reverse-Engineering Handoff

UniFrog no longer links the external `libffplayer.a` player. Media playback is
owned by `src/unifrog_media.c`, using FFmpeg for demux/software decode and the
HCRTOS `/dev/auddec`, `/dev/viddec`, `/dev/vidsink`, `/dev/dis`, and SND paths
directly.

## Repositories

- `unifrog-hcrtos-sdk`: mixed-license SDK submodule used by the build. Important
  headers are under `include/hcrtos/hcuapi`, especially `vidmp.h`, `viddec.h`,
  `avsync.h`, and `codec_id.h`.
- `~/host-frogdev/hclinux`: useful for HCRTOS/Linux media examples and driver
  behavior clues.
- `~/host-frogdev/universal/sf2000_hcrtos`: useful for SDK component recipes,
  display/audio/media examples, and vendor driver configuration.
- `~/host-frogdev/universal/sf2000_gb300_multicore_private`: useful only for
  stock-firmware behavior comparisons, especially audio and logging.
- `~/host-frogdev/universal/temp/frontend`: MuOS frontend reference only.
  UniFrog is theme-compatible/inspired, not a MuOS source fork.

## Useful Techniques

Extract and inspect the old player archive without committing artifacts:

```sh
rm -rf build/re && mkdir -p build/re
cd build/re
/opt/mipsel-mti-elf/bin/mipsel-mti-elf-ar x ../../unifrog-hcrtos-sdk/lib/vendor/libffplayer.a
/opt/mipsel-mti-elf/bin/mipsel-mti-elf-objdump -dr built-in.o > libffplayer.objdump.txt
/opt/mipsel-mti-elf/bin/mipsel-mti-elf-objdump -t built-in.o > libffplayer.symbols.txt
strings -tx built-in.o | rg '/dev|VIDDEC|AUDDEC|avsync|extra|decoder'
```

Good symbols to inspect: `decoder_init`, `write_packet`, `write_extradata`,
`read_thread`, `alloc_decoder_device`, and `close_decoder_device`. `rizin -A`
and `pdg` from rz-ghidra are useful after `objdump` has identified the narrow
region to decompile.

Confirm the active build still uses the source implementation:

```sh
make native-media-link-check
rg -n "ffplayer|hcplayer|HCPlayer|hcplayer_|libffplayer" Makefile src include docs tools
rg -n "libffplayer|ffplayer|hcplayer" build output
/opt/mipsel-mti-elf/bin/mipsel-mti-elf-nm -A output/sf2000.out build/core_modules/*.out | rg -i "ffplayer|hcplayer"
```

`make native-media-link-check` is part of `make check` and `make verify`. It
checks project source, linked output symbols, and link maps for vendor-player
references. The manual source search should only find this handoff
documentation. The build/output search and symbol search should find no
matches. The SDK submodule still carries `include/vendor/ffplayer.h` and
`lib/vendor/libffplayer.a` as reverse-engineering inputs, but UniFrog project
code should not include or link them.

For source-level SDK behavior clues, inspect:

- `components/hc-examples/source/showlogo.c` in the HCRTOS trees. It is the
  clearest source example for feeding HCRTOS media decoders without ffplayer:
  parse stream headers, fill `struct video_config`/`struct audio_config`, write
  large extradata as `AV_PACKET_EXTRA_DATA`, then write `AvPktHd` plus ES data
  to `/dev/viddec` or `/dev/auddec`.
- `components/hc-examples/source/es_decoder.c` for a smaller elementary-stream
  decoder setup.
- `unifrog-hcrtos-sdk/include/hcrtos/hcuapi/viddec.h` and `vidmp.h` for the
  public video decoder interface. There is no separate public still-image API
  in the SDK submodule; the linked `libviddrv_imagedec.a` appears to register
  through the normal video decoder path.

## Important Findings

- MP4 H.264 `avcC` extradata is passed in `video_config.extra_data` with
  `extradata_mode=0`, and compressed packets are written in the FFmpeg demuxer
  packet format. Do not force `h264_mp4toannexb` for this path unless target
  logs prove the decoder needs it for a specific stream.
- H.264 extradata is written after `VIDDEC_INIT` as an ES packet only when the
  extradata already starts with Annex-B `00 00 00 01`.
- Normal compressed packets use `AvPktHd` followed by elementary stream data.
- `render_vframe` in vendor `libffplayer.a` allocates a `0x5c` `vframe_info`
  payload and submits it with ioctl `0x805c1a00` (`VIDSINK_DISPLAY_FRAME`) on
  `/dev/vidsink`. `DIS_SET_DISPLAY_INFO` is a different ABI and can produce
  green frames for software-decoded FFmpeg video.
- Video writes should check `/dev/viddec` queue space with `VIDDEC_GET_STATUS`
  before filling the KSHM ring, then send EOS and wait briefly with
  `VIDDEC_CHECK_EOS`.
- Audio decoder configs need real `snd_devs`, buffering range, and `kshm_size`.
  The KSHM-backed profile must be tried before any no-KSHM diagnostic/minimal
  profile because this `libauddrv.a` can report successful init/start without
  actually consuming compressed input.
- The SDK FFmpeg build can demux more formats than it can software-decode, so
  prefer compressed packets to HCRTOS decoder plugins when possible.
- The SDK headers define many image/video codec IDs, including PNG/BMP/GIF and
  HEVC/VP9, but header presence alone is not decoder support. Route only codecs
  backed by linked plugin libraries or target-tested software fallback.
- The linked audio plugin set covers AAC, FLAC, MP3, Opus, PCM, RealAudio,
  Vorbis/Tremor, WMA, and WMA Pro. HCRTOS examples report DTS, EAC3, and APE
  as unsupported audio events, and this SDK build has no AC3/EAC3/DTS/APE/ALAC
  decoder plugin archives, so those should not be routed to `/dev/auddec`
  without new target evidence.
- Container audio paths should keep codec setup data separate from ES payloads.
  For example, native FLAC passes STREAMINFO as decoder extradata and begins ES
  writes at the first FLAC audio frame after metadata blocks.
- MP4/M4A AAC follows that same rule: pass AudioSpecificConfig as auddec
  extradata and feed raw AAC access units. Do not synthesize ADTS around MP4 AAC
  packets unless target logs prove a specific raw-ADTS stream needs it.
- `/dev/auddec` packets should carry PTS/duration when FFmpeg provides them,
  including freerun audio-only playback. Audio-only file playback should pace
  packet feeding with a small live feed-lead window. The decoder ring is not a
  startup prebuffer once `/dev/auddec` has started; large leads are heard as
  initial fast playback.
- Local file playback should leave `audio_flush_thres` at `0`. That HCRTOS
  field intentionally drops audio frames when the I2SO DMA queue exceeds the
  threshold, so using the live-capture examples' `100-200ms` values can sound
  like periodic distortion even while A/V sync remains correct.
- Native video file playback also needs bounded packet-feed pacing. HCRTOS cast
  examples are already paced by network/input arrival; local SD files are not,
  so UniFrog paces video and hardware-audio ES writes against stream timestamps.
  The larger SD readahead buffer is the intended jitter absorber for local file
  reads.
- The current local-file tuning knobs are `MEDIA_AUDIO_FEED_LEAD_MS`,
  `MEDIA_VIDEO_FEED_LEAD_MS`, `MEDIA_FILE_BUFFER_SIZE`, and
  `MEDIA_FILE_READAHEAD_SIZE`. Video has its own multi-window cache controlled
  by `MEDIA_VIDEO_READAHEAD_SIZE` and `MEDIA_VIDEO_READAHEAD_SLOTS` because
  seek-heavy MP4 demuxing alternates between active audio/video file regions.
  Startup buffering is controlled by `MEDIA_VIDEO_PREFILL_*`, and optional
  full-file preload for small videos is controlled by
  `MEDIA_VIDEO_PRELOAD_MAX_BYTES`. Set these in `config.mk` for device
  experiments instead of editing `src/unifrog_media.c` directly. Native video
  creates these caches after the hardware decoder KSHM allocations, so cache
  size changes should not prevent `/dev/viddec` from opening. The algorithm and
  log counters are documented in `docs/media-buffering-algorithm.md`.
- `MEDIA_AUDIO_MAX_HW_AHEAD_MS` and `MEDIA_VIDEO_MAX_HW_AHEAD_MS` cap how far
  packet feeding can run ahead of the hardware decoder clocks after a stall.
  These are separate from the wall-clock feed-lead settings.
- The SF2000 audio path must stay muted/gated until real PCM exists. Digital
  zeroes alone can still expose board noise.

## Current UniFrog Map

- Frontend: `src/native_frontend.c` plus `src/frontend_lvgl.c`.
- Theme parsing/rendering: `src/frontend_lvgl.c`, `src/unifrog_ui.c`,
  `src/unifrog_image.c`, `src/unifrog_ge.c`.
- Media: `src/unifrog_media.c`.
  - Native video: FFmpeg demux, HCRTOS `/dev/viddec`/`/dev/vidsink`, H.264
    avcC post-init ES packet handling, queue-space waits, and bounded EOS wait.
    Extension routing covers MP4/MOV/MKV/AVI/TS/M2TS, MPEG program and
    elementary streams, raw H.264, MJPEG, ASF/WMV, and RM containers when backed
    by the linked SDK codecs.
  - Native audio: WAV PCM/MS ADPCM software or auddec paths; raw packetizers for
    MP3, ADTS AAC, FLAC, Ogg Vorbis/Opus; demuxed auddec/software fallback for
    containers such as M4A, WMA/ASF, RA/RM, and video-container audio.
  - Native images: FFmpeg first-frame decode plus libswscale into the RGB565
    framebuffer for JPG/PNG/GIF/BMP-style image media.
- Audio gate/mute: `src/unifrog_audio.c`, documented in `docs/audio-quirks.md`.
- Storage/log resilience: `src/unifrog_log.c`, `src/unifrog_storage_probe.c`,
  `docs/filesystem-mmc-notes.md`.
- Libretro hosting: `src/unifrog_libretro_host.c` and core module files.
- Optional JS2300 scripts: `src/frontend/js2300_frontend_*.c`; this is not the
  removed JavaScript frontend.

## Remaining Work

- Hardware-test native video stability and EOS behavior across the linked SDK
  video codecs and routed extensions: H.264, MPEG-1/2, MPEG-4/H.263, MJPEG,
  VC1/WMV3, and VP8.
- Hardware-test native image viewing on target media and decide whether HCRTOS
  `libviddrv_imagedec` offers a better path than FFmpeg/libswscale for large
  images.
- Hardware-test native container audio coverage for AAC, Vorbis, Opus, FLAC,
  WMA, RA, and edge cases where the SDK decoder needs different container
  headers or packet boundaries.
- Do not add HEVC/VP9 routes for the SF2000 SDK build without new evidence:
  headers define those codec IDs, but the linked prebuilt video decoder plugins
  are not present in `unifrog-hcrtos-sdk/lib/vendor`.
- Keep all slow SD-card work cached or asynchronous; this device has a weak CPU
  and fragile storage, but a comparatively useful GE.
