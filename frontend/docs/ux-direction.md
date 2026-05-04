# UniFrog Frontend UX Direction

The default UI should feel like a fast handheld console frontend, not a desktop
file manager compressed onto a small screen.

## First View

The first view is the main navigation surface:

- Continue
- Library
- Files
- Media
- Firmware
- Settings

The user can reach content quickly, but the design also leaves room for future
metadata, favorites, recently played games, and per-system views.

## Interaction Model

- D-pad moves focus.
- A opens or confirms.
- B backs out.
- START opens contextual actions.
- SELECT+START exits the active app/core when that combination is reserved by
  the runtime.

The frontend should use native input repeat handling rather than implementing
timing-heavy repeat logic in JavaScript once the binding exists.

## Rendering Model

The JavaScript UI describes layout and style. JS2300 batches drawing work into
native calls such as `video.rects()` and `video.text()` so the interpreter does
not become the frame bottleneck.

Themes should be plain JavaScript modules exporting color and text tokens.
Users can replace or extend them without recompiling.
