# UniFrog Frontend Assets

`frontend/` now contains only assets used by native firmware paths:

```text
themes/  User-editable color themes and system icons
```

The boot frontend is native C/LVGL code in `src/native_frontend.c` and
`src/frontend_lvgl.c`. Optional standalone JS2300 diagnostics are packaged from
top-level `scripts/` into `/unifrog_data/scripts`.

The in-game quick menu is native code in `src/unifrog_libretro_host.c` and is
opened by the libretro host with `SELECT+START`.
