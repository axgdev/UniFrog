# UniFrog Frontend

`frontend/` packages the editable JavaScript UI installed on the SD card under
`/unifrog`. It runs on JS2300 and calls native bindings for input, rendering,
audio, content launch, battery, standby, firmware handoff, and logging.

## Layout

```text
app/        JavaScript UI files
main.js     Small loader that loads app modules
scripts/    Device smoke-test scripts
themes/     User-editable color themes and icons
Makefile    Syntax checks and package assembly
```

Installed SD-card layout:

```text
/unifrog/main.js
/unifrog/app/*.js
/unifrog/scripts/*.js
/unifrog/themes/*.ini
/unifrog/themes/system-icons/icons/*.png
/unifrog/user/
```

`main.js` loads `app/theme.js`, `app/constants.js`, `app/catalog.js`, and
`app/app.js` in order. Put UI behavior in `app/app.js`, editable menu data and
core lists in `app/catalog.js`, and default visual values in `app/theme.js`.
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

The packaged smoke test is `/unifrog/scripts/smoke-test.js`. Run it from the
Developer UI to exercise video, input, filesystem, backlight, battery,
packaged-core, log-flush, and timing bindings. It writes
`/unifrog/smoke-test-result.txt` and `SMOKE ...` lines to `/log.txt`.
