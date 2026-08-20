# SF2000/GB300 Audio Quality Strategy

## Objectives

- Keep libretro and media playback clear, continuous, and synchronized.
- Suppress the physical speaker noise floor without clipping attack transients.
- Keep every supported transport testable even when it is not the production
  default.
- Choose production routes from hardware evidence, not API success alone.

## Production Routes

### SF2000

- Libretro PCM: mono AUDSINK with 512-frame writes and the R07 amplifier gate.
- Native compressed media: hardware `/dev/auddec` where decode progress is
  confirmed, with software PCM fallback.
- Silence policy: preserve underrun fade, hold R07 open across short gaps, and
  close it only after sustained silence. Mute before closing R07 and reopen it
  only when real PCM or decoder progress is available.

### GB300

- Libretro PCM: duplicated-stereo direct SND using the v0.4.4-compatible route,
  2048-frame coalesced writes, and the L15 amplifier gate.
- Native compressed media: hardware `/dev/auddec` where progress is confirmed,
  with direct-SND software PCM fallback.
- Silence policy: leave SF2000 underrun-fade behavior disabled, keep L15
  sequencing separate, and avoid AUDSINK for libretro until its stereo queue
  accounting is understood.

Both PCM routes use I2S DMA hardware. AUDSINK is middleware over the same
hardware and is not additional acceleration.

## Repair Strategy

1. Keep the current production routes as known-good baselines.
2. Require route diagnostics to prove audible output, accepted writes, decoder
   progress, and correct amplifier-gate transitions.
3. Repair GB300 AUDSINK as a diagnostic mode by testing stereo-aware ring sizes,
   coalesced writes, and byte-versus-frame accounting. Do not make it the
   libretro default unless it reaches zero write failures and underruns.
4. Reduce GB300 direct-SND latency incrementally by testing 1024-frame and then
   512-frame transfers while retaining the 2048-frame fallback.
5. Improve SF2000 video silence handling with two levels: hardware fade/mute for
   short starvation, then mute plus R07 close for sustained silence or pause.
6. Keep hardware-decoder init success insufficient by itself. Require decoded
   frames, headers, or clock progress before opening the physical output route.

## Acceptance Metrics

- No audio write failures or reported underruns.
- Each production PCM write succeeds on the first attempt.
- No repeated output-gate transitions during ordinary short audio gaps.
- Libretro remains at expected frame rate with audio enabled.
- Native and FFmpeg video modes return cleanly after the bounded test.
- Listener confirms no clipped first attack, periodic gaps, loud idle buzz, or
  synchronization drift.

Run `js2300/scripts/audio-quality-matrix.js` on both boards with the same configured
game and video. Preserve the retained log from each board for comparison.
