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

`make deps-cores` clones or updates each listed core commit directly into:

```text
.deps/cores/
```

Shared support inputs for CHD/zstd/zlib/LZMA are pinned in the same manifest and
fetched into:

```text
.deps/support/
```

The core build recipe lives in `cores/Makefile` and writes built static archives
to:

```text
cores/output/
```

The top-level Makefile links those archives into loadable core modules and
stages the final `.bin` files under `output/sdcard/unifrog/cores/`.

## Core Changes

UniFrog-specific core changes are committed as patch series under
`cores/patches/<core>/`. Generated checkouts under `.deps/` can be deleted and
recreated from the manifest plus patches.
