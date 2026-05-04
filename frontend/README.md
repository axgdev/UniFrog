# UniFrogFrontend

UniFrogFrontend is the editable JavaScript interface for UniFrog. It runs on
JS2300 and calls native UniFrog bindings for the work that must stay fast:
input, rendering, audio, content scanning, libretro launching, firmware
handoff, battery, standby, and logging.

The goal is to make the device feel polished by default while keeping the UI
easy for users to modify with normal JavaScript files on the SD card.

## Principles

- The first screen is the usable frontend, not a landing page.
- Navigation must be fast on a D-pad and readable on a 320x240 display.
- JavaScript owns state, layout decisions, themes, and user customization.
- C owns hardware, batching, caches, and emulator/media hot paths.
- Every expensive UI operation should have a batched native call available.
- The frontend should recover cleanly if a theme, plugin, or user script fails.

## Repository Layout

```text
.
|-- app/                 Default UniFrog JavaScript frontend
|-- themes/              User-editable visual themes
|-- docs/                UX and binding notes
`-- Makefile             Packaging and validation helpers
```

## SD Card Layout

The intended installed layout is:

```text
/unifrog/main.js
/unifrog/app/*.js       optional modules for later frontend versions
/unifrog/themes/        colors, icon PNGs, and optional JS themes
/unifrog/themes/*.ini   color-theme examples for non-JS customization
/unifrog/scripts/*.js   developer and smoke-test scripts
/unifrog/user/          optional user overrides
```

Runtime settings are stored in `/unifrog/settings.ini`. Users can override the
default colors without editing JavaScript by placing `/unifrog/theme.ini` on the
SD card using the same key names shown in `themes/default.ini`. A theme may also
set `font=/media/mmcblk0/unifrog/font.ufnt` to load a fixed 5x7 bitmap font.
Font files are text files with one printable ASCII glyph per line:
`A=7e 11 11 11 7e`, where the five hex bytes are the glyph columns.

The default theme also supports icon keys such as `icon_gba`,
`icon_snes`, `icon_media`, and `icon_settings`. Values can be absolute paths or
paths relative to `icon_root`. PNG icons should be small RGBA or RGB files; the
default generated set is stored under `/unifrog/themes/system-icons/icons`.

The firmware should boot JS2300 with `/unifrog/main.js` as the entry script.
The first hardware-test version keeps `main.js` self-contained because the
initial embedded MQuickJS integration evaluates one script directly.

The default package includes `/unifrog/scripts/smoke-test.js`. Run it from
Developer -> Smoke test or Developer -> Scripts to exercise the JS2300 video,
input, filesystem, backlight, battery, packaged-core, log-flush, and timing
bindings on the device. It writes `/unifrog/smoke-test-result.txt` and emits
`SMOKE ...` lines to `/log.txt`.

## Development

This project avoids transpilation for normal UI edits. `make check` builds a
local `mqjs` from the configured MQuickJS checkout and compiles every shipped
JavaScript file to 32-bit MQuickJS bytecode. This catches parser failures with
the same JavaScript implementation used by JS2300 on the device.

Current MQuickJS syntax guidance:

- Use `var` and function declarations.
- Avoid `import`/`export`, `const`, `let`, arrow functions, template literals,
  object shorthand, spread syntax, and trailing commas until the target
  MQuickJS parser accepts them.
- Keep module files self-contained for now; the first JS2300 integration
  evaluates `/unifrog/main.js` directly.

The default MQuickJS checkout path is the firmware repository's `mquickjs`
submodule. Override it with `make MQUICKJS_DIR=/path/to/mquickjs check` when
needed.
