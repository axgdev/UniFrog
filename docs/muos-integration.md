# MuOS-Inspired Frontend

UniFrog no longer clones or builds against the upstream MuOS frontend source.
The device frontend is native UniFrog code that can read MuOS-style theme
archives and language files.

Fetch the current pinned inputs with:

```sh
make deps
```

Build the device-testable native frontend with:

```sh
make
```

`FRONTEND_IMPL=native` is the default boot frontend. The launcher exposes
Explore, Collection, History, Apps, Info, Config, Reboot, and Shutdown entries.
Explore is lazy directory browsing, so entering a large system folder does not
recursively scan the whole ROM tree.

This creates:

```text
.deps/support/lvgl       Standalone LVGL checkout for UniFrog UI support
```

Local frontend integration code lives in UniFrog. The frontend layer provides:

- LVGL display flush and tick callbacks backed by `unifrog_fb`/`unifrog_perf`.
- Menu input events backed by `unifrog_input_menu_buttons()`.
- Path mapping from MuOS locations to `/media/mmcblk0`, `/ROMS`, and
  `/unifrog`.
- Launch callbacks that call `unifrog_libretro_run_path_ex()`.
- Device services for battery, backlight, reboot, standby, storage recovery,
  and log flushing through public `include/unifrog/` headers.

SF2000 uses `SELECT` as a neutral shortcut modifier in the native frontend:
`SELECT+A` launches the last game, `SELECT+X` queues a screenshot log, and
`SELECT+Y` force-flushes logs. This avoids conflicting with upstream MuOS
module buttons such as `A`, `B`, `X`, `Y`, `L1`, and `R1` while fitting the
smaller SF2000 button set.
