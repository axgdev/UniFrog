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
  keeps L15 active-low and changes only the same GPIO direction/output state
  when the audio gate is enabled.
- The LCD panel ID is only a default board hint. Some GB300 units can have an
  SF2000 panel, so UniFrog also switches to the GB300/L15 audio gate when the
  local input scanner proves that the GB300 stock-bit keypad bus is present.
  That GB300 bus probe runs periodically even while the SF2000 scanner reports
  plausible buttons, because a screen-swapped GB300 can otherwise look like an
  SF2000 until audio and controls are already routed incorrectly.
- The wireless RF code temporarily owns part of the GPIO-L group and historically
  configured L15 as an input. UniFrog reasserts the enabled audio gate after RF
  polling so GB300 audio is not silently disabled by controller polling.
- `SND_IOCTL_SET_MUTE` controls the low-level HCRTOS sound output path. For
  SF2000 UniFrog-owned SND playback, this is the reliable way to keep the DAC
  quiet until non-silent PCM is available. On GB300, release playback prefers
  the older AUDSINK path and keeps the L15 amplifier gate behavior separate
  from the SF2000 delayed-signal mute policy; 0142 and 0147 showed the newer
  direct SND AUDPAD route could accept nonzero DMA writes while remaining
  inaudible.
- Digital zero samples are not enough by themselves. If the amplifier path is
  unmuted and enabled, zero PCM can still produce audible board noise.

## Current Contract

- SND playback starts muted.
- `unifrog_audio_write_timeout()` scans SND PCM buffers:
  - SF2000 all-silent buffers keep SND muted, and real-signal buffers unmute
    before transfer;
  - GB300 direct-SND fallback keeps the L15 physical gate open once output is
    enabled and only reasserts the open state if a later write observes it
    closed.
- `unifrog_audio_set_output_enabled(audio, 1)` enables the SF2000 physical gate
  immediately. On GB300 it follows the v0.4.4-compatible order for AUDSINK and
  direct-SND fallback: set playback volume, unmute the global SND path, then
  open the L15 gate instead of using the SF2000 delayed signal gate.
- Closing audio mutes SND and disables the physical output gate.
- Native media playback keeps audio inside UniFrog where possible, so silence
  gating is based on decoded PCM instead of supervising an external player.
- UniFrog-owned UI/theme playback prefers the SND backend because the silence
  gate can inspect PCM buffers before transfer. The audsink backend remains a
  fallback for those short sounds.
- Libretro core playback uses mono output on SF2000. GB300 or a stock-bit
  GB300 input bus still gets the GB300 L15 gate, but release playback sends
  mono-mixed content through stereo s16 hardware frames. This matches the stock
  GB300 `run_sound_init(..., 2)` transport while preserving the single-speaker
  mono content policy. AUTO PCM still tries AUDSINK first, matching the
  v0.4.4-era route more closely than the silent direct-SND experiment.
- On GB300, UniFrog's AUTO PCM opener tries AUDSINK before direct
  `/dev/sndC0i2so`. The direct SND fallback intentionally uses the simpler
  v0.4.4-style parameters (`O_WRONLY`, no AUDPAD source, `start_threshold=0`)
  because 0142 and 0147 showed the newer vendor-style `O_RDWR`/AUDPAD route can
  report successful transfers without audible output. Direct SND remains first on
  SF2000 because it gives the silence gate full PCM visibility and has been the
  stable route there.
- The SF2000 underrun-fade silence policy is not applied on GB300. When the
  GB300/L15 route is selected, UniFrog leaves the underrun-fade register and
  neutral tone/EQ/balance controls untouched so the SF2000 noise mitigation
  cannot suppress a GB300 speaker path.
- GB300 also strips the shared DTS `i2so_platform` mute/fade fields after the
  audio modules initialize: the R07 `pinmux-mute` hook is cleared and platform
  fade is disabled at runtime. SF2000 keeps the DTS R07 mute and fade behavior.
- On GB300, the broad direct-PCM route probe is available at runtime from
  Developer -> Audio test. It is no longer a normal-playback startup probe, so a
  single device run can test direct PCM routes, L15/R07 gate combinations, and
  `/dev/auddec` PCM routes without rebuilding.
- The focused GB300 `/dev/auddec` PCM route probe is also available from
  Developer -> Audio test. 0134 through 0136 showed all production auddec routes
  stayed at `decoded=0`/`first_header_*=0`; use the runtime probe to collect
  auddec route labels instead of compiling separate one-off probe builds.
- GB300 normal playback uses `/dev/auddec` first again. 0150 proved the missing
  board step was the shared DTS `i2so_platform` mute/fade state, not the PCM
  speaker gate or mono mix. Hardware auddec now prepares that audio route before
  opening the decoder and falls back to FFmpeg-decoded PCM only if init, packet
  writes, or decode-progress checks fail.
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
- GB300 compressed `/dev/auddec` diagnostics start with an I2SO-only KSHM
  profile and do not rotate the first route. The 0127 diagnostics showed only
  `/dev/sndC0i2so` exists as a direct SND node; SPO/PCMO masks can return
  successful `AUDDEC_INIT`/`AUDDEC_START` while leaving the decoder clock stuck
  and the speaker silent. The combined I2SO+SPO, SPO, and PCMO profiles remain
  as fallback diagnostics if I2SO-only init fails.
- The GB300 hardware-auddec route opens a persistent `/dev/sndC0i2so` handle.
  The vendor `SND_IOCTL_SET_EXTRA_DATA_PATH` loopthrough to `AUDDEV_I2SO`
  remains opt-in via `MEDIA_GB300_I2SO_EXTRA_ROUTE=1`; default builds use the
  established I2SO prime route first. Stalled GB300 auddec sessions are closed
  without extra PAUSE/FLUSH/RLS teardown ioctls because those can block hard on
  this runtime. If auddec falls back to FFmpeg-decoded PCM after an auddec
  attempt, GB300 uses the simpler direct-SND software output backend to avoid a
  poisoned AUDSINK state from the failed decoder session.
- GB300 hardware-auddec probe attempts keep the speaker gate closed until
  auddec status reports header or frame progress. Init/start success and a
  moving `AUDDEC_GET_CUR_TIME` clock are not enough to unmute the physical
  output.
- System volume/mute opens keep `/dev/sndC0i2so` write-only. GB300 release
  direct-SND fallback is also write-only; only SF2000 UniFrog-owned direct PCM,
  diagnostics, and I2SO-prime playback paths use bidirectional SND opens for
  DMA.
- GB300 diagnostics treat "auddec init/start success but no decode progress" as
  a runtime fault. Do not use `AUDDEC_GET_CUR_TIME` alone as the health signal;
  0132 and 0140 GB300 logs showed the decoder clock can advance while headers
  and decoded frames remain at zero.
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
`0x8035665c`, and the GB300 equivalent at `0x8035b2a8`, are not just simple
ring-buffer initializers. Disassembly shows they open the stock sound device,
stop it, configure a 16-bit PCM stream, start the device, set volume to `0x5a`,
apply several sound ioctls, select duplicate mode according to channel count,
allocate a `0x4800`-byte ring, and start a task that copies stereo S16 frames
from the stock audio ring into the sound device. The stock libretro GB300 path
calls `platform_audio_init(sample_rate, 2)` and writes stereo frames into that
ring, mixing content toward mono in the first channel. UniFrog cannot call that
function from the HCRTOS build because the stock firmware runtime is not mapped
there, but the important behavioral clues are:

- configure the DAC/SND path before opening the amp gate;
- keep output muted until real PCM exists;
- avoid software gain as a noise mask;
- prefer the left-speaker/duplicate-left route over differential or fake
  stereo output on SF2000 hardware, and mix GB300 content to mono while still
  feeding duplicated stereo frames into its I2S hardware path;
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

GB300 direct PCM and auddec diagnostics run together from Developer -> Audio
test. The direct PCM section logs `gb300_probe` routes and then a gate matrix:

- `audsink_i2so_spo`, `audsink_spo`, `audsink_i2so`, `audsink_pcmo`, and
  `audsink_i2so_pcmo`: AUDSINK output masks.
- `snd_i2so_audpad`, `snd_spo_i2sodma`, `snd_spo_spodma`, and
  `snd_pcmo_audpad`: direct SND nodes and sources.
- `both_low`, `l15_low_r07_high`, `l15_high_r07_low`, and `both_high`: physical
  gate matrix while writing tone to `/dev/sndC0i2so`.

The auddec section logs this route order:

- `i2so_extra_audsink_bypass_after`, `i2so_extra_bypass_after`, and
  `i2so_extra_audsink_dma_after`: extra-data-path loopthrough comparisons.
- `i2so_prime_after_dma`, `i2so_prime_before_dma`, and
  `default_prime_after_dma`: I2SO-prime variants around the auddec init/start
  order.
- `i2so_prime_after_bypass` and `i2so_audsink_prime_after`: prime comparison
  routes for bypass destination and `enable_audsink=1`.
- `i2so_current_after`, `i2so_gate_before`, `i2so_controls_before`,
  `i2so_audsink_after`, and `default_current_after`: non-prime control-order
  and default-device diagnostics.
- `i2so_flush200_after`, `i2so_48k_after`, and `i2so_spo_after`: comparison
  routes for live-capture flush threshold, 48 kHz, and I2SO plus SPO mask.

If direct `/dev/sndC0i2so` beeps are audible but all
`gb300_auddec_probe` tones are silent, the PCM speaker path is healthy and the
remaining failure is inside `/dev/auddec` output routing or mute sequencing. If
one auddec probe tone is audible, use that label as the production GB300
auddec route before investigating compressed codec packet details.

The legacy `UNIFROG_AUDIO_GB300_ROUTE_PROBE_ONCE` and
`UNIFROG_MEDIA_GB300_AUDDEC_PROBE_ONCE` compile-time probes remain for automatic
startup capture, but they are not the normal GB300 workflow. Prefer Developer ->
Audio test because it exercises the widest set of route and gate combinations
in one firmware build.

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
