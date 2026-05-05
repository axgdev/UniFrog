# UniFrog Frontend

`frontend/` packages the editable JavaScript UI installed on the SD card under
`/unifrog`. It runs on JS2300 and calls native bindings for input, rendering,
audio, content launch, battery, standby, firmware handoff, and logging.

## Layout

```text
app/        JavaScript UI files
main.js     Small loader that loads app modules
quick-menu.js  In-game paused quick menu
scripts/    Device smoke and performance diagnostics
themes/     User-editable color themes and icons
Makefile    Syntax checks and package assembly
```

Installed SD-card layout:

```text
/unifrog/main.js
/unifrog/quick-menu.js
/unifrog/*.js.mqbc
/unifrog/bytecode-manifest.txt
/unifrog/app/*.js
/unifrog/app/*.js.mqbc
/unifrog/scripts/*.js
/unifrog/scripts/*.js.mqbc
/unifrog/themes/*.ini
/unifrog/themes/system-icons/icons/*.png
/unifrog/user/
```

`main.js` is a literal loader. It loads default theme data, constants, catalog
data, helpers, screen rendering, action handling, then `app/app.js`, which owns
frontend state and the runtime loop. Put screen behavior in the focused
`app/*.js` module that matches it; put editable menu data and core lists in
`app/catalog.js`, and default visual values in `app/theme.js`.
Runtime settings live in `/unifrog/settings.ini`. Users can override colors by
placing `/unifrog/theme.ini` on the SD card with the same keys as
`themes/default.ini`.

Themes may set `font=/media/mmcblk0/unifrog/font.ufnt` for a fixed 5x7 bitmap
font. Font files are text files with one printable ASCII glyph per line:

```text
A=7e 11 11 11 7e
```

Icon keys such as `icon_gba`, `icon_snes`, `icon_media`, and `icon_settings`
accept absolute paths or paths relative to `icon_root`.

`quick-menu.js` is the in-game JavaScript pause menu opened with
`SELECT+START`. Its shortcuts are `B` resume, `X` return to the frontend,
`L` load state, and `R` save state.

## Development

Normal UI edits do not use transpilation or bundling. JS2300 supports
`load("relative/path.js")` relative to `/unifrog`, and `make check` compiles
every shipped JavaScript file to 32-bit MQuickJS bytecode.

```sh
make -C frontend help
make -C frontend print-config
make -C frontend check
make -C frontend package
```

`make package` stages both source and `.mqbc` bytecode. The generated
`bytecode-manifest.txt` stores source and bytecode fingerprints, because device
timestamps are not reliable. JS2300 executes bytecode only when the manifest
matches the current bytes; otherwise it falls back to source and logs why.
Entry scripts and literal `load()` dependencies are bytecode-preloaded before
the JS API is attached, so the modular files stay fast without bundling.
The home screen is drawn before the library index is parsed; the index loads
immediately after first paint, or synchronously if the user opens a system list
before that background step completes.

The direct frontend default is `../.deps/mquickjs`, which is the same checkout
the root Makefile fetches as `.deps/mquickjs`. Override it when needed:

```sh
make -C frontend MQUICKJS_DIR=/path/to/mquickjs check
```

Current MQuickJS syntax guidance:

- Use `var` and function declarations.
- Avoid `import`/`export`, `const`, `let`, arrow functions, template literals,
  object shorthand, spread syntax, and trailing commas.
- Keep shipped files compatible with the parser used by JS2300 on device.

Packaged diagnostics live under `/unifrog/scripts`. Run them from the Developer
UI:

- `smoke-test.js` exercises video, input, filesystem, backlight, battery,
  packaged-core, log-flush, bytecode packaging, and timing bindings.
- `perf-test.js` records JS screen draw, list, read, and module-load timings.
- Developer -> Storage test runs the native quick SD profile sweep, prefers
  `/ROMS/test.md` when present, and writes `/unifrog/storage-test-result.txt`.

They write `/unifrog/smoke-test-result.txt` or
`/unifrog/perf-test-result.txt` and log `SMOKE ...` or `PERF ...` lines.
