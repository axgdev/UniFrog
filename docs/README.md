# Documentation Map

Start with the repository root `README.md` for build commands and layout.
Use `AGENTS.md` for agent-specific build and editing rules.
Use `../THIRD_PARTY.md` for third-party source layout, attribution, and
binary package notice requirements.

Current component docs:

- `../cores/README.md`: libretro source workflow, patches, and core ABI rules
- `../js2300/README.md`: MQuickJS embedding layer
- `js2300-scripting.md`: optional JS2300 script contract
- `muos-integration.md`: upstream MuOS/LVGL checkout and shim policy
- `../THIRD_PARTY.md`: third-party license and source inventory
- `unifrog-architecture.md`: current firmware/runtime/module boundaries
- `external-core-loading.md`: SD-loaded module format and loader details
- `memory-layout.md`: linker and arena memory map
- `performance-api.md`: CP0/cache/timing support
- `link-layout-diagnostics.md`: map-file and no-load reservation checks
- `libretro-core-quirks.md`: per-core integration notes
- `firmware-switching.md`: reset-mediated fastboot handoff
- `update-layout.md`: SD-card update packages and version slots
- `clock-speed-resilience.md`: CPU/GE clock behavior
- `sf2000-hardware-findings.md`: board-level hardware findings
- `sf2000-backlight-pwm.md`: backlight PWM notes
- `audio-quirks.md`: SF2000 audio gate/mute behavior and buzz diagnostics
- `filesystem-mmc-notes.md`: confirmed FatFs/MMC/logging behavior
- `media-video-freeze-handoff.md`: native MP4/H.264 freeze signature and fix path
- `av-output.md`: AV output notes
- `WIRELESS_STATUS.md`: wireless controller investigation status
