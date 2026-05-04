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
- `.deps/cores`

Core sources are fetched directly from the upstream repositories pinned in
`cores/manifest.mk`. UniFrog changes are committed as patch series under
`cores/patches/`, and `make deps-cores` applies them into `.deps/cores`
without adding Git submodules to the app repository.

## Public History

Public commits should be curated, stable, and useful for review and `git blame`.
Group related private experiments into meaningful commits and reference the
source commit hashes in commit bodies.

Once the public repository is open to contributors, avoid rewriting public
`main` or release tags.
