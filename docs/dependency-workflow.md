# Dependency Workflow

UniFrog keeps this repository small. Third-party source is fetched into
untracked `.deps/` Git checkouts and is built by the root Makefile or component
Makefiles. The durable source contract is:

```text
upstream URL + exact upstream commit + generated UniFrog patch queue
```

`.deps` is disposable authoring and merge state. Do not commit it.

## Patch Queues

UniFrog dependency edits are checked in as generated patch files:

```text
patches/hcrtos-ffmpeg-compat.patch
cores/patches/<core>/
```

Developers should not hand-edit those files. Edit the dependency checkout in
`.deps`, commit the change there, then regenerate the patch queue.

For libretro cores:

```sh
make dep-edit DEP=gpsp
cd .deps/cores/gpsp-libretro
# edit files
git add ...
git commit -m "gpsp: describe the SF2000-specific change"
cd -
make dep-refresh DEP=gpsp
make dep-patches-check DEP=gpsp
```

Normal builds apply patches with Git:

```sh
git am -3 --keep-cr cores/patches/<core>/*.patch
```

Root dependency patches that predate the mailbox format are still applied by
Git with `git apply --3way --index`, then committed in the `.deps` checkout so
the worktree stays clean.

## Manifest

Pinned libretro and support inputs live in `cores/manifest.mk`:

```text
name|checkout-directory|upstream-url|pin-policy|upstream-ref|upstream-commit|patch-dir|sparse-paths
```

`upstream-ref` is human-facing metadata, so release tags are preferred when
they map to the pinned commit. `upstream-commit` is the exact build input.
`patch-dir` is `-` when the dependency has no UniFrog patch queue.

Root-level dependencies such as LVGL and FFmpeg are pinned in
`config/options.mk`. They use the same public maintenance commands:

```sh
make dep-status DEP=lvgl
make dep-edit DEP=lvgl
make dep-update DEP=lvgl REF=<new-ref>
make dep-refresh DEP=lvgl
make dep-finalize DEP=lvgl REF=<new-ref>
```

The underlying patch format can differ by dependency. Libretro core patches
are mailbox patches for `git am -3`; FFmpeg uses its checked-in compatibility
patch. JS2300 and its vendored JavaScript engine are maintained in the
standalone `frog2k-javascript` repository and are pinned by the
`JS2300_BRANCH` plus `JS2300_REF` pair. The branch fetch proves that the
immutable commit is the published `main_unifrog` tip before the checkout uses
the exact commit.

## Fetch Modes

Default fetch behavior uses the original upstream remotes:

```sh
make setup
```

Defaults are optimized for low network and disk use:

```text
DEP_CHECKOUT=sparse
DEP_DEPTH=1
partial clone with --filter=blob:none
```

Useful overrides:

```sh
make setup DEP_CHECKOUT=full
make setup DEP_DEPTH=0
make setup DEP_CHECKOUT=full DEP_DEPTH=0
```

Sparse checkout is an optimization, not a source contract. Use a full checkout
when debugging sparse issues or doing larger dependency maintenance.

## Updating A Core

Prefer the original upstream core plus a focused UniFrog patch queue. Keep a
fork pin only where the fork itself is the product feature, as with the
separately labelled `*-prosty` optimized cores. This keeps ordinary device-port
changes reviewable and makes upstream updates substantially easier.

To test the current patch queue on a newer upstream ref:

```sh
make dep-update DEP=gpsp REF=<new-tag-or-commit>
```

The target prepares `.deps/...` like this:

```text
unifrog-base      new upstream ref, no UniFrog changes
unifrog-update    current patch queue applied on top
```

If conflicts happen, resolve them inside the checkout:

```sh
cd .deps/cores/gpsp-libretro
git status
# resolve files
git add ...
git am --continue
cd -
```

After the queue is valid, finalize the manifest pin and regenerate:

```sh
make dep-finalize DEP=gpsp REF=<new-tag-or-commit>
make dep-patches-check DEP=gpsp
```

For a dependency nested inside a core source tree, use its declared
`NESTED_CORE_SPECS` entry and regenerate that queue explicitly:

```sh
make -C cores nested-patches-refresh \
  CORE=fake08-prosty NESTED=libs/z8lua
```

For reviewing old versus new patch queues, use normal Git tools from the
checkout. When enough history is present, `git range-diff` is the preferred
review command.

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

This fetches the SDK, the pinned JS2300 engine, LVGL, FFmpeg, LZ4, stb_truetype,
and shared support source without fetching every libretro core.

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
still requires adding its package metadata and any required source spec to
`cores/manifest.mk`, plus documenting the dependency in `THIRD_PARTY.md`.

## Displayless Checks

This repository is routinely tested in an Alpine container without a display.
Use:

```sh
make host-check
make host-frontend-check
make host-visual-check
```

It writes deterministic shared-model PPM artifacts under `build/host-visual/`.
Use `make host-frontend-run` for the optional libxcb shared-model viewer.

## Agent-Friendly Logs

Long builds can be run with a low-noise console wrapper:

```sh
make agent-quick-check
make agent AGENT_TARGET=verify
```

The wrapper prints only the target, log path, and final status to the terminal.
The full stdout/stderr stream is written under `build/logs/`, with the latest
path recorded in `build/logs/agent.latest`. Use `make agent-log-tail` or
`make agent-log-grep PATTERN=...` to inspect it without rerunning the command.

Normal `make` invocations are quiet by default: recursive builds inherit
`--silent`, and compile/link progress labels are hidden unless
`BUILD_PROGRESS=1` or `V=1` is set. Use `V=1` only when the actual compiler and
linker commands are needed in the console or saved agent log.

Broad local searches also use `.ignore` to skip generated output, fetched
dependencies, and the SDK by default. Use `rg --no-ignore` or an explicit path
when auditing dependency code.

## Config Inventory

Runtime settings and local build-option examples are generated from
`config/options.mk`:

```sh
make config-check
```

The build packages the documented default `/unifrog/unifrog.ini` and
user-owned settings remain in `/unifrog_data/unifrog.ini`. The frontend default
values also come from the generated `build/unifrog_default_options.h`, so the
manifest is the source to review when defaults change.

`config.mk` is untracked and is read before `config/options.mk`, so copied
`?=` lines from `build/config/config.example.mk` work as local overrides.
