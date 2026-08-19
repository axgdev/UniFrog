# Firmware Switching

UniFrog switches to another ASD by refreshing the small fastboot stage-1 loader
in its reserved high-memory window, writing a one-shot handoff record, flushing
both from cache, and transferring control to stage 1. Stage 1 reads the handoff
path, loads the selected SD-relative `.asd`, copies the payload to the stock
load address, and jumps to it. The built-in firmware browser can hand off any
safe SD-relative `.asd` path.

Power > Firmware Boot lists ASD files. Press A to boot one immediately or X to
make it the default. The selected SD-relative path is stored in the documented
`default_boot` setting and mirrored to `/firmware/boot_asd.cfg` for the small
pre-OS loader. Hold B during power-on to cancel that default for the current
boot and open UniFrog. Selecting "Default to UniFrog" removes the loader file.

Do not reset between creating the one-shot handoff and reading it. Device logs
showed that SDRAM reinitialization invalidated the record, so fastboot ignored
the requested firmware and loaded UniFrog again. UniFrog embeds the exact stage
1 built into `bios/bisrv.asd` and recopies it before every direct transfer; it
does not rely on old executable bytes remaining intact in reserved RAM.

Before the transfer, UniFrog flushes its logs, turns off audio output and the
backlight, suspends the scheduler, and disables interrupts. If switching
breaks, first check for `mode=direct` in the `unifrog boot handoff` log and for
`fastboot.stage1.handoff_result arg0=0` in the retained trace. Absence of later
UniFrog logs is expected when the selected stock firmware boots.

Stock firmware may still expect its support files in the stock `firmware/`
layout after the handoff. If a manually selected `.asd` from another folder
shows the stock logo and then stalls, retest the same firmware from
`/media/mmcblk0/firmware/` before changing the fastboot load address.
