# UniFrog Libretro Cores

`cores/Makefile` builds the libretro cores used by the root firmware package.
Source checkouts are fetched into `.deps/cores`; shared support libraries are
fetched into `.deps/support`.

## Commands

Run from the repository root:

```sh
make -C cores help
make deps-cores
make -C cores check
```

Useful core-maintenance targets:

```text
make -C cores help                  Show the core workflow
make -C cores print-config          Show paths and tool settings
make -C cores init                  Fetch pinned upstream sources and patches
make -C cores init CORE_IDS="gpsp gambatte"
                                    Fetch only selected cores plus support
make -C cores                       Build every configured core archive
make -C cores CORE_IDS="gpsp gambatte"
                                    Build only selected core archives
make -C cores smoke-check           Build libretro-common and QuickNES
make -C cores check                 Verify clean source checkouts
make -C cores dep-status DEP=gpsp   Show one dependency checkout
make -C cores dep-edit DEP=gpsp     Prepare a checkout for edits
make -C cores dep-update DEP=gpsp REF=<new-ref>
                                    Apply patches on a new upstream ref
make -C cores dep-refresh DEP=gpsp  Regenerate patches from local commits
make -C cores dep-finalize DEP=gpsp REF=<new-ref>
                                    Pin a prepared update and refresh patches
make -C cores pin-status            Compare pins using manifest policy
make -C cores upgrade-pins          Update manifest pins by policy
make -C cores dev-core CORE=mycore CORE_DIR=/work/mycore
                                    Build a local external core checkout
```

## Source Contract

Pinned upstream inputs live in `cores/manifest.mk`:

```text
name|checkout-directory|upstream-url|pin-policy|upstream-ref|upstream-commit|patch-dir|sparse-paths
```

`pin-policy` is `head` or `tag`. The default update path follows that policy;
`MODE=head` or `MODE=tag` is only for an explicit override.

`upstream-ref` is human-facing metadata, so tagged dependencies use release
names when possible. `upstream-commit` is the exact reproducible build input.
`patch-dir` is `-` when a dependency has no UniFrog patch queue.

Use the original upstream repository by default and keep the SF2000 port in a
small `cores/patches/<id>/` queue. The `*-prosty` packages are the deliberate
exception: they retain tzubertowski's device-optimized forks and are named
separately so users can distinguish them from upstream-based builds. Do not
replace an available upstream source with a Multicore fork merely because that
fork already contains an SF2000 Makefile.

`DEP_CHECKOUT=sparse` is the default; use `DEP_CHECKOUT=full DEP_DEPTH=0` when
you intentionally want the full upstream tree and normal history for
maintenance.

UniFrog-specific dependency changes are authored as local Git commits in
`.deps`, then regenerated with `make -C cores dep-refresh DEP=<id>`.
Do not hand-edit patch files.
Prefer build-wrapper changes in `cores/Makefile` for toolchain flags, excluded
host-only sources, warning policy, and static libretro symbol renames.

Do not commit the fetched upstream source trees.

Some forks pin their own source submodules. These are declared in
`NESTED_CORE_SPECS`, reset and patched by `core-init`, and checked separately
by `make -C cores check`. Regenerate such a queue with:

```sh
make -C cores nested-patches-refresh \
  CORE=fake08-prosty NESTED=libs/z8lua
```

## License Notes

Core source trees are third-party code and do not inherit UniFrog's MIT
license. Before enabling a new core in the package, add its upstream license
and source location to `../THIRD_PARTY.md`.

The release package includes binary core modules, so license obligations follow
the enabled module set. Several configured cores are GPL-family or
non-commercial licensed. Keep the source path reproducible through
`cores/manifest.mk` pins plus `cores/patches/<name>/`, and avoid copying
upstream source into this repository.

## ABI Contract

Generic cores should stay close to stock libretro. Specialized cores may include
`include/unifrog/abi.h` and call UniFrog services through the versioned ABI
table when that gives a real integration or performance benefit.

ABI-visible structs start with a `size` field and version or magic fields.
Existing fields stay stable; additive callbacks go at the end; `NULL` callback
handling must remain valid. Other UniFrog headers are source-level APIs unless
they explicitly say they are ABI-stable.

Until all cores are loaded externally, static builds avoid `retro_*` collisions
with preprocessor renames in `cores/Makefile`.

## Long Loads

Libretro has no generic progress callback for synchronous `retro_load_game()`.
UniFrog supports an optional `unifrog_core_load_progress(stage, current, total)`
symbol for slow load paths such as ROM reads, BIOS fallback, memory-map setup,
or emulator reset. Do not call it from `retro_run()` or other hot paths.
