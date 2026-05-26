# Native Frontend

UniFrog now uses its own native libretro frontend. It is heavily inspired by
MustardOS, commonly known as MuOS, especially in its menu structure and
`.muxthm` theme compatibility, but it is not MuOS and does not build or ship
MustardOS frontend source.

Build the device-testable frontend with:

```sh
make
```

`FRONTEND_IMPL=native` is the only boot frontend. It boots through
`src/native_frontend.c` and renders through `src/frontend_lvgl.c`. The launcher
exposes Explore, Collection, History, Apps, Info, Config, Reboot, and Shutdown
entries. Apps includes a native Media Player for media-only browsing and
playback through the HCRTOS/FFmpeg media stack. Explore is lazy directory
browsing, so entering a large system folder does not recursively scan the whole
ROM tree.

Required external frontend input:

```text
.deps/support/lvgl       Standalone LVGL checkout for UniFrog UI rendering
```

MuOS is useful as a design and compatibility reference, but it is intentionally
not a build dependency. If a developer wants to compare behavior with upstream
MuOS, clone it outside the tracked source tree or under ignored `.deps` by
hand. Do not add MustardOS frontend files to the UniFrog build unless we make a
clear license and architecture decision to vendor or depend on them.

The native frontend provides:

- LVGL display flush and tick callbacks backed by `unifrog_fb`/`unifrog_perf`.
- Menu input backed by `unifrog_input_menu_buttons()`.
- Path mapping to `/media/mmcblk0`, the configured ROM root, `/unifrog`, and `/unifrog_data`.
- Launch callbacks that call `unifrog_libretro_run_path_ex()`.
- Native media launch callbacks that call `unifrog_media_play_video_ex()`.
- Device services for battery, backlight, reboot, standby, storage recovery,
  and log flushing through public `include/unifrog/` headers.
- Compatibility with common MuOS `.muxthm` theme archives where practical.

ROM browsing is directory-based and lazy. The browser enters the configured ROM
root directly, shows immediate child directories as systems, and lists every
regular file inside those directories instead of building an index or filtering
by extension.

The ROM settings are stored in `UNIFROG_SETTINGS_PATH` and can be edited by
hand:

- `rom_root=/path/to/roms`
- `rom_root_label=ROMs`
- `rom_system.<system>=<display label>|<core-id-1>,<core-id-2>`

`<system>` is the directory name under the ROM root. `rom_root_label` is the
name shown for the root folder itself. If a system has no saved
`rom_system.<system>` entry, opening one of its ROMs shows the core chooser and
the selected core becomes that system's default. The same defaults can be edited
on-device through `Config -> General -> ROM Systems`, so users do not need a PC
to change their setup.

SF2000 uses `SELECT` as a neutral shortcut modifier in the native frontend:
the `SELECT` modifier stays fixed, while the paired button for resume, log
flush, and screenshot shortcuts can be remapped in `Launch defaults -> Hotkeys`.
That keeps shortcuts predictable on the smaller SF2000 button set without
locking the user to one binding.

Credits should acknowledge MuOS as a major interface and theme-format
inspiration, without representing UniFrog as MuOS or requiring MuOS sources.
