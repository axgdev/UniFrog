# Developer Onboarding

This guide is the shortest path from a fresh checkout to a useful change. The
Makefile is authoritative; docs explain intent and non-obvious hardware facts.

## Start Here

```sh
make setup
make doctor
make dev-check QUICK_CORE=quicknes
```

`dev-check` is the normal edit loop. It runs repository checks, Linux behavior
tests, frontend/theme/JS checks, links firmware, and builds one quick core.

Before handoff:

```sh
make quick-check
make verify
```

Run `make clean && make verify` after touching Makefiles, linker scripts,
dependency wiring, or anything that changes link composition.

## Find The Code

```text
apps/firmware/src/main.c          firmware composition
apps/linux/src/                   Linux runner composition
foundation/src/platform/sf2000/   SF2000/GB300 hardware backends
foundation/src/platform/linux/    Linux test/runtime backends
foundation/src/{abi,archive,config,display,modules,runtime,storage}
                                  reusable runtime services
components/frontend/src/app/      launcher, browser, settings, themes
components/frontend/src/reader/   document reader UI
components/frontend/src/libretro_frontend/
                                  in-game libretro presentation
components/libretro/src/          libretro host, content, sessions, policy
components/media/src/             media policy and playback
components/diagnostics/src/       device diagnostics
js2300/src/                       JavaScript runtime, host bridge, core
cores/manifest.mk                 libretro dependency pins and core metadata
```

When a file name does not tell you its owner, fix the name or move the code.
Prefer a clear source tree over extra prose.

## Boundaries

- Foundation cannot import components.
- Libretro and media cannot import frontend presentation.
- Frontend side effects leave through `frontend_services`, not direct engine
  calls.
- Public headers live in `include/unifrog/`; private headers stay beside their
  implementation.
- Include the exact public header you need. Do not add catch-all headers.
- There are no stability guarantees yet. Remove obsolete compatibility code
  instead of preserving old APIs by default.

`make architecture-check` and `make frontend-structure-check` enforce the
rules that are already explicit.

## Common Workflows

Frontend or UI:

```sh
make host-frontend-check
make linux-runner-check
make dev-check QUICK_CORE=quicknes
```

Libretro behavior:

```sh
make host-quick-check
make quick-core-check QUICK_CORE=quicknes
make dev-check QUICK_CORE=quicknes
```

Media, storage, hardware, or link layout:

```sh
make quick-check
make clean && make verify
```

Dependency or core work:

```sh
make list-cores
make setup-cores CORE_IDS="quicknes gambatte"
make core CORE=quicknes
make dep-status DEP=quicknes
```

Use `docs/dependency-workflow.md` and `cores/README.md` for pin and patch queue
maintenance.

## Change Rules

- Keep moves/renames separate from behavior changes.
- Prefer deleting legacy code over wrapping it.
- Prefer plain C. Use C++ only when it materially reduces complexity with no
  measurable firmware cost; keep firmware C++ free of exceptions, RTTI, and
  hidden allocations unless the cost is deliberately measured.
- Keep comments for constraints the code cannot express, especially hardware
  sequencing, ABI shape, and linker assumptions.
- Add or tighten checks when a boundary becomes clear.
- Commit checkpoints with the verification commands in the body.

## Useful Commands

```sh
make help
make help-options
make print-config
make component-sizes
make agent-quick-check
make agent-verify
make agent-log-tail
```

The agent targets write full logs under `build/logs/` while keeping console
output short.
