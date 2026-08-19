# Maintenance Architecture

UniFrog remains a monorepo. Components are separated by C interfaces and Make
targets instead of Git submodules, so firmware, host checks, packaging, and
device diagnostics can evolve in one atomic commit.

## Host-Testable Components

These components contain no HCRTOS hardware access and run on Linux during
`make quick-check`:

- `frontend_model.h` and `frontend_model.c`: static menus, typed
  actions, selection behavior, and storage-profile menus.
- `frontend_controller.h` and `frontend_controller.c`: screen
  transitions and mutable menu settings for the shared model tests. The
  production app owns runtime view stacks in `frontend_navigation.c`.
- `frontend_services.h` and `frontend_services.c`: cold launch/report
  side effects from the production frontend into libretro, media, reader,
  JS2300, and diagnostic report services.
- `libretro_policy.h` and `unifrog_libretro_policy.c`: quick-menu value
  selection and labels, independent of core and hardware state.
- `media_policy.h` and `unifrog_media_policy.c`: readahead range lookup, LRU
  selection, and usage-clock rollover.
- `reader.h` path classification helpers: shared reader/media content
  decisions that should not pull frontend UI code into media tests.
- `diagnostic_policy.h` and `unifrog_diagnostic_policy.c`: storage patterns,
  incremental checksums, throughput calculations, and display test pixels.
- Device-facing diagnostics use `unifrog_task_*`, `UNIFROG_*_ROOT`, and
  `unifrog_platform_debug_*`; delay APIs, mount paths, and debug transports
  remain in platform backends.
- `task.h`: background work and delay abstraction. Components use
  `unifrog_task_*`; FreeRTOS and pthreads stay in platform backends.
- `core_registry.h` and `frontend_core_registry.c`: module-header
  parsing, installed-core discovery, and content compatibility families.
  Linux additionally inspects native libretro `.so` files through the same
  registry contract.
- `storage_profile.h` and `unifrog_storage_profile.c`: runtime profile labels
  and reported bus, timing, and signal properties.
- `tools/frontend_model_viewer.c`: deterministic shared-model visual artifacts and
  optional libxcb model browsing. This is not the production app runner.

Run:

```sh
make linux-runner
make linux-runner-check
make host-frontend-check
make host-visual-check
make host-frontend-run
make linux-xcb-run
```

`linux-runner` builds `build/linux/unifrog-linux`, a headless Linux executable
that runs the production `unifrog_frontend_app` with Linux platform services.
It accepts `--script` button sequences such as `down,down,down,enter` together
with `--render-ppm`. `linux-xcb-run` builds `build/linux/unifrog-linux-xcb`,
the same production frontend in an interactive XCB window. Arrow keys move,
Enter activates, and Backspace navigates back. While a core is running, `q`
opens the quick menu and Escape exits.

`host-frontend-check`, `host-visual-check`, and `host-frontend-run` exercise
the shared frontend model through `tools/frontend_model_viewer.c`. Use them for fast
model/action regressions. Use `linux-runner-check` or `linux-xcb-check` when a
change touches production app composition, filesystem browsing, theme loading,
launch services, or Linux platform shims.

The Linux production frontend source set is derived from
`components/frontend/sources.mk`. Add frontend source files to that
owner manifest first; host runner targets should filter or compose from it
rather than duplicating the inventory.

Linux cores must be native shared libraries for the machine running UniFrog;
the MIPS `.bin` modules cannot execute on an x86 or ARM host. Put
`*_libretro.so` files in:

```text
build/linux/sdcard/unifrog/cores
```

`make linux-runner`, `make linux-run`, `make linux-xcb`, and
`make linux-xcb-run` automatically build the default native core set into that
directory. The native builds use clean staged source copies under
`build/linux/core-src`, so host objects never contaminate the MIPS dependency
checkouts. Override the set with, for example:

```sh
make linux-xcb-run LINUX_CORE_IDS="gambatte quicknes gearboy"
```

The supported automatic host builds are `gambatte`, `gpsp`, `snes9x2005`,
`snes9x2002`, `quicknes`, `fceumm`, `gearboy`, and `pce-fast`. Other native
libretro libraries can still be installed manually or supplied through
`UNIFROG_LINUX_CORE_ROOT`.

Alternatively set `UNIFROG_LINUX_CORE_ROOT` to a core directory. UniFrog also
checks `/usr/lib/libretro`, `/usr/lib64/libretro`, and
`/usr/local/lib/libretro`. The registry calls `retro_get_system_info` to obtain
supported extensions, and Open With only offers installed compatible cores.

## Device-Only Boundaries

Linux host tests cannot validate SD DMA/cache coherency, HCRTOS interrupts,
mount recovery, hardware framebuffer/GE handoff, or device audio routing.
Native Linux libretro execution is supported, but it does not replace device
testing for timing, memory pressure, or hardware behavior. Keep these under
their existing platform/runtime boundaries and verify them with firmware
builds, retained logs, and device diagnostics.
Platform-neutral decisions should move behind public `unifrog_*` services so
`foundation/src/platform/linux/` and `foundation/src/platform/sf2000/` can
provide different implementations without duplicating component logic.
Shared components are checked for direct HCRTOS headers, `/dev` paths, and the
SF2000 storage mount path by `make architecture-check`.

Storage policy descriptions belong in `foundation/src/storage/`. Controller
operations, mount/recovery behavior, and runtime profile application remain in
`foundation/src/platform/sf2000/`.

## Frontend Rules

- Add static menu rows to the shared frontend model, not directly to
  `frontend_app.c`.
- Put deterministic navigation and setting mutation in the frontend
  controller. Keep filesystem launches, reboot, storage switching, and other
  device effects behind action services.
- Route game, media, reader, script, audio diagnostic, and bug-report launch
  effects through `unifrog_frontend_launch_services`; do not call those
  subsystem entrypoints directly from `frontend_app.c`.
- Give commands a typed `enum unifrog_frontend_action`. Keep filesystem paths
  and action identities separate.
- The frontend quick menu may use `libretro_session.h`; it must not import
  `unifrog_libretro_internal.h` or mutate libretro engine state directly.
- Use `FRONTEND_ITEM_INFO` for informational rows. Do not present unsupported
  features as commands that silently do nothing.
- Discover installed cores through the shared registry. Device entries come
  from module headers; Linux entries come from native libretro system info.
- Do not count child directory entries during normal navigation. Explicit
  indexing or diagnostics may scan recursively, but returning from a game must
  only rebuild the visible directory.

## Verification Matrix

| Change | Required checks |
| --- | --- |
| Shared menus/actions | `make host-frontend-check`, `make quick-check` |
| Linux app runner/platform shim | `make linux-runner-check`, `make quick-check` |
| Core discovery/selection | `make core-registry-check`, `make quick-check` |
| Shared model rendering | `make host-visual-check`, `make host-frontend-run` |
| Libretro/media/diagnostic policy | `make host-quick-check`, `make quick-check` |
| Platform storage, media IO, or libretro runtime | `make quick-check`, `make verify`, device test |
| Makefiles, dependencies, linker scripts | `make clean && make verify` |

The root Makefile remains the source of truth for every component.
