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
- On SF2000 hardware the practical output gate is GPIO R07. The muted/off state
  is visible in diagnostics as R07 output bit set; the enabled state clears it.
- `SND_IOCTL_SET_MUTE` controls the same useful mute/output path through the
  HCRTOS sound platform driver. For UniFrog-owned SND playback, this is the
  reliable way to keep the amplifier quiet until non-silent PCM is available.
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
- Libretro core playback uses the HCRTOS AUDSINK path explicitly with mono PCM
  routed through the left speaker path. Earlier device testing found this route
  lower-cost and closer to the stock speaker behavior than direct one-channel
  SND playback. The libretro host still controls the outer SND mute and amp
  gate so silence does not hold the analog path open.
- Audio-only and native video-container audio playback use UniFrog's direct
  FFmpeg decode path where possible, so decoded PCM goes through the same
  silence gate as UI sounds.

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
  stereo output on SF2000 hardware;
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
