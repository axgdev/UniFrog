# Wireless Controller Status

UniFrog now brings up the wireless RF path with the stock-traced
L27/L28/L29 bus during application startup, before framebuffer setup. The old
exploratory fallback probes have been removed from the production path now that
the stock-compatible sequence is confirmed working.

Current stock-trace alignment:

- Replays the stock full-init setup shadow before the RF `0x53`/`0x25`
  self-test, including the transient `r54=0x3fc085b3` and `r58=0x000004fe`
  state seen in the stock trace.
- Keeps the stock `0x53=0x5a`, `0x53=0xa5`, `0x25=0xa5`, read `0x05`
  sequence and delays together without a preliminary `0x05` read.
- Mirrors the stock RX reset tail by clearing `0xfd` after `0x20=0x8f`.
- Runs the radio configuration immediately after a successful self-test.
- Links the local DTS by default so I2C0 does not claim L28/L29 before the RF
  bit-banged bus is initialized.
- Decodes the controller port from the RF status RX-pipe bits. Hardware log
  `loghcrtos32.txt` shows player 1 packets as `status=0x40` and the player 2
  switch as `status=0x42` with raw bit `0x8000` set.
- ORs both decoded wireless ports into the UniFrog menu button state so either
  paired controller can navigate the native menu.
- Exposes reusable helpers for availability, per-port button state, combined
  button state, and state reset.
- Manual diagnostics run only the stock-hold/fixed-channel receive checks.

## Shared Local/Wireless Bus

SF2000 local buttons and the RF wireless controller both touch the L23-L29 GPIO
group. The local keypad uses L23 as serial data and L24 as clock. The wireless
bit-bang path temporarily owns the wider L23-L29 group while polling the RF chip,
then must return the local keypad lines to GPIO input/output mode.

The production poll path samples local buttons before RF polling, polls wireless,
restores the local bus, samples local buttons again, and debounces the merged
local result once. Debouncing twice around an RF poll can create alternating
pressed/released local states while the shared GPIO group is settling.

Both physical and wireless controllers are valid inputs for port 1 and the menu.
Wireless P1/P2 selection must not hide the physical controls. During gameplay,
`retro_input_state(port=0)` reports the physical buttons OR wireless P1, and
`port=1` reports the physical buttons OR wireless P2. This deliberately lets the
physical controls keep working even if the RF controller is set to player 2.

Transition diagnostics are intentionally bounded:

- `unifrog input transition` logs the raw local scan after core/frontend
  transitions.
- `unifrog input local_bus` logs the L23-L29 pinmux bytes, GPIO control words,
  and live pin input levels at transition boundaries.
- `unifrog input sources` logs local, wireless, combined, and menu masks with
  pre/post-RF local raw scans.
- If wireless input stays active while local input remains zero, a low-frequency
  `wireless_active_local_zero` local-bus snapshot is emitted. This should show
  whether local scanning is truly gone at the GPIO layer or whether normalization
  or debounce is suppressing it.

If a device log shows the firmware build commit and the libretro build commit do
not match, first check the module ABI line. External core modules are expected
to keep working across releases when their logged required ABI version and table
size fit the running firmware. Treat a binary as stale only when the loader
reports `bad_abi`, `bad_exports`, or behavior clearly depends on a newer
module-support helper.

The first confirmed receive log after the stock reset-tail fix was
`loghcrtos31.txt`: RF self-test passed as `stock_full_init`, `diag_after_init`
reported `stock_delta=0x00000000`, receive-ready `0x40` appeared repeatedly,
the stock-hold and final-poll diagnostics both reported `got_packet=1`, and all
four fixed stock channels received packets.

`loghcrtos32.txt` confirmed normal menu navigation works from wireless input and
corrected the player selector interpretation: the payload byte bit previously
used for P1/P2 stayed in the P1 state, while RF status changed from `0x40` to
`0x42` and raw bit `0x8000` appeared when switching to player 2.

`loghcrtos33.txt` confirmed both decoded players work in the menu: P2 logs as
`status=0x42` with raw bit `0x8000`, and P1 logs as `status=0x40`.
