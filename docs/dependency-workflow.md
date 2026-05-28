# Dependency Workflow

UniFrog keeps this repository small. Third-party source is fetched into
untracked `.deps/` checkouts and is built by the root Makefile or the component
Makefiles.

## Managed Source Repo

The preferred dependency mirror is:

```text
git@github.com:axgdev/unifrog-deps.git
```

Each dependency lives on a sparse snapshot branch:

```text
deps/<name>/<upstream-label>
```

Examples:

```text
deps/gpsp/eca3bee1e2d2
deps/libretro-common/e2e3eccfd245
deps/ffmpeg/n4.4.7
```

The branch is the UniFrog-ready dependency subset, not a full upstream mirror.
It intentionally carries only the files needed by the default build so
`unifrog-deps` stays small. UniFrog-specific dependency changes live as normal
commits on these branches instead of build-time patches. Do not force-push a
branch after other developers may have fetched it. Create a new branch for a new
upstream base or a meaningful integration change, then update the pin in the
manifest.

`cores/manifest.mk` still records the original upstream URL and upstream commit,
and the root `Makefile` does the same for root-level dependencies such as
MQuickJS, LVGL, and FFmpeg. That keeps provenance visible without carrying
separate `upstream/<name>` branches in the managed repo.

## Fetch Modes

Default fetch behavior uses the managed dependency branches:

```sh
make setup
```

`DEP_SOURCE=unifrog` is the default and requires each managed branch to be
present. Fetches use non-interactive Git settings so missing SSH credentials
fail quickly instead of blocking.

Useful overrides:

```sh
make setup DEP_SOURCE=unifrog
make setup DEP_SOURCE=upstream
make setup DEP_CHECKOUT=full DEP_DEPTH=0
```

`DEP_CHECKOUT=sparse` keeps local checkouts small. `DEP_CHECKOUT=full` keeps the
whole dependency tree for development when fetching from upstream. The managed
`unifrog-deps` branches remain sparse snapshots by design. `DEP_DEPTH=1` is
shallow by default; `DEP_DEPTH=0` fetches normal history.

A local sibling dependency repo is supported for fast iteration:

```sh
make setup-cores UNIFROG_DEPS_REPO_URL=../unifrog-deps DEP_SOURCE=unifrog
```

Relative local paths are resolved before the root Makefile recurses into
`cores/`, so the same override works from the root build.

## Selected Cores

Developers can fetch, build, and package only the libretro cores they care
about:

```sh
make setup-cores CORE_IDS="gpsp gambatte"
make core-package CORE_IDS="gpsp gambatte"
make -C cores CORE_IDS="gpsp gambatte"
```

`CORE_IDS` uses the package-facing ids from `make list-cores`. UniFrog always
fetches required core infrastructure such as `libretro-common`, and it fetches
CHD support only when the selected cores need it.

For a minimal non-core setup:

```sh
make setup-min
```

This fetches the SDK, MQuickJS, LVGL, FFmpeg, and shared support source without
fetching every libretro core.

## Local Core Development

Developers working on their own core can keep it outside `.deps` and build it
through the same cross-toolchain:

```sh
make -C cores dev-core CORE=mycore CORE_DIR=/work/mycore
```

The default assumes `Makefile.libretro` and produces
`mycore_libretro_sf2000.a`. Override these when the core uses different names:

```sh
make -C cores dev-core CORE=mycore CORE_DIR=/work/mycore \
  DEV_CORE_MAKEFILE=Makefile \
  DEV_CORE_TARGET=mycore_libretro_sf2000.a
```

The produced archive is copied to `cores/output/`. Packaging a fully new core
still requires adding its module metadata to the root `Makefile`, documenting
the dependency in `THIRD_PARTY.md`, and adding any required source spec to
`cores/manifest.mk`.

## Displayless Checks

This repository is routinely tested in an Alpine container without a display.
Use:

```sh
make host-visual-check
```

It writes deterministic PPM and PNG artifacts under `build/host-visual/`. Those
artifacts are intended for CI diffing and quick manual inspection. A real X11
host frontend can be added later as an optional manual test path, but the
displayless artifacts are the baseline.

## Config Inventory

Runtime settings and local build-option examples are generated from
`config/options.mk`:

```sh
make config-check
```

The build packages `settings.example.ini` under `/unifrog` and user-owned
settings remain in `/unifrog_data/settings.ini`. The native frontend default
values also come from the generated `build/unifrog_default_options.h`, so the
manifest is the source to review when defaults change.
