# Documentation Map

Start with `../README.md`, then use the smallest focused document that answers
the question.

## Architecture

- `source-layout.md`: source ownership and boundary rules.
- `ownership-map.md`: review/check routing by subsystem.
- `architecture-roadmap.md`: repository strategy and C/C++ policy.
- `unifrog-architecture.md`: firmware, runtime, package, and ABI boundaries.
- `external-core-loading.md`: SD-loaded module format and loader details.

## Workflows

- `developer-onboarding.md`: first build, edit loop, and handoff checks.
- `dependency-workflow.md`: dependency pins, patch queues, and selected cores.
- `../cores/README.md`: libretro core build and patch workflow.
- `../js2300/README.md`: embedded JavaScript runtime layer.
- `js2300-scripting.md`: optional JS2300 script contract.

## Hardware Notes

- `sf2000-hardware-findings.md`: board-level findings.
- `sf2000-mmc-driver.md`: HC15xx MMC driver work.
- `sf2000-gb300-audio.md` and `audio-quirks.md`: audio routes and gotchas.
- `memory-layout.md` and `link-layout-diagnostics.md`: linker and arena layout.
- `firmware-switching.md` and `update-layout.md`: fastboot and SD update layout.

Keep docs short. If a source rename or clearer function removes the need for a
paragraph, prefer the code change.
