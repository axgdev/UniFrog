# UniFrog Frontend Assets

This directory contains assets used by frontend paths:

```text
themes/  User-editable color themes and system icons
```

The boot frontend is native C/LVGL code under
`components/frontend/src/app/`.
Optional standalone JS2300 diagnostics are packaged from `js2300/scripts/`
into `/unifrog_data/scripts`.

The in-game quick menu is frontend-owned native code and is opened by the
libretro host with `SELECT+START`.
