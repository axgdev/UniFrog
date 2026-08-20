# Architecture Roadmap

UniFrog should stay a modular monorepo for firmware-owned code. The device
image, Linux runner, SD package, and host checks need atomic changes across
frontend, runtime, media, and libretro boundaries. Splitting these into
separate repositories before the APIs are stable would move complexity from C
interfaces into release coordination.

## Repository Strategy

- Keep UniFrog-owned firmware, host runner, JS2300 bridge, components,
  foundation code, tools, linker scripts, and tests in this repository.
- Keep the HCRTOS SDK as the only Git submodule. It has a different license
  profile and lifecycle, and it is important enough to remain independently
  versioned.
- Keep libretro cores and other third-party source as manifest-pinned `.deps`
  checkouts plus generated patch queues. Cores are rarely edited, license
  sensitive, and easier to replace when UniFrog carries only pins, build rules,
  and reviewable patch queues.
- Do not add Git submodules for normal cores. Use `make dep-edit`,
  `make dep-refresh`, and `make dep-patches-check` for local core changes.

## Boundary Goals

- Public APIs live in `include/unifrog/` and `js2300/include/`.
- Private headers stay beside their owning implementation and must not be
  included across component boundaries.
- Foundation code cannot depend on components.
- Frontend owns presentation and UI state; libretro and media engines cannot
  import frontend code.
- Platform SDK and vendor headers stay in app entrypoints, SF2000 platform
  backends, and SF2000 media backends.
- Shared policy code must be host-testable and free of HCRTOS includes,
  concrete device nodes, and SF2000 mount paths.

## Refactor Sequence

1. Move source inventories into component-owned manifest fragments while
   keeping the root `Makefile` as the build source of truth.
2. Compile components with owner-specific private include paths instead of
   exposing every component private directory globally.
3. Add narrow service facades where frontend, JS2300, libretro, and media need
   runtime actions. The first implemented facade is
   `unifrog_frontend_launch_services`, which covers frontend launch/report
   side effects while keeping libretro and media hot paths direct.
4. Promote shared classification, parsing, and policy helpers out of UI-owned
   files so components can be tested independently.
5. Expand `make architecture-check` whenever a boundary rule is made explicit.

## C And C++ Policy

Firmware stays C-first. Use C++ only where it makes code materially smaller or
clearer without measurable firmware cost. Firmware C++ must avoid exceptions,
RTTI, hidden allocation, and surprising global constructors unless the cost is
measured and documented.
