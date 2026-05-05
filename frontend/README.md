# UniFrog Frontend

`frontend/` packages the editable JavaScript UI installed on the SD card under
`/unifrog`. It runs on JS2300 and calls native bindings for input, rendering,
audio, content launch, battery, standby, firmware handoff, and logging.

## Layout

```text
app/        JavaScript UI files
defaults/   Packaged default .opt files
locales/    Packaged translation string files
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
/unifrog/defaults/frontend.opt
/unifrog/locales/*.ini
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

Packaged defaults live under `/unifrog/defaults`. User-owned overrides belong
under `/unifrog/user`; the root install target replaces packaged app, default,
theme, script, core, and module directories, but preserves `/unifrog/user` and
runtime state files during upgrades.

Frontend options use `.opt` files with the same comment shape used by SF2000
multicore core options:

```text
### [rom_roots] :[/ROMS|/] :[/ROMS|/|/ROMS]
rom_roots=/ROMS|/
```

`/unifrog/defaults/frontend.opt` provides shipped defaults.
`/unifrog/user/frontend.opt` overrides those defaults. Runtime `settings.ini`
can still hold recent session state such as the last launched game, but every
user-facing frontend setting is also read from and written back to
`/unifrog/user/frontend.opt`. The on-device settings menu is split into
Display, Launch, Library, System, and Tools sections so the `.opt` file can be
edited either on device or on a computer.

ROM discovery is based on folder structure, not extension guessing. The
default `rom_roots=/ROMS|/` scans system folders directly under `/ROMS` and
directly under the SD root. For example, `/ROMS/gba`, `/ROMS/psx`, `/gba`, and
`/psx` are valid. Recognized folder aliases are case-insensitive and follow
common retro-handheld names, including `gba`, `gb`, `gbc`, `nes`, `fc`, `snes`,
`sfc`, `genesis`, `megadrive`, `md`, `sms`, `gg`, `pce`, `pcengine`, `tg16`,
`psx`, `ps`, and `ps1`. Every non-hidden file inside a recognized game folder
is indexed as a game for that system; PlayStation `.bin` tracks are hidden
when they are inside a PlayStation folder.

Translation files under `/unifrog/locales` are packaged as key/value `.ini`
files for English, Simplified Chinese, Hindi, Spanish, French, Arabic,
Bengali, Portuguese, Russian, Urdu, Indonesian, German, Japanese, Swahili,
Marathi, Telugu, Turkish, Tamil, Vietnamese, and Korean.

Themes use `font=/media/mmcblk0/unifrog/fonts/NotoSans-Regular.ttf` by
default. The native renderer reads UTF-8 text and renders glyphs on demand from
TTF/OTF files through a small cache. A theme may provide a semicolon-separated
fallback list, for example:

```text
font=/media/mmcblk0/unifrog/fonts/NotoSans-Regular.ttf;/media/mmcblk0/unifrog/fonts/NotoSansArabic-Regular.ttf
```

Themes may still set `font=/media/mmcblk0/unifrog/font.ufnt` for a fixed 5x7
bitmap font. Font files with `.ufnt` are text files with one printable ASCII
glyph per line:

```text
A=7e 11 11 11 7e
```

Icon keys such as `icon_gba`, `icon_snes`, `icon_media`, and `icon_settings`
accept absolute paths or paths relative to `icon_root`.

`make deps-fonts` fetches permissively licensed Noto fonts into `.deps/fonts`;
`make -C frontend package` includes them as `/unifrog/fonts` when present. The
Japanese and Simplified Chinese Noto CJK files are subsetted at package time to
the glyphs used by shipped UI strings and labels, keeping the SD package small
while preserving the full source fonts in `.deps/fonts`. The package also ships
Android's Apache-2.0 `DroidSansFallback.ttf` as a smaller full CJK option for
ROM filenames that use glyphs outside the UI subsets. The default theme loads
the Latin/Greek/Cyrillic Noto Sans file only to keep normal startup memory low.
Users and language-specific themes can add Arabic, Devanagari, Bengali, Tamil,
Telugu, Japanese UI, CJK UI, or CJK full fallback files when they need those
scripts.

System icons live under `/unifrog/themes/system-icons/icons`. The packaged
library subdirectory contains optimized cropped console images from the source
sheets under `assets/`, including systems UniFrog does not launch yet. The
currently supported systems map to cropped art from that library and can still
be overridden by changing the `icon_*` keys in a theme.

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
  `/ROMS/test.md` when present, shows the risky SD stage on screen, and writes
  `/unifrog/storage-test-result.txt`.
- Developer -> Storage full test reads `/ROMS/probes/test*.md` across runtime
  SD profiles and checkpoints `/unifrog/storage-full-test-result.txt` after
  each return to safe mode.
- Developer -> Storage mode test selects one SD profile, switches once, reads
  all probes, then restores safe mode and writes
  `/unifrog/storage-mode-test-result.txt`.

The scripts write `/unifrog/smoke-test-result.txt` or
`/unifrog/perf-test-result.txt` and log `SMOKE ...` or `PERF ...` lines.
