# Frontend

UniFrog now uses its own libretro frontend. It is heavily inspired by
MustardOS, commonly known as MuOS, especially in its menu structure and
`.muxthm` theme compatibility, but it is not MuOS and does not build or ship
MustardOS frontend source.

Build the device-testable frontend with:

```sh
make
```

The frontend is the only boot frontend. It boots through
`components/frontend/src/app/frontend_app.c` and renders through
`components/frontend/src/app/frontend_lvgl.c`. The launcher exposes Explore,
Collection, History, Apps, Info, Config, Reboot, and Shutdown entries. Apps
includes a native Media Player for media-only browsing and playback through the
HCRTOS/FFmpeg media stack. Explore is lazy directory browsing, so entering a
large system folder does not recursively scan the whole ROM tree.

Required external frontend input:

```text
.deps/support/lvgl       Standalone LVGL checkout for UniFrog UI rendering
```

MuOS is useful as a design and compatibility reference, but it is intentionally
not a build dependency. If a developer wants to compare behavior with upstream
MuOS, clone it outside the tracked source tree or under ignored `.deps` by
hand. Do not add MustardOS frontend files to the UniFrog build unless we make a
clear license and architecture decision to vendor or depend on them.

The frontend provides:

- LVGL display flush and tick callbacks backed by `unifrog_fb`/`unifrog_perf`.
- Menu input backed by `unifrog_input_menu_buttons()`.
- Path mapping to `/media/mmcblk0`, the configured ROM root, `/unifrog`, and `/unifrog_data`.
- Launch callbacks routed through `unifrog_frontend_launch_services`.
- Native media launch callbacks routed through the same launch-service table.
- Device services for battery, backlight, reboot, standby, storage recovery,
  and log flushing through public `include/unifrog/` headers.
- Compatibility with common MuOS `.muxthm` theme archives where practical.

ROM browsing is directory-based and lazy. The browser enters the configured ROM
root directly, shows immediate child directories as systems, and lists every
regular file inside those directories instead of building an index or filtering
by extension.

ROM settings are stored in `/unifrog_data/unifrog.ini` and can be edited by
hand or through the frontend:

- `rom_root=/path/to/roms`
- `rom_roots=/first/root|/second/root`
- `rom_root_label=ROMs`
- `rom_system=<folder>:<core-id>` (repeat once per folder override)

`<folder>` is the directory name under the ROM root. `rom_root_label` is the
name shown for the root folder itself. If a folder has no saved `rom_system`
entry, opening one of its ROMs uses the file association or shows the core
chooser. The same defaults can be edited
on-device through `Config -> General -> ROM Systems`, so users do not need a PC
to change their setup.

SF2000 uses `SELECT` as a neutral shortcut modifier in the frontend:
the `SELECT` modifier stays fixed, while the paired button for resume, log
flush, and screenshot shortcuts can be remapped in `Launch defaults -> Hotkeys`.
That keeps shortcuts predictable on the smaller SF2000 button set without
locking the user to one binding.

Credits should acknowledge MuOS as a major interface and theme-format
inspiration, without representing UniFrog as MuOS or requiring MuOS sources.
