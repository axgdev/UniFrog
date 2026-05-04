# Agent Notes

Keep this repository small and direct.

## Build Contract

- Primary command: `make`
- Health check: `make doctor`
- Handoff verification: `make check`
- Defaults: `TOOLCHAIN=/opt/mipsel-mti-elf`, `SDK=unifrog-hcrtos-sdk`,
  `DEPS=.deps`
- Local overrides belong in untracked `config.mk`.

## Scope

- Build the native SF2000 `bisrv.asd` and SD-card package.
- Keep the mixed-license HCRTOS SDK in the `unifrog-hcrtos-sdk` submodule.
- Fetch external source dependencies with `make deps`; do not commit `.deps`,
  generated outputs, downloaded toolchains, or upstream core/MQuickJS checkouts.
- Preserve `tools/asdpack.c` as a host-built tool.

## Editing Rules

- Prefer Makefile changes over shell scripts.
- Avoid Python, CMake, Autotools, and generated configure layers.
- Keep dependencies to a host C compiler, the MIPS cross toolchain, `make`,
  `dtc`, the SDK, fetched dependencies, and normal Unix utilities.
- If changing link libraries or linker scripts, run `make clean && make check`.
  Otherwise run `make check` before handoff.
