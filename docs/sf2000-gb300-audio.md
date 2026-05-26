# SF2000 and GB300 Audio

This is the current audio contract for UniFrog on the HCRTOS SF2000/GB300
runtime. It records the working routes and the failed assumptions that should
not be reintroduced.

## Device Routing

SF2000 and GB300 share the same HCRTOS audio driver stack, but the board output
enable is different:

- SF2000 uses GPIO R07 as the active-low speaker/amp gate.
- GB300 uses GPIO L15 as the active-low speaker/amp gate.
- Panel ID is only a hint. A GB300 with an SF2000 panel is detected by the
  stock-bit keypad bus and must still use the GB300/L15 route.
- RF polling can disturb the GPIO-L group, so GB300 reasserts the enabled L15
  gate after controller polling.

The missing GB300 audio step was the shared `i2so_platform` runtime state.
After audio modules initialize, UniFrog clears the GB300-incompatible R07
`pinmux-mute` hook and disables platform fade. SF2000 keeps the DTS R07 mute
and fade behavior. Without that GB300 runtime cleanup, decoder clocks and PCM
writes can look healthy while the speaker stays silent.

## Core and PCM Audio

SF2000 direct SND playback is the stable UniFrog-owned PCM route. It lets the
silence gate inspect samples before transfer, so all-silent buffers can keep
the low-level path muted until real PCM exists.

GB300 normal PCM output follows the stock-era route more closely:

- AUTO PCM tries AUDSINK before direct `/dev/sndC0i2so`.
- Content is mono-mixed, but the hardware transport is stereo S16 frames.
- The physical device has one speaker; duplicate the mono sample into both
  slots instead of sending a one-channel transport.
- Direct SND is only a fallback. It is opened write-only, without AUDPAD source,
  with `start_threshold=0`, and with coalesced writes.
- On GB300 direct SND fallback, `SND_IOCTL_XFER` returning `errno=EPERM` means
  the DMA ring did not have enough room yet. Sleep and retry; do not treat it
  as a permission failure.

Do not apply SF2000 underrun-fade or neutral tone/EQ/balance policy to GB300.
Those SF2000 noise mitigations can suppress the GB300 speaker path.

## Media Hardware Audio

Native media uses `/dev/auddec` first on both devices when the HCRTOS decoder
supports the codec. The working GB300 path is:

- configure `/dev/auddec` with the linked HCRTOS `libauddrv.a` ABI;
- use a KSHM-backed audio decoder configuration;
- route decoder output to I2SO;
- open the persistent GB300 `/dev/sndC0i2so` I2SO-prime handle around the
  decoder session;
- keep the speaker gate closed until decode time actually advances;
- for MP4/M4A AAC, put AudioSpecificConfig in `audio_config.extra_data` and
  send raw demuxed AAC access units as ES packets.

Explicit ADTS `.aac` files are separate: their ADTS frames are already the file
payload. Do not wrap MP4/M4A AAC packets in synthetic ADTS. The ADTS-wrapped
MP4/M4A path could initialize cleanly, but it left the GB300 decoder clock
stuck at zero.

Audio-only playback opens auddec in freerun mode and still paces the compressed
feed against the decoder clock. Video playback lets hardware audio own the STC
and syncs video to that clock. Do not dump compressed packets into the rings at
SD-card speed; large startup bursts make video frames run fast and then exit or
stall.

On SF2000, hardware auddec can be present before viddec because that is the
normal native-video sync path. If `VIDDEC_INIT` rejects that combined state with
`errno=1`, close auddec, retry viddec in freerun mode, and use UniFrog software
PCM audio for that video session. Keep this recovery SF2000-only; GB300 media
needs the hardware-auddec plus I2SO-prime path to preserve audible output.

SF2000 can still use hardware auddec and hardware viddec together when
`VIDDEC_INIT` accepts the combined state. Keep the fast packet-dropping seek
algorithm shared across SF2000 and GB300, but preserve the current video frame
instead of hiding the video layer while the demuxer catches up.

For inter-frame video codecs, seek catch-up must also be keyframe-gated. The
demuxer can land on the previous H.264 keyframe, and resuming playback on the
first packet whose timestamp reaches the target can feed a dependent frame
without its reference history. That produces visible artifacts until the next
I-frame. The native player may show the first pre-target keyframe as a clean
still. If that keyframe is close to the target, it feeds the short pre-roll
without pacing so the decoder has reference frames and can resume on the target
packet. If the pre-roll is too long, it drops video until the target and resumes
only on a keyframe at or after the target. Audio packets before the target are
also dropped so the hardware audio clock does not restart early. Repeated seeks
while catch-up is active must use the pending target as the current position,
because hardware clocks can report `0` immediately after decoder flushes.

SF2000 keeps the hardware-specific stable sync details: viddec flush rate `0.0`
and no explicit `/dev/avsync0` timebase reset. GB300 keeps the hardware auddec
plus I2SO-prime route, including the `1.0` viddec flush rate. The high-level
seek algorithm stays shared unless a device-specific driver quirk requires a
lower-level flush or clock difference.

## Progress Signals

Auddec init/start success is not enough. A working route must show real
progress after packets are fed.

On GB300, the useful signal for the raw-ASC AAC path is
`AUDDEC_GET_CUR_TIME` advancing. The status fields can remain at zero on the
working path, so do not require `first_header_got` or `frames_decoded` before
opening the speaker. Fall back only when writes fail or when both status and the
decoder clock stay stuck.

On SF2000, keep the existing delayed-signal mute behavior for UniFrog-owned SND
PCM. That path has sample visibility and should not be changed for GB300 fixes.

## Known Bad Assumptions

These were tested while restoring GB300 audio and should stay out of the
production path:

- `SND_IOCTL_SET_EXTRA_DATA_PATH` loopthrough to I2SO. It did not provide the
  missing output step and increased freeze risk.
- Treating direct `/dev/sndC0i2so` AUDPAD/O_RDWR write success as proof of
  audible GB300 output. Logs showed successful writes while the speaker stayed
  silent.
- One-channel GB300 hardware transport. GB300 wants stereo S16 frames even
  though the content is mono and the device has one speaker.
- Synthetic ADTS wrapping for MP4/M4A AAC. Use raw access units plus ASC
  extradata.
- Requiring GB300 auddec status header/frame counters before enabling output.
  The confirmed route can have a moving auddec clock while those counters stay
  unhelpful.
- PAUSE/FLUSH/RLS teardown after stalled auddec sessions. Close the device
  instead; the extra teardown ioctls can block on this runtime.
- Reusing SF2000 R07 DTS mute/fade state on GB300. GB300 needs the runtime
  `i2so_platform` cleanup plus the L15 gate.

## Practical Log Checks

For a healthy GB300 media run, expect logs like:

- `auddec base ... aac_packet=raw_asc` for MP4/M4A AAC;
- first GB300 route `gb300_i2so_prime_kshm`;
- `gb300_i2so_prime open ... dest=...`;
- `auddec output_enable reason=clock_progress`;
- `packet_status ... time=...` with `time` increasing;
- no direct-SND fallback unless hardware auddec actually fails.

For libretro/core audio, expect stereo transport on GB300 and mono transport on
SF2000. The content policy remains mono/single-speaker; only the hardware frame
format differs.
