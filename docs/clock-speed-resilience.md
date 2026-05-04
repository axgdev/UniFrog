# Clock-Speed Resilience

UniFrog can change the HC1512 SCPU clock at runtime before launching a core and
from the in-game quick menu. Code that touches hardware must therefore not
assume the boot-time `CONFIG_CPU_CLOCK_HZ` still matches the real CPU clock.

## Failure Pattern

The physical SF2000 controls worked in the JavaScript frontend at the boot
clock, then stopped once libretro cores ran at the 918 MHz profile. Logs showed
wireless input still changing, but local raw scans stayed zero. The local
keypad bus and pinmux were still configured correctly, so the failure was not
player priority or a bad input map.

The actual issue was timing. The SF2000 keypad is read through a shift
register on `L23`/`L24`, and GB300 uses the same class of shift-register scan
on `L25`/`L26`/`L27`. The original scanner relied on short `usleep()` delays
and incidental instruction overhead. In this SDK, sub-millisecond `usleep()`
uses CP0 Count with the build-time CPU clock constant, so runtime SCPU changes
can make hardware pulse widths too short.

## Rule

Do not use CPU-cycle spin loops or sub-millisecond SDK `usleep()` as a hardware
protocol delay after runtime clock switching is enabled.

For short hardware delays, use:

```c
unifrog_perf_delay_us(usec);
```

That helper reads the current SCPU clock registers and waits using the current
CP0 Count rate. It is safe in short IRQ-disabled bit-bang sections because it
does not call the scheduler.

For millisecond-scale sleeps or pacing, use:

```c
unifrog_perf_time_us();
unifrog_perf_time_ms();
msleep(ms);
vTaskDelay(...);
```

Those paths are based on the RTOS tick and do not depend on the current SCPU
profile. The JavaScript `JS2300.now()` binding and libretro pacer use this
RTOS-tick path.

## Current Hardened Paths

- SF2000 local keypad scan:
  - explicit load delay
  - explicit settle delay after switching data back to input
  - explicit clock-low and clock-high pulse widths
- GB300 local keypad scan:
  - same explicit load, settle, low, and high timing contract
- Wireless RF register bit-banging:
  - uses `unifrog_perf_delay_us()` instead of a CPU-cycle spin count
- JavaScript frontend clock:
  - `JS2300.now()` uses the RTOS tick, not fixed CP0 scaling
- Libretro frame pacing:
  - real-time pacing uses the shared RTOS-tick helper
- Wireless diagnostics:
  - receive-window duration uses RTOS milliseconds instead of a fixed 810 MHz
    CP0 count multiplier

## Diagnostics To Check

When a speed-sensitive bug is suspected, check `/log.txt` for:

- `unifrog build commit=...` and `unifrog libretro build commit=...`
- `unifrog libretro scpu after target=... current=...`
- `unifrog input sources ... raw0=... raw1=... norm0=... norm1=...`
- `unifrog boot_time ...`
- `frontend ready input_ms=...`

If raw local input works before a clock change and then stays zero after the
clock change while pinmux and GPIO direction are unchanged, suspect pulse
timing before input mapping. If real-time UI or pacing gets too fast or too
slow after a clock change, suspect fixed CP0 scaling before application logic.

## SCPU Range Policy

The UI exposes the guarded range that has been useful on hardware:

- fixed selectors: 198, 297, 396, and 594 MHz
- digital PLL profiles: 702, 756, 810, 864, and 918 MHz

The current ceiling remains 918 MHz. Values above that are intentionally not
accepted by the generic launch options or quick menu because earlier probing
showed aggressive overclocking can freeze the device.
