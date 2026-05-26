# UniFrog Frontend Assets

`frontend/` now contains only assets used by native firmware paths:

```text
quick-menu.js  In-game libretro quick menu loaded from firmware sources
themes/        User-editable color themes and system icons
```

The boot frontend is native C/LVGL code in `src/native_frontend.c` and
`src/frontend_lvgl.c`. Optional standalone JS2300 diagnostics are packaged from
top-level `scripts/` into `/unifrog_data/scripts`.

`quick-menu.js` is opened by the libretro host with `SELECT+START`. Its
shortcuts are `B` resume, `X` return to the frontend, `L` load state, and `R`
save state. It can also adjust fast-forward speed, frameskip, audio, display,
backlight, and CPU settings for the current game session.
