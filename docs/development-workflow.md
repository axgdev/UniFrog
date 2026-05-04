# Development Workflow

UniFrog now uses one app repository plus one separate SDK repository.

The app repository contains the native frontend, JS2300 runtime bindings,
JavaScript frontend, build rules, linker scripts, DTS files, docs, and small
permissive third-party sources.

The SDK repository contains the mixed-license HCRTOS headers, static libraries,
kernel objects, and notices required to link the app.

## Local Setup

```sh
make deps-alpine
make deps
make doctor
make check
```

Use `config.mk` for local overrides:

```make
SDK := ../unifrog-hcrtos-sdk
TOOLCHAIN := /opt/mipsel-mti-elf
CCACHE := ccache
```

Do not commit `config.mk`.

## External Dependencies

Fetched dependencies live under `.deps/` and are not committed:

- `.deps/mquickjs`
- `.deps/unifrog-cores`

The `.deps/unifrog-cores` checkout is a compatibility bridge for the current
core build. The intended direction is to replace it with direct core manifests
and UniFrog overlays in this repository.

## Public History

Public commits should be curated, stable, and useful for review and `git blame`.
Group related private experiments into meaningful commits and reference the
source commit hashes in commit bodies.

Once the public repository is open to contributors, avoid rewriting public
`main` or release tags.
