# Source Layout

UniFrog is a modular monorepo. Public APIs remain under `include/unifrog/`,
while implementation ownership is explicit:

```text
apps/                    Firmware, Linux, and fastboot entry composition
foundation/src/          Hardware and reusable runtime mechanisms
components/frontend/     Device UI, settings, themes, and interaction
components/libretro/     Emulation session and content engine
components/media/        Media decoding and playback engine
components/diagnostics/  Device test algorithms and reports
js2300/                  JS runtime, UniFrog host, core, and scripts
```

## Foundation

`foundation/src/` owns ABI, archive handling, shared content classification,
device detection, portable display primitives, low-level module loading,
platform drivers, runtime services, and storage primitives. Foundation must
not import a component.

Keep board-specific details under `foundation/src/platform/sf2000/`. Generic
code should use public `unifrog_*` services instead of duplicating hardware
rules. Linux implementations of those services live under
`foundation/src/platform/linux/`. Fastboot handoff policy stays in
`runtime/`; the SF2000 reset and stage1 handoff implementation lives in the
platform backend.

Audio routing and framebuffer/graphics-engine device access are platform
backends. Their SF2000 implementations live under the corresponding
`platform/sf2000/audio/` and `platform/sf2000/display/` directories; reusable
pixel, image, presenter, surface, text, and UI code remains in `display/`.

## Components

`components/frontend/src/app/` owns the launcher, browser, settings, themes,
and LVGL interpretation. `components/frontend/src/libretro_frontend/` owns
in-game libretro frontend presentation such as the quick menu. Keep app flow
and view dispatch in `frontend_app.c`; put reusable helpers in named units such
as `frontend_content.c`, `frontend_items.c`, `frontend_paths.c`,
`frontend_browser.c`, `frontend_rom_roots.c`, `frontend_history.c`,
`frontend_favorites.c`, `frontend_labels.c`, `frontend_navigation.c`,
`frontend_draw.c`, `frontend_text.c`, and
`frontend_services.c`, and `frontend_progress.c`. The document reader UI is
frontend-owned under `components/frontend/src/reader/`; shared reader file-type classification lives
in foundation. Host simulation uses the
same menu model and controller through injected action services. Launch and
diagnostic side effects leave the frontend through
`unifrog_frontend_launch_services` instead of direct component calls.

`components/libretro/src/` owns core loading, session execution, callbacks,
content decompression, save data, states, pacing, and watchdog behavior. It
must not import frontend presentation. `libretro_session.h` is the narrow
engine interface consumed by frontend-owned in-game presentation.

`components/media/src/` owns native playback, FFmpeg/HCRTOS integration,
buffered IO, seeking, and direct audio paths. It must not import frontend
presentation.

Media tuning, content classification, and buffering policy are portable units
at the component root and are shared by Linux, the frontend, and firmware.
HCRTOS decoding, device-node IO, direct audio, and GB300 diagnostics live in
`components/media/src/platform/sf2000/`.

`components/diagnostics/src/` owns developer-facing test algorithms and report
generation.

`js2300/src/unifrog_host/` owns standalone UniFrog bindings, while
`js2300/src/libretro_core/` owns the JS2300 libretro core. Generic VM code in
`js2300/src/js2300_runtime.c` remains independent of UniFrog.

## Apps

`apps/firmware/src/main.c` composes the firmware application.
`apps/linux/src/main.c` composes the Linux runner from the same
platform-neutral frontend and policy components.
`apps/fastboot/src/` owns the minimal fastboot stages and handoff code.

## Refactor Rules

- Do not add implementation catch-all files at repository root.
- Keep public headers small and private details beside their owner.
- Foundation cannot depend on components.
- Libretro and media engines cannot depend on frontend code.
- Keep mechanical moves separate from behavior changes.
- Run `make quick-check` after focused edits and `make verify` before handoff.
