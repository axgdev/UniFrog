# Development Workflow

UniFrog now uses one app repository plus one SDK submodule.

The app repository contains the native frontend, JS2300 runtime bindings,
JavaScript frontend, build rules, linker scripts, DTS files, docs, and small
permissive third-party sources.

The SDK submodule points at the separate mixed-license HCRTOS repository. It
contains the headers, static libraries, kernel objects, and notices required to
link the app without vendoring that code into UniFrog history.

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
- `.deps/support`

Core and shared support sources are fetched directly from the upstream
repositories pinned in `cores/manifest.mk`. UniFrog changes are committed as
patch series under `cores/patches/`, and `make deps-cores` applies them into
`.deps/cores` without adding Git submodules to the app repository. Shared
libchdr, zstd, zlib, and LZMA sources live under `.deps/support` and are built
once for the cores that need CHD/media support.

The default SDK path is the `unifrog-hcrtos-sdk` submodule. `make deps`
initializes it with a shallow checkout. Use `SDK=/path/to/unifrog-hcrtos-sdk`
only when testing local SDK changes outside the app repository.

## Branches

Use only two long-lived branches:

- `develop`: local/private integration branch for intermediate work.
- `main`: public branch with curated commits suitable for review, releases, and
  useful `git blame`.

There is no `public-main` branch. Move work from `develop` to `main` by
cherry-picking clean commits or by squashing related experiments into coherent
commits.

## Public History

Public commits should be curated, stable, and useful for review and `git blame`.
Group related private experiments into meaningful commits and reference the
source commit hashes in commit bodies.

Once the public repository is open to contributors, avoid rewriting public
`main` or release tags.
