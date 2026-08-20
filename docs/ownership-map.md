# Ownership Map

This map is the review-routing equivalent of `CODEOWNERS` without binding the
repository to specific GitHub account names. Use it to decide which team should
review a change and which checks are expected before handoff.

| Area | Owner | Scope | Primary checks |
| --- | --- | --- | --- |
| `apps/firmware`, `apps/fastboot`, `linker/` | Firmware integration | Boot flow, app composition, fastboot handoff, link layout | `make clean && make verify` |
| `apps/linux`, `foundation/src/platform/linux` | Linux runner | Host platform shim, headless and XCB runners | `make linux-runner-check`, `make host-frontend-check` |
| `foundation/src/platform/sf2000`, `board/`, `dts/` | Hardware platform | SF2000/GB300 storage, input, audio, display, clocks, fastboot handoff implementation | `make quick-check`, `make verify`, device test |
| `foundation/src/{abi,archive,config,device,display,modules,runtime,storage}` | Foundation runtime | Public runtime services and portable utilities | `make host-quick-check`, `make quick-check` |
| `components/frontend` | Frontend | Production app UI, navigation, themes, settings presentation, reader UI, quick menu | `make host-frontend-check`, `make linux-runner-check`, `make quick-check` |
| `components/libretro` | Libretro frontend engine | Core loading, sessions, callbacks, save/state/content handling | `make host-quick-check`, `make quick-check` |
| `components/media` | Media engine | Media classification, FFmpeg/HCRTOS playback, media policy | `make media-policy-host-check`, `make quick-check` |
| `components/diagnostics` | Diagnostics | Device tests, report generation, deterministic diagnostic policy | `make diagnostic-policy-host-check`, `make quick-check` |
| `js2300` | JS2300 integration | Host bridge, scripts, JS libretro core; portable engine is pinned in `frog2k-javascript` | `make js2300-check`, `make quick-check` |
| `cores`, `patches`, `THIRD_PARTY.md` | Dependencies and cores | Manifests, patch queues, source pins, license inventory | `make core-manifest-check`, `make dep-patches-check` |
| `tools`, `mk`, `config` | Build and verification | Make fragments, CI helpers, generated config defaults | `make quick-check`; `make clean && make verify` after dependency or linker changes |

## Review Rules

- Mechanical moves and behavior changes should be separate commits.
- Any public header change needs a short ABI impact note in the commit body.
- Any new cross-component include must be justified by a public API boundary.
- Any dependency pin or patch queue update must keep the source contract:
  upstream URL, exact commit, and generated UniFrog patch queue.
- Any firmware-size-sensitive change should run `make component-sizes` and
  mention material size movement in the handoff.
