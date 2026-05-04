# SF2000 Backlight PWM Notes

This document captures the working SF2000 backlight path so it does not get
lost again. The important point is that `PINPAD_R05` is both the hard
backlight gate and the PWM-capable brightness pin.

## Hardware Contract

- `PINPAD_R05` as GPIO is only a hard gate:
  - GPIO low turns the panel backlight on.
  - GPIO high turns the panel backlight off.
- `PINPAD_R05` muxed as `PINMUX_R05_PWM_2` exposes the real brightness path as
  `/dev/pwm2`.
- The confirmed useful PWM profile is:
  - frequency: 10 kHz
  - period: 100000 ns
  - polarity: 1
  - duty: `period * level / 100`
- With that profile, brightness tracks duty directly:
  - `100` means full brightness.
  - `10` means a visibly dim 10 percent duty.
  - `1` is the lowest useful on level.
  - `0` should switch R05 back to GPIO and drive it high for hard off.

## Required DTS Shape

The SDK device tree must expose PWM2 and keep the backlight node wired to it:

```dts
pwm@2 {
    pinmux-active = <PINPAD_R05 1>;
    devpath = "/dev/pwm2";
    polarity = <1>;
    status = "okay";
};

backlight {
    backlight-gpios-rtos = <PINPAD_R05 GPIO_ACTIVE_LOW>;
    backlight-pwmdev = "/dev/pwm2";
    backlight-frequency = <10000>;
    brightness-levels = <0 1 2 4 6 8 10 12 16 20 25 32 40 50 64 80 100>;
    default-brightness-level = <16>;
    default-off;
    status = "okay";
};
```

## Working Runtime Sequence

Do not reconfigure R05 as GPIO after enabling nonzero brightness. That cancels
PWM control and leaves the display effectively full-on.

For nonzero brightness:

1. Clamp the requested level to `1..100`.
2. Call `pinmux_configure(PINPAD_R05, PINMUX_R05_PWM_2)`.
3. Open `/dev/pwm2` with `O_RDWR`.
4. Fill `struct pwm_info_s`:
   - `period_ns = 100000`
   - `duty_ns = period_ns * level / 100`
   - `polarity = true`
5. Call `PWMIOC_SETCHARACTERISTICS`.
6. Call `PWMIOC_START`.
7. Cache the requested level for UI readback.

For level `0`:

1. Call `pinmux_configure(PINPAD_R05, PINMUX_R05_GPIO)`.
2. Configure R05 as output.
3. Drive R05 high.

## UI Readback Rule

Do not feed quantized hardware readback back into the selected UI value after a
successful write. Earlier builds saw requests like `70` read back as `64`, and
the UI snapped the slider to `60`, making higher levels difficult to reach.

The UI should keep the requested value after success. Reading the current level
is useful only for initial synchronization.

## Diagnostics

Every brightness write should be cheap and safe to log. A healthy direct PWM
write looks like:

```text
unifrog backlight direct level=10 duty_ns=10000 period_ns=100000 polarity=1 ret_set=0 ret_start=0
js2300 backlight request=10 ret=0 current=10
```

If the panel brightness does not move:

- Confirm the log line says `direct`, not only `/dev/backlight`.
- Confirm `ret_set=0` and `ret_start=0`.
- Confirm `duty_ns` matches `level * 1000` for the 10 kHz profile.
- Confirm no later code switches `PINPAD_R05` back to GPIO for nonzero levels.
- Confirm the SDK FAT append fix is present; otherwise repeated flushes may
  overwrite the earlier diagnostic lines and make the test look inconsistent.

## Known False Leads

- `/dev/backlight` can report success while the visible panel remains full-on
  if the driver reconfigures R05 back to GPIO after programming PWM.
- GPIO writes alone cannot dim the panel; they are only hard on/off control.
- Quantized brightness levels are expected in the kernel backlight table, but
  UniFrog's user-facing levels should remain the requested values.
