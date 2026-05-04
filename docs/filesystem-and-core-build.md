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

## Current Core Build

The fresh repository does not track libretro cores as submodules. For the first
transition step, `make deps` fetches the existing UniFrog core build repository
into:

```text
.deps/unifrog-cores
```

The top-level Makefile invokes that checkout to build the same core artifacts as
the previous repository.

## Intended Core Build

The next simplification is to replace `.deps/unifrog-cores` with direct
manifests:

```text
cores/
  manifest.mk
  gpsp/
    upstream.mk
    overlay/
    patches/
```

Each core entry should pin an upstream URL and commit. UniFrog-specific files
should live as small overlays where possible, with patches only for changes that
must edit upstream files.

That layout keeps this repository small while making core changes reviewable in
normal commits.
