# SF2000 / HC15xx Hardware Findings

This document records the SF2000/GB300 hardware behavior observed while building the native HCRTOS UniFrog runtime, plus useful clues from the HCRTOS source tree and the B210 chipset documentation in `/root/host-frogdev/universal/sf2000_chipset_documentation`.

The B210 files are not guaranteed to match the retail SF2000 PCB exactly. Treat schematic-derived items as leads until confirmed on hardware.

## Confirmed On Device

### CPU and ISA

- The CPU behaves like MIPS32r1 with a partial and buggy subset of MIPS32r2.
- Working r2-family instructions verified with correctness tests:
  - `ext`
  - `ins`
  - `clz`
  - `clo`
  - `mul`
  - `madd`
  - `maddu`
  - `msub`
  - `msubu`
  - `movn`
  - `movz`
  - `sync`
  - `ehb`
- Broken or unavailable instructions:
  - `seb`, `seh`, `wsbh`, `rdhwr`: illegal instruction trap.
  - `rotr`, `rotrv`: execute but behave like logical right shifts, so they are not safe as rotates.
- The custom `mipsfrog` toolchain enables only the verified useful subset and avoids the broken instructions.

### CPU Clocks

- HCRTOS DTS supports SCPU clock selection through `clock = <N>` and `scpu-dig-pll-clk`.
- Known stable boot-time SCPU settings tested:
  - about 594 MHz
  - about 810 MHz
  - about 918 MHz
- Runtime PLL probing showed selectors and PLL values can be changed, but aggressive overclock and some underclock probes can freeze the device.
- UniFrog exposes guarded runtime launch profiles at 198, 297, 396, 594,
  702, 756, 810, 864, and 918 MHz. 918 MHz remains the current
  high-performance ceiling. Values above it are not currently safe.
- CP0 count is stable enough for relative benchmarking, but absolute MHz estimates need calibration because the count source is not necessarily equal to CPU core frequency.
- Hardware protocol delays must not assume the boot-time CPU clock after
  runtime SCPU changes. Use `unifrog_perf_delay_us()` for short bit-bang
  delays and RTOS-tick time helpers for real-time UI/pacing. See
  `docs/clock-speed-resilience.md`.

### Display, Video, and Framebuffer

- Native HCRTOS framebuffer is available through `/dev/fb0`.
- UniFrog now exposes `/dev/fb0` through `unifrog/fb.h` so callers can map an
  RGB565 framebuffer, flush it explicitly, wait for vsync, and use y-pan
  buffering when the framebuffer allocation allows it.
- The visible LCD is 320x240, but the display pipeline/video plane uses a
  larger internal representation. Hardware video required a specific display
  mode to show the full frame instead of a cropped/tiny region.
- Native HCRTOS video must use the HD video-plane geometry even on the 320x240
  SF2000 LCD. The confirmed full-screen setup is `DIS_TYPE_HD` /
  `DIS_LAYER_MAIN` with `VIDDEC_SET_DISPLAY_RECT` and `DIS_SET_ZOOM` using a
  `1920x1080` source canvas and `1920x1080` destination rectangle. Programming
  a `320x240` destination shows only a small top-left rectangle on the LCD, and
  programming lower decoded dimensions as the source crops/zooms the video
  plane before scaling.
- Hardware video decode works through the HCRTOS `/dev/viddec` path when the
  board DTS reserves MMZ media memory and `viddec.kshm_size`:
  - `/dev/viddec` opens.
  - `/dev/vidsink` opens.
  - `/dev/dis` opens.
  - MP4/H.264 decode and audio decode both work.
- Native video uses a 2 MiB FFmpeg AVIO read buffer for MP4 demuxing and asks
  `/dev/viddec` for a 16 MiB KSHM buffer to match the SF2000 DTS.
- Best video playback behavior observed so far:
  - Quick mode off improved smoothness.
  - Audio-master, STC sync, freerun, and audio-quick modes all produced usable audio/video.
  - Video-master with quick mode off produced smoother video but audio stutter.
  - Audio-flush mode looked sped up and should not be the default.
- The GE accelerator is available through `/dev/ge`.
  - GE fill is much faster than CPU framebuffer fill.
  - GE stretch/blit works and is useful for scaling emulator frames to LCD output.
  - `HCGE_SET_CLOCK` controls the GE accelerator clock, not the CPU clock.
  - `loghcrtos38.txt` showed the 198/148 selectors were faster than the
    225/238 selectors for fill and stretch tests, despite their labels.
  - UniFrog exposes the tested GE operations through `unifrog/ge.h` without
    leaking HCGE SDK structs into application code.
  - UniFrog also exposes `unifrog/presenter.h`, a reusable RGB565 presentation
    path that combines source cache flush, GE stretch, optional aspect
    preservation, optional vsync, and y-pan page switching.

### Storage

- The SD/MMC path is sensitive to signal integrity. A flat SD extender cable caused frequent CRC errors and automount churn.
- The default build boots directly in `wide20`: 4-bit SD, 20 MHz,
  high-speed disabled, and no UHS/1.8 V negotiation. This keeps the 4-bit
  throughput benefit while avoiding high-speed/UHS negotiation on weak cards
  and SD extenders. Runtime fast-read mount/remount windows
  are disabled by the default `SD_READ_MODE=boot`; ROM and native module reads
  stay on the boot profile. Developer -> Storage test can run a guarded runtime
  sweep from that boot profile: it
  buffers logs, shows progress on screen, prefers `/ROMS/test.md` when present,
  verifies a safe remount first, switches profiles through the SD bus
  suspend/resume hooks, records host caps/timing/mount status, restores the
  boot profile, then writes the report. The screen shows the most reliable
  freeze stage; warm reboot diagnostics are secondary because full power cycles
  can overwrite them before UniFrog starts.
- `SD_MODE=safe`, `wide1`, `wide2`, `wide4`, `wide8`, `wide10`, `wide12`,
  `wide14`, `wide16`, `wide18`, `wide20`, `wide22`, `wide24`, `wide25`,
  `wide37`, `hs1`, `wide50`, `wide`, `uhs12`, `uhs25`, and `uhs` remain
  fixed-profile diagnostic boot builds.
  `logunifrog0009.txt` showed wide and UHS were unstable on the tested device.
- SD cards support 1-bit and 4-bit transfer widths here. The HCRTOS MMC driver
  reports invalid `bus-width` values, so 2-bit and 3-bit profiles are not valid
  experiments.
- Runtime profile switching is used for diagnostics only unless
  `SD_READ_MODE` is explicitly set to a non-boot profile. Frontend startup keeps
  file-backed logging suspended until the first screen is ready, then flushes.
  If a diagnostic fast window fails, ROM content prep restores the boot profile
  before core init and retries the read on the boot profile. Storage full test reads
  `/ROMS/probes/test*.md`, returns to the boot profile after each experimental read,
  and checkpoints the report before the next probe. Storage mode test switches
  to one selected profile, reads all probes, then restores once; use it to
  separate sustained-read stability from repeated suspend/resume stress. If an
  unstable mode wedges inside the MMC command path, software recovery may still
  fail.
- Buffered UniFrog log flushes and SDK file UART drains use synchronous writes
  and close the log file after each flush. This is slower, but it gives the SD
  card an explicit completion point instead of leaving background appends dirty.
- Trust the `unifrog storage config` line when comparing SD modes. It reports
  the DTB profile actually seen by the kernel (`bus-width`, high-speed, UHS,
  and 1.8 V flags).
- The RGB panel timing now uses the 9 MHz clock option with the existing
  444x304 totals, for roughly 66.7 Hz refresh. The previous 6.6 MHz profile was
  roughly 48.9 Hz and could produce visible rolling/diagonal flicker, especially
  when SD activity injected noise onto shared rails.
- `logunifrog0022-*` fixed-profile boots still failed before libretro dispatch
  when launching a ROM. The runtime single-mode tests were better isolated:
  `uhs25` passed every probe and reached about 14 MiB/s on the 50 MiB read,
  while `wide50`, `uhs12`, and `uhs` still showed read failures. Later game
  launch testing showed `uhs25` can still stall on the third repeated
  safe-to-fast runtime switch even though suspend/resume and mount report
  success. Treat the UHS profiles as electrical/protocol experiments, not the
  default user-facing fast path.
- UniFrog keeps `/log.txt` bounded: when it grows past 1 MiB at boot, it moves
  the previous file to `/log-prev.txt` before appending the new run.
- The SDK file UART appends lazily after mount, then `fsync()`s and closes the
  file after each buffered drain. It must not open and `fsync()` `/log.txt`
  immediately on every mount notification because that competes with early SD
  probing.
- If a transient file UART write or sync fails, the logger closes the file
  descriptor but keeps the mount name so the delayed worker can reopen and retry
  instead of waiting for a fresh mount notification.
- HCRTOS FAT append semantics must be real, not just `O_APPEND` in the stored
  file flags. The FAT mount has to seek to end on open and before each append
  write, otherwise UniFrog flushes and file UART writes can overwrite earlier
  runs.

### Backlight and Panel Brightness

- `PINPAD_R05` is the LCD backlight control line.
- R05 as GPIO is a hard on/off gate:
  - GPIO low turns the backlight on.
  - GPIO high turns it off.
- R05 can also be muxed to `PWM_2`, exposed as `/dev/pwm2`.
- PWM2 is the real backlight dimming path:
  - `F10000 P1` dims directly as duty decreases.
  - `F1000 P0` is inverted: lower raw duty makes the backlight brighter.
- Default chosen for the HCRTOS DTS:
  - `/dev/pwm2`
  - 10 kHz
  - polarity 1
  - GPIO R05 kept for hard recovery on/off
- R05 must not be reconfigured as GPIO after enabling PWM for nonzero
  brightness. GPIO mode is only the hard off/recovery path; otherwise it
  cancels PWM2 and leaves the display effectively full-on.
- `/dev/backlight` writes are low-frequency control operations, so diagnostics
  can safely log the requested level, quantized duty, R05 mux value, and PWM
  ioctl results. Those lines are the first thing to check if the UI-reported
  brightness and physical panel brightness disagree.
- UniFrog's app-facing backlight helper drives `/dev/pwm2` directly after
  selecting `PINMUX_R05_PWM_2`, matching the old working PWM probe path. It
  falls back to `/dev/backlight` only if direct PWM setup fails.
- See `docs/sf2000-backlight-pwm.md` for the complete working contract and
  diagnostics.
- HCRTOS already has a generic `/dev/backlight` driver with:
  - `backlight-pwmdev`
  - `backlight-frequency`
  - `brightness-levels`
  - `default-brightness-level`
- Earlier SF2000 DTS only used `backlight-gpios-rtos`, so the practical default before this work was on/off at full brightness.

### Panel-Side Dimming

- The ST7789 panel accepts brightness/CABC commands:
  - `0x53`
  - `0x51`
  - `0x55`
- These alter panel/image behavior, not the LED backlight.
- Some CABC combinations create a pixelated or CRT-like appearance. This could be explored as an optional visual effect, but it should not be treated as a power-saving backlight control.
- PWM2 is preferable for real night-mode and battery-saving brightness.

### LED

- `PINPAD_L25` controls the red/green status/charging LED behavior:
  - one GPIO state shows green
  - the other shows red
- No evidence so far of RGB/full-color LED control. It is likely a two-color LED package or two LEDs wired through a single selector/control path.

### Wireless Controllers

- The stock SF2000 firmware supports the wireless controllers through a small RF IC, not through a normal HCRTOS `/dev/input` device.
- Reverse engineering `bisrv_08_03.asd` showed the RF IC is driven by bit-banging MSYSIO registers:
  - `0xb8800050`: input sample register
  - `0xb8800054`: output register
  - `0xb8800058`: direction/enable register
  - `0xb8800354` and `0xb8800358`: extra GPIO/pin setup used during RF init
- The protocol uses:
  - data bit: `0x08000000`
  - clock bit: `0x10000000`
  - chip-select bit: `0x20000000`
- Stock RF init writes:
  - `0x53=0x5a`
  - `0x53=0xa5`
  - `0x25=0xa5`
  - readback register `0x05` must return `0xa5`
  - then `0x3d=0x20`, `0xfc=0`, `0xe1=0`, `0xe2=0`, `0x27=0x70`, `0xfd=0`
- Packet polling checks RF register `0x07` bit `0x40`, reads two bytes from register `0x61`, and uses the RF status RX-pipe bits to choose player 1 versus player 2. Hardware testing with `loghcrtos32.txt` showed player 1 as `status=0x40`, then player 2 as `status=0x42`; raw bit `0x8000` also appeared after the player 2 switch.
- Raw wireless bits decoded by the stock firmware:
  - `0x0020`: SELECT
  - `0x0010`: START
  - `0x0008`: UP
  - `0x0004`: DOWN
  - `0x0002`: LEFT
  - `0x0001`: RIGHT
  - `0x0080`: A
  - `0x0040`: B
  - `0x0800`: L
  - `0x4000`: X
  - `0x2000`: Y
  - `0x1000`: R
- The HCRTOS implementation now initializes the RF IC through the confirmed stock-compatible sequence, polls packets, exposes reusable per-port and combined button state helpers, logs P1/P2 transitions, and ORs both wireless ports into the UniFrog menu button state.

### Audio

- Native HCRTOS audio works through `/dev/sndC0i2so`.
- `loghcrtos38.txt` showed 512-byte periods had the lowest measured write cost
  across 22050, 44100, and 48000 Hz. Larger periods blocked longer in the
  current benchmark.
- UniFrog exposes a small S16 interleaved PCM API through `unifrog/audio.h`.
- The B210 design strings expose analog audio-related nets:
  - `LO_PWMLP`
  - `LO_PWMRP`
  - `VDDA_DAC`
- The DB-B210 schematic shows the speaker amp is fed from the SoC analog/PWM
  DAC left output (`LO_PWMLP`) through the MX2018A amplifier. The R07/L15 gate
  only controls the downstream amp enable; it does not silence the DAC output.
- Use `/dev/sndC0i2so` `SND_IOCTL_SET_VOLUME` and `SND_IOCTL_SET_MUTE` for the
  low-level DAC path before opening the amp gate. Opening only the gate can
  expose DAC idle noise, which is why raising software gain masked noise but
  made audio crunchy.
- Earlier audio diagnostics found that the SF2000 speaker route behaves as a
  mono left-path output. Differential stereo and dual-mono experiments produced
  worse noise on device; the stable libretro path is mono PCM through AUDSINK's
  left-channel duplicate route with software gain defaulting to 1x.
- Confirmed stable UniFrog route, May 2026:
  - open AUDSINK as one-channel S16 PCM for libretro
  - mix libretro stereo to mono in software
  - keep software gain fixed at 1x; the device is already loud and higher gain
    can clip
  - set hardware DAC volume/mute through `/dev/sndC0i2so`
  - keep the external amp gate closed while priming/starting audio
  - open the amp gate only after the DAC path is configured and unmuted
  Do not reintroduce duplicated stereo, differential drive, or gain-as-noise
  masking without a device log proving the analog tone/noise issue stayed gone.
- HCRTOS audio driver and `/dev/auddec` can produce working audio. Current
  libretro logs show the AUDSINK volume ioctl returning unsupported, so UniFrog
  also applies the SND volume/mute ioctls directly when using AUDSINK.
  `loghcrtos148` also showed audio writes are measurable but much
  smaller than gpSP core frame time, and AUDSINK delay is not reliable enough to
  drive auto-frameskip directly on this path.

## Benchmark Findings

### MIPS r2 Subset

Instruction microbenchmarks showed:

- `ins`: meaningful improvement over mask/or.
- `clz`/`clo`: large improvement over software loops.
- `maddu`: useful improvement for multiply-accumulate patterns.
- `movn`/`movz`: useful improvement over branchy conditional moves.
- `ext`: correct but close to shift/mask in isolated tests; still useful in real decode-heavy code.
- `mul`: no major speedup over `mult/mflo` on this CPU.

GBA-like workload tests showed `mipsfrog` is generally competitive or better than pure MIPS32r1, but results vary by workload and optimization level. `-Os` sometimes wins because the device has small caches and code footprint matters.

### Native Hardware Paths

The latest performance report in `loghcrtos38.txt` showed:

- CPU framebuffer fill is expensive: about 1,069,940 CP0 counts per 320x240
  RGB565 frame in that run.
- GE fill is far faster: about 103,747 counts per full-screen fill at the
  faster GE selector.
- GE stretch-blit is valuable for scaling emulator framebuffers: common source
  sizes from 160x144 to 320x240 were around 360k to 371k counts per frame at
  the faster selector.
- Cached memory plus explicit flush remains better than uncached drawing for
  rendering-style workloads.
- Framebuffer y-pan works and the current allocation exposes many possible
  pages, so double/triple-buffered presentation is practical.
- `/dev/mmz` and `/dev/dsc` opened, making them useful future leads for media
  buffers and crypto/hash acceleration once the matching SDK headers are
  promoted.
- `/dev/adc` and `/dev/queryadc0` were missing in this image, so battery
  telemetry still depends on the direct fallback path.
- Full cache flush has measurable cost but is not the dominant cost for every operation.
- Hardware video decode works and is much faster than anything the CPU could do in software.
- The preferred libretro presentation path is RGB565 core output, source cache
  flush, GE stretch through `unifrog_presenter_present_rgb565()`, then vsync/pan
  according to the frontend latency policy.
- In gpSP dynarec testing, fixed frameskip 2 reduced presented GBA frames to
  roughly one in three, but the demanding title-screen run still measured about
  30-32 FPS. The active core frame cost stayed much larger than GE presentation
  cost, so further host-side GE/display tricks are expected to be small wins
  for that workload unless presentation becomes the bottleneck in another core.
- `/dev/dis` supports `DIS_SET_ZOOM` on the main display layer, and old HCRTOS
  SF2000 code used it for video-layer sizing. A future opt-in experiment could
  use display-layer scaling to avoid per-frame GE stretch, but it must preserve
  the LCD setup used to avoid SF2000 tearing and should be treated as a
  presenter-mode experiment rather than a gpSP fix.

## Schematic / B210 Documentation Leads

Files inspected:

- `DB-B210-V1.1-SCH.pdf`
- `DB-B210-V1.1.DSN`
- `DB-B210-V1.1.pcb`

The schematic PDF is image-like and not text-extractable with the currently installed tools. The DSN/PCB string tables still expose useful net and pin names.

### Strong Leads

- `BL_PWM`, `VLED+`, `VLED-`
  - Confirms that the reference B210 design has a dedicated backlight PWM/LED supply path.
  - This aligns with the R05/PWM2 discovery.
- `XGPIO_R5/PWM_2/UART2_RX/I2C2_SDA/USB_DBG_STATUS[0]`
  - Confirms R05 has a PWM2 alternate function.
- `XADC8B_IN`, `VBAT1`, `VBAT2`
  - Suggests battery/voltage measurement may be possible through ADC.
  - This could enable battery telemetry or low-battery throttling if the retail board wires it similarly.
- `USB0PP/USB0PN`, `USB1PP/USB1PN`, `USB0ID`, `USBVBUS`
  - Indicates two USB PHY paths and OTG/host-device possibilities on the chipset/reference board.
  - Retail SF2000 board exposure is unknown.
- `IR-SEN`, `IRRX`, `XGPIO_R7/IR`
  - Suggests IR receiver support may exist on the reference design.
- `SDIO_CLK/CMD/D0-D3`
  - Confirms SDIO-capable pins. Potentially useful for Wi-Fi/SDIO peripherals on other boards, not necessarily SF2000.
- `I2C1_SCL`, `I2C1_SDA`, `I2C2_*`, `I2C3_*`
  - Useful for probing hidden PMIC, charger, panel, touch, or audio devices if present.
- `HDMI_*`, `VOUT_*`, `VGA_*`
  - Reference design includes HDMI/VGA/VOUT-related nets. The SF2000 handheld may not populate these, but the display controller likely supports more outputs than the LCD.
- `VIN_*`
  - Video input/camera-style pins exist in the mux table. Probably not populated on SF2000, but relevant for chipset capability mapping.
- `SPDIF`, `I2S_*`, `SSI_*`
  - More audio/serial capabilities exist than the UniFrog currently uses.

### Pinmux Clues

Interesting alternate functions from the B210 strings:

- `R05`: `PWM_2`, `UART2_RX`, `I2C2_SDA`, `USB_DBG_STATUS[0]`
- `R08`: `UART2_TX`, `I2C2_SCL`, `USB_DBG_STATUS[1]`, `PRGB_R0`
- `R07`: `IR`, `TVE_VSYNC`
- `L23`: `PWM_0`, `I2S_DATAI`, `I2S_DATAO`, `IR`, `VIN_D6`, `VIN_D1`
- `L25`: `I2S_BCLK`, `SPDIF`, `VIN_CLK`
- `T15`: `PWM_2`, `UART2_TX`, `I2C3_SCL`, `PRGB_B0`, `I2C3_SDA`
- `T12`: `I2S_DATAO`, `PWM_2`, `EJ_TCLK`, `SDIO_D3`

Do not blindly repurpose these pins on the SF2000. Several are already used for LCD, buttons, storage, audio, or LEDs.

## Potential Next Experiments

### Power and Battery

- Probe ADC channels, especially around `XADC8B_IN`, to see whether battery voltage is readable.
- If battery voltage is available:
  - add a battery meter
  - add low-battery warning
  - dynamically select CPU clock/backlight level
  - throttle expensive emulator modes

### Backlight

- Keep 10 kHz polarity 1 as default.
- Add user-facing brightness config stored on SD when the logging/storage path is reliable enough.
- Check if very low PWM values save measurable power without display instability.

### CPU Performance Modes

- Keep boot-time profiles:
  - battery: lower SCPU clock and lower backlight
  - balanced: 810 MHz
  - performance: 918 MHz
- Runtime PLL switching is possible but risky. It needs a guarded implementation with:
  - restore-on-exit
  - no unsafe overclock values
  - immediate log flush before each test
  - long stability runs before using in normal builds

### GE-Assisted Frontend and Cores

- Use GE for:
  - framebuffer clears
  - menu fills
  - scaling 160x144, 240x160, 256x224, and 320x240 core outputs to LCD
- Evaluate whether GE can do useful color conversion or blending for libretro cores.
- Avoid CPU-side full-screen software scaling when possible.

### Hardware Video

- Keep the known working display mode as default.
- Add robust player stop/recovery for SD errors.
- Use a bounded streaming cache for media input instead of whole-file preload.
- Use hardware video decode for UniFrog previews or media playback. It is not directly useful for emulating game consoles, but it can offload UI/media features.

### Storage

- Keep 1-bit SD mode as a stability option.
- Add a storage reliability benchmark with retry/error counters.
- Avoid continuous logging to SD during performance-sensitive operation.

### Hidden Devices

- Probe I2C buses for devices, carefully and read-only first.
- Probe ADC channels and GPIO inputs.
- Inventory `/dev/*` and compare against DTS nodes.
- If USB host pins are physically exposed on related boards, test host mode separately from the handheld.

## Practical Defaults Today

- CPU: 810 MHz balanced, 918 MHz performance mode.
- ISA/toolchain: `mipsfrog` only if the target workload benefits; pure MIPS32r1 remains safest.
- Backlight: PWM2 at 20 kHz polarity 1 through `/dev/backlight`.
- SD: 1-bit mode when reliability matters, especially with extenders.
- Video: hardware decoder for media playback; GE 198 selector for scaling/fill.
- Audio: S16 interleaved PCM with 512-byte periods as the current low-latency
  default.
- Logging: memory buffer with manual flush.
