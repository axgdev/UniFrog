# Agent Notes

This repository is intentionally small and direct. Keep it that way.

## Build Contract

- Primary command: `make`
- Health check: `make doctor`
- Verification before handoff: `make check`
- Default toolchain: `/opt/mipsel-mti-elf`
- Default SDK path: `../unifrog-hcrtos-sdk`
- Default fetched dependency path: `.deps`
- Local overrides belong in untracked `config.mk`, not in committed files.

## Scope

- Keep only files needed to build the native SF2000 `bisrv.asd`.
- Do not vendor the full HCRTOS tree, generated download caches, unrelated
  sample applications, downloaded toolchains, MQuickJS checkouts, or libretro
  core source checkouts.
- Keep mixed-license HCRTOS headers/static libraries in the sibling SDK
  repository. Fetch external source dependencies with `make deps`.

## Editing Rules

- Prefer Makefile changes over adding shell scripts.
- Avoid Python, CMake, Autotools, and generated configure layers.
- Keep dependencies to a host C compiler, the MIPS cross toolchain, `make`,
  `dtc` for the local device tree, the sibling SDK repository, fetched
  dependencies under `.deps`, and normal Unix utilities.
- Preserve `tools/asdpack.c` as a host-built tool instead of committing a
  prebuilt binary.
- If changing link libraries or linker scripts, run `make clean && make check`.
