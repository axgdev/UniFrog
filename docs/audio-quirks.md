# SF2000 Audio Quirks

This document records the confirmed audio behavior of UniFrog on the
SF2000/GB300 HCRTOS runtime. Keep it updated when changing `src/unifrog_audio.c`
or `src/unifrog_media.c`.

## Confirmed Behavior

- The device has a faint analog noise floor from power-on. UniFrog can reduce
  software-amplified buzzing, but it cannot fully remove the physical noise
  floor through the audio driver.
- The loud buzz reported during video playback and audio diagnostics was tied
  to the speaker/output path being enabled while no real PCM signal was being
  delivered.
- On SF2000 hardware the practical output gate is GPIO R07. GB300 stock
  firmware uses GPIO L15 for the same active-low amp-enable role. The muted/off
  state is visible in diagnostics as the selected gate output bit set; the
  enabled state clears it.
- Reverse engineering `bisrv_gb300_v2.asd` confirms the GB300 stock helper at
  `0x801b8b74` sets `0xb8800058` bit 15 as output and writes
  `(level << 15)` to `0xb8800054`. The stock emulation path calls this helper
  with `level=0` after `run_sound_init(0, sample_rate, channels)`, so UniFrog
  keeps L15 active-low and explicitly restores the L15 pinmux to GPIO when the
  audio gate is enabled.
- The LCD panel ID is only a default board hint. Some GB300 units can have an
  SF2000 panel, so UniFrog also switches to the GB300/L15 audio gate when the
  local input scanner proves that the GB300 stock-bit keypad bus is present.
- The wireless RF code temporarily owns part of the GPIO-L group and historically
  configured L15 as an input. UniFrog reasserts the enabled audio gate after RF
  polling so GB300 audio is not silently disabled by controller polling.
- `SND_IOCTL_SET_MUTE` controls the low-level HCRTOS sound output path. For
  UniFrog-owned SND playback, this is the reliable way to keep the DAC quiet
  until non-silent PCM is available; the board-specific GPIO gate is still
  handled separately.
- Digital zero samples are not enough by themselves. If the amplifier path is
  unmuted and enabled, zero PCM can still produce audible board noise.

## Current Contract

- SND playback starts muted.
- `unifrog_audio_write_timeout()` scans SND PCM buffers:
  - all-silent buffers keep SND muted;
  - buffers with real signal unmute before transfer.
- `unifrog_audio_set_output_enabled(audio, 1)` enables the physical gate but
  leaves direct SND playback muted until a non-silent buffer is written.
- Closing audio mutes SND and disables the physical output gate.
- Native media playback keeps audio inside UniFrog where possible, so silence
  gating is based on decoded PCM instead of supervising an external player.
- UniFrog-owned UI/theme playback prefers the SND backend because the silence
  gate can inspect PCM buffers before transfer. The audsink backend remains a
  fallback for those short sounds.
- Libretro core playback uses the HCRTOS AUDSINK path explicitly on SF2000 to
  keep the lower-cost mono-left route. GB300 or a stock-bit GB300 input bus
  requests stereo frames but lets UniFrog's AUTO PCM opener choose the backend;
  current GB300 logs show AUDSINK accepts init but fails PCM transfer, while
  direct `/dev/sndC0i2so` accepts DMA writes.
- On GB300, UniFrog's AUTO PCM opener tries direct `/dev/sndC0i2so` before
  AUDSINK. The direct SND open uses the vendor-style HCRTOS parameters from the
  cast/sound-test examples: `O_RDWR`, AUDPAD source, `start_threshold=2`, and a
  larger DMA ring. Direct SND remains first on SF2000 because it gives the
  silence gate full PCM visibility and has been the stable route there.
- On GB300, the broad direct-PCM route probe is now disabled by default. The
  0128 logs proved that `/dev/sndC0i2so` can drive the speaker through the L15
  gate; that beep proves only PCM DMA output, not compressed hardware decode.
- On GB300, the first hardware-auddec open runs a focused `/dev/auddec` PCM
  probe before normal playback. It feeds a generated `PCM_S16LE` tone through
  `/dev/auddec` using several route/control orders and logs
  `unifrog media gb300_auddec_probe`. If one of these tones is audible, the
  auddec-to-speaker route is known and remaining silence is in compressed
  packet/config handling. If all are silent while direct SND remains audible,
  the missing piece is the auddec render path or mute/gate initialization.
- Audio-only and native video-container audio playback prefer compressed
  packets through `/dev/auddec` when the linked HCRTOS plugin supports the
  codec. Unsupported or failed software-only routes still fall back to
  FFmpeg-decoded PCM through the UniFrog SND silence gate.
- Native media must use the audio decoder ABI from the linked HCRTOS
  `libauddrv.a`, not blindly copy either adjacent public header. The bundled
  driver switches on `AUDDEC_INIT == 0x82500301`, which corresponds to a
  592-byte `struct audio_config`: it includes `codec_frame_size` and
  `dma_buffer_time`, but not the newer `mix_*` or `slave_mode` fields from the
  Linux-driver header. A 584-byte or 608-byte ioctl is rejected before
  `/dev/auddec` can initialize.
- Compressed audio packets must be fed through a KSHM-backed `/dev/auddec`
  configuration. The direct HCRTOS examples use `kshm_size` around `0x80000` to
  `0xa0000`; UniFrog uses `0xa0000`. Device logs from 0108 showed that a
  no-KSHM "minimal" configuration can return successful `AUDDEC_INIT` and
  `AUDDEC_START` while never parsing headers (`frames_decoded=0`,
  `first_header_got=0`, `AUDDEC_GET_CUR_TIME=0`) and eventually blocking
  writes. Treat init success as necessary but not sufficient.
- For direct `/dev/auddec`, the legacy `enable_audsink` field is inverted in the
  linked `libauddrv.a`: a zero value enables the internal audsink render path.
  This matches the stock HCRTOS direct-decoder examples, which memset
  `struct audio_config` and do not set the field. UniFrog keeps compatibility
  variants with value `1`, but the stock-style zero value is attempted first.
- GB300 compressed `/dev/auddec` routes start with an I2SO-only KSHM profile
  and do not rotate the first route. The 0127 diagnostics showed only
  `/dev/sndC0i2so` exists as a direct SND node; SPO/PCMO masks can return
  successful `AUDDEC_INIT`/`AUDDEC_START` while leaving the decoder clock stuck
  and the speaker silent. The combined I2SO+SPO, SPO, and PCMO profiles remain
  as fallback diagnostics if I2SO-only init fails.
- Audio-only compressed playback should open `/dev/auddec` in freerun mode
  first. STC update/sync modes are reserved for audio plus video, where the
  video decoder can synchronize to the audio-owned clock.
- Freerun does not mean "drop packet timing." HCLinux/HCRTOS feed examples and
  reverse-engineering of `libffplayer.a` both write packet PTS into `AvPktHd`.
  UniFrog keeps packet PTS/duration for `/dev/auddec` and paces audio-only
  demuxing with a bounded live feed-lead window instead of dumping the whole
  compressed file into the KSHM ring at SD-card speed. The current 3000 ms lead
  is still paced against the decoder clock; raising it much further should be
  treated as a device-tested tuning change, not passive startup prebuffering,
  because `/dev/auddec` is already running.
- Do not set `audio_flush_thres` for local file playback. HCRTOS documents that
  field as an audio-master frame-drop threshold when the I2SO DMA queue is
  deeper than the configured milliseconds; it is useful for low-latency live
  capture, but for SD-card file playback it can create audible periodic drops.
- The same rule applies when native video uses hardware auddec as the STC
  owner. The file demuxer must be the real-time source; otherwise audio and
  video stay synchronized to each other but both run fast because compressed
  packets advance through the hardware rings as quickly as SD reads complete.
  SD jitter is handled by the separate file readahead buffer, not by keeping the
  running decoder rings seconds ahead.
- The hardware feed lead and SD read buffers are build tunables. Use
  `MEDIA_AUDIO_FEED_LEAD_MS`, `MEDIA_VIDEO_FEED_LEAD_MS`,
  `MEDIA_FILE_BUFFER_SIZE`, `MEDIA_FILE_READAHEAD_SIZE`, and
  `MEDIA_VIDEO_READAHEAD_SIZE`/`MEDIA_VIDEO_READAHEAD_SLOTS` in `config.mk` or
  on the `make` command line when device logs show either
  `ahead_ms`/`ahead_a` too close to zero or excessive SD reads. The default
  hardware-audio feed lead is now one global `3000 ms` value for audio-only and
  all video resolutions; the audio payload is small enough that this is simpler
  and more robust than resolution-specific audio pacing. Video readahead is a
  small multi-window cache with a bounded startup prefill. Native video
  allocates that cache after the audio/video decoder KSHM setup so it cannot
  starve hardware decode. Low-resolution video also uses a smaller video KSHM
  ring via `MEDIA_VIDEO_LOWRES_KSHM_SIZE` to leave heap for that cache. For
  slower cards, tune
  `MEDIA_VIDEO_PREFILL_MAX_BYTES`; for no-stutter small-video playback,
  optionally set `MEDIA_VIDEO_PRELOAD_MAX_BYTES`. See
  `docs/media-buffering-algorithm.md`.
- Video and audio hardware queues are also capped against the actual decoder
  clocks with `MEDIA_VIDEO_MAX_HW_AHEAD_MS` and
  `MEDIA_AUDIO_MAX_HW_AHEAD_MS`. This prevents a long SD/demux stall from being
  followed by a large catch-up burst that queues many seconds into `/dev/viddec`
  or `/dev/auddec`. The caps should remain above the active feed lead; equal
  values make normal decoder-clock jitter look like constant over-ahead waits.
- After an explicit seek, `/dev/auddec` and `/dev/viddec` report their decoded
  clocks from zero until enough post-flush packets are fed. The bounded
  `MEDIA_SEEK_WARMUP_PACKETS` window lets those packets through before
  re-enabling hardware-ahead caps; without it, seeks can stall forever with
  `clock=0` and a large absolute packet PTS.
- Hardware-ahead waits are also bounded by `MEDIA_HW_AHEAD_MAX_WAIT_MS`. If the
  decoder clock remains stuck after a seek, UniFrog logs `hw_ahead timeout`,
  caps its internal feed timestamp, and continues instead of waiting until the
  watchdog resets the device.
- Video seeks also reset `/dev/avsync0` with `AVSYNC_SET_STC_MS`, which matches
  the HCRTOS cast player timebase path. If repeated seeks still leave viddec
  behind audio by more than `MEDIA_VIDEO_STUCK_BEHIND_MS`, UniFrog logs
  `seek video recover`, flushes viddec with the stock-style `1.0` flush rate,
  resets AVSYNC to the audio clock, and hides the video layer until post-seek
  catch-up reaches the requested timestamp.
- MP4/M4A AAC must be passed like HCPlayer does: AudioSpecificConfig in
  `audio_config.extra_data`, then raw demuxed AAC access units as ES packets.
  Wrapping those raw MP4 packets in synthetic ADTS headers initialized the
  SF2000 decoder but produced no audible audio in 0109 logs.

## Stock Firmware Notes

The stock SF2000 firmware function used by the old multicore hijack at
`0x8035665c` is not just a simple ring-buffer initializer. Disassembly shows it
opens the stock sound device, stops it, configures a 16-bit PCM stream, starts
the device, sets volume to `0x5a`, applies several sound ioctls, and then
selects duplicate mode according to channel count. The stock libretro path then
writes stereo frames into the stock audio ring. UniFrog cannot call that
function from the HCRTOS build because the stock firmware runtime is not mapped
there, but the important behavioral clues are:

- configure the DAC/SND path before opening the amp gate;
- keep output muted until real PCM exists;
- avoid software gain as a noise mask;
- prefer the left-speaker/duplicate-left route over differential or fake
  stereo output on SF2000 hardware, but keep real stereo frame delivery on
  GB300 because its stock libretro ring path wrote stereo frames;
- compare HCRTOS SND/AUDSINK settings against the stock duplicate and volume
  behavior whenever libretro audio quality regresses.

The local SF2000 HCRTOS tree contains FFmpeg package recipes, public media
headers, codec plugins, and prebuilt SDK libraries. See
`docs/libffplayer-replacement.md` for the reverse-engineering handoff that was
used to remove UniFrog's external player dependency.

## Things That Were Not the Fix

- Keeping an idle SND device open after boot was not the fix. It has been
  removed because the validated fix is gate/mute discipline, not continuous
  silent playback.
- Backlight PWM changes were not shown to be the cause of the loud buzz. The
  screen/backlight may still expose the board's analog noise floor, but the
  loud software-amplified buzz follows the audio output gate/mute state.
- Raising volume/gain can mask the buzz under real audio, but it is not a fix.

## Diagnostics

Use Developer or Storage -> Audio Probe. The important stages are:

- `1/6 auto-muted silence`: SND path active, should remain quiet.
- `2/6 forced mute`: hardware/SND mute asserted, should be quiet.
- `3/6 unmuted silence`: diagnostic stage. If this buzzes, the board/output
  path is noisy while enabled without signal.
- `4/6 raw gate off`: physical output path gated off, should be quiet.
- `5/6 re-muted silence`: raw gate restored, SND mute reasserted.
- `6/6 quiet tone`: short audible marker.

When the loud buzz is fixed, only the faint hardware noise floor should remain
during silence stages.

GB300 auddec diagnostics run once per boot on the first hardware-auddec open.
The audible/logged order is:

- `0 i2so_current_after`: current production route, I2SO, internal audsink,
  gate/control after `AUDDEC_START`.
- `1 i2so_gate_before`: same route, but system output is unmuted and L15 is
  opened before `AUDDEC_INIT`.
- `2 i2so_controls_before`: same route, but auddec channel/volume/mute ioctls
  run before `AUDDEC_START`.
- `3 i2so_audsink_after`: I2SO with `enable_audsink=1`.
- `4 default_current_after`: `snd_devs=0`, relying on the driver default.
- `5 i2so_flush200_after`: I2SO with the live-capture-style
  `audio_flush_thres=200`.
- `6 i2so_48k_after`: I2SO at 48 kHz, matching several HCRTOS cast PCM
  examples.
- `7 i2so_spo_after`: I2SO plus SPO mask for comparison only.

If direct `/dev/sndC0i2so` beeps are audible but all
`gb300_auddec_probe` tones are silent, the PCM speaker path is healthy and the
remaining failure is inside `/dev/auddec` output routing or mute sequencing. If
one auddec probe tone is audible, use that label as the production GB300
auddec route before investigating compressed codec packet details.

## Open Work

- Native video is rendered by the HCRTOS hardware video plane. UniFrog can set
  the source/destination rectangle, but it does not currently receive video
  frames as normal UI surfaces. Arbitrary compositing inside the native UI would
  need a separate decode/readback path or a confirmed hardware-plane overlay
  contract.
- If the HCRTOS player internals expose a private PCM sink or `AUDDEV_PCMO`
  path, it should be investigated. That would let UniFrog apply silence gating,
  downmixing, and any integer audio filters before the final SND transfer.
- Audio EQ/twotone controls exist in the SND driver. UniFrog currently sets
  them to neutral because previous buzzing was gate-related, not EQ-related.
  Future quality work should compare stock firmware settings before changing
  this default.
