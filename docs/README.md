# Documentation Map

Start with the repository root `README.md` for build commands and layout.
Use `AGENTS.md` for agent-specific build and editing rules.
Use `../THIRD_PARTY.md` for third-party source layout, attribution, and
binary package notice requirements.

Current component docs:

- `../cores/README.md`: libretro source workflow, patches, and core ABI rules
- `../frontend/README.md`: SD-card JavaScript frontend package
- `../js2300/README.md`: MQuickJS embedding layer
- `../THIRD_PARTY.md`: third-party license and source inventory
- `unifrog-architecture.md`: current firmware/runtime/module boundaries
- `external-core-loading.md`: SD-loaded module format and loader details
- `memory-layout.md`: linker and arena memory map
- `performance-api.md`: CP0/cache/timing support
- `link-layout-diagnostics.md`: map-file and no-load reservation checks
- `libretro-core-quirks.md`: per-core integration notes
- `firmware-switching.md`: reset-mediated fastboot handoff
- `clock-speed-resilience.md`: CPU/GE clock behavior
- `sf2000-hardware-findings.md`: board-level hardware findings
- `sf2000-backlight-pwm.md`: backlight PWM notes
- `av-output.md`: AV output notes
- `WIRELESS_STATUS.md`: wireless controller investigation status
