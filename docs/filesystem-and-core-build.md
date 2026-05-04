# Filesystem And Core Build

UniFrog stages SD-card files under:

```text
output/sdcard/
```

The installed runtime layout is:

```text
/unifrog/main.js
/unifrog/app/
/unifrog/themes/
/unifrog/scripts/
/unifrog/cores/
```

The native firmware loads external libretro core modules from
`/unifrog/cores/`.

## Core Sources

The app repository does not track libretro cores as Git submodules. Core source
inputs are described in:

```text
cores/manifest.mk
```

`make deps-cores` clones or updates each listed branch directly into:

```text
.deps/cores/
```

The core build recipe lives in `cores/Makefile` and writes built static archives
to:

```text
cores/output/
```

The top-level Makefile links those archives into loadable core modules and
stages the final `.bin` files under `output/sdcard/unifrog/cores/`.

## Core Changes

UniFrog-specific core changes are currently kept on the public branches listed
in `cores/manifest.mk`. The next refinement is to move simple integration files
into committed overlays under `cores/overlays/`, leaving only unavoidable
upstream source edits on the fetched core branches.
