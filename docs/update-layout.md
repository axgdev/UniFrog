# Update Layout

UniFrog keeps the SD-card runtime layout stable so firmware packages can be
installed without deleting user data.

## Live Layout

- `bios/bisrv.asd`: fastboot image installed as the stock boot payload.
- `unifrog/firmware/unifrog.bin`: active UniFrog firmware loaded by fastboot.
- `unifrog/cores`: native core modules for the active build.
- `unifrog/modules`: optional runtime modules for the active build.
- `unifrog/manifest.ini`: active build identity and dependency commits.
- `unifrog_data`: user-owned state and extensions. This includes saves, logs,
  cache, scripts, custom themes, custom languages, update zips, installed
  version slots, and custom firmware ASD files.

The `unifrog` directory is distribution-managed. It should be safe to replace it
with the `unifrog` directory from a release zip. The `unifrog_data` directory is
the user's durable area and must not be deleted by update activation.

## Update Packages

An update zip is the same root layout produced by `make sd-zip`:

- `bios/bisrv.asd`
- `unifrog/firmware/unifrog.bin`
- `unifrog/manifest.ini`
- `unifrog/cores/...`
- optional `unifrog/modules/...`
- optional empty `unifrog_data/...` directories for first-install discovery

Users put update zips in `unifrog_data/updates`. The frontend installs them
into `unifrog_data/versions/<zip-name>/` first, then validates that the fastboot
image, firmware image, and manifest exist. Installing a zip does not change the
active system.

Apps -> Package Check runs the same validation on device. It checks the live
layout, rejects update zips that would write mutable files into
`unifrog_data`, and verifies installed core module headers against the running
UniFrog ABI.

## Version Switching

Apps -> Updates lists installed slots from `unifrog_data/versions`. Activating a
slot copies its boot files and managed runtime files into the live layout,
writes `unifrog_data/active-version.ini`, flushes logs, and reboots.

Activation is marker based:

- `unifrog_data/pending-activation.ini`: written before live files are copied.
- `unifrog_data/active-version.ini`: written after the live layout is updated.
- `unifrog_data/boot-ok.ini`: written when the frontend reaches the UI on
  the next boot.

If the device crashes during activation, the pending marker remains. If an
activated slot fails before the frontend starts, `boot-ok.ini` is missing. The
Info -> Diagnostics and Apps -> Package Check screens expose those markers so a
user can report the exact activation state without host tools.

Activation intentionally preserves all of `unifrog_data`. That makes rollback
practical: install several zips, activate the one to test, and activate an older
slot if the new one regresses.

There is no RTC on the device, so slots are named from the zip filename rather
than timestamps. Release zips should therefore use sortable names such as
`unifrog-0.4.4.zip` or `unifrog-2026w20.zip`.
