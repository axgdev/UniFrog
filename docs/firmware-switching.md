# Firmware Switching

UniFrog switches to another ASD by writing a one-shot fastboot handoff record in
the reserved high-memory window, flushing it from cache, and resetting the
device. On the next boot, `bios/bisrv.asd` is the UniFrog fastboot image. Its
stage-1 loader reads the handoff path, loads the selected SD-relative `.asd`,
copies the payload to the stock load address, and jumps to it. The built-in
stock/UniFrog shortcuts use the v0.4.4-compatible `firmware/<name>.asd` layout;
the firmware browser can hand off any safe SD-relative `.asd` path.

Do not jump directly from the running UniFrog app into the resident stage-1
address. That was the misleading path during debugging: the handoff record was
valid, and quickly power-cycling after selecting stock firmware proved fastboot
could consume it, but an in-place jump from the live JS/HCRTOS runtime did not
enter stage-1 reliably. Treat reset into fastboot as part of the switch
contract.

Before reset, UniFrog turns off audio output and the backlight, suspends the
scheduler, disables interrupts, and then calls `reset()`. If switching breaks,
first check that the log contains `unifrog boot handoff path=...`; absence of
later UniFrog logs is expected when the selected stock firmware boots.

Stock firmware may still expect its support files in the stock `firmware/`
layout after the handoff. If a manually selected `.asd` from another folder
shows the stock logo and then stalls, retest the same firmware from
`/media/mmcblk0/firmware/` before changing the fastboot load address.
