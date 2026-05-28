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
make -C cores init                  Fetch pinned managed source branches
make -C cores init CORE_IDS="gpsp gambatte"
                                    Fetch only selected cores plus support
make -C cores                       Build every configured core archive
make -C cores CORE_IDS="gpsp gambatte"
                                    Build only selected core archives
make -C cores smoke-check           Build libretro-common and QuickNES
make -C cores check                 Verify clean source checkouts
make -C cores core-status           Show source checkout status
make -C cores pin-status            Compare pins using manifest policy
make -C cores upgrade-pins          Update manifest pins by policy
make -C cores diff-core CORE=gpsp   Show local delta for one core checkout
make -C cores log-core CORE=gpsp    Show local commits for one core checkout
make -C cores update-core CORE=gpsp Refresh one core checkout from upstream
make -C cores dev-core CORE=mycore CORE_DIR=/work/mycore
                                    Build a local external core checkout
```

## Source Contract

Pinned upstream inputs live in `cores/manifest.mk`:

```text
name|checkout-directory|upstream-url|pin-policy|upstream-commit|deps-branch|deps-commit|sparse-paths
```

`pin-policy` is `head` or `tag`. The default update path follows that policy;
`MODE=head` or `MODE=tag` is only for an explicit override.

`deps-branch` points at the sparse dependency branch in
`git@github.com:axgdev/unifrog-deps.git`. `DEP_SOURCE=unifrog` is the default
and requires that managed branch to exist. `DEP_SOURCE=upstream` uses the
original URL directly for development checkouts. `DEP_CHECKOUT=sparse` is the
default; use `DEP_CHECKOUT=full DEP_DEPTH=0` when you intentionally want the
full upstream tree.

UniFrog-specific dependency changes should live as commits on the managed
dependency branch rather than as patch files in this repository.
Prefer build-wrapper changes in `cores/Makefile` for toolchain flags, excluded
host-only sources, warning policy, and static libretro symbol renames.

Do not commit the fetched upstream source trees.

## License Notes

Core source trees are third-party code and do not inherit UniFrog's MIT
license. Before enabling a new core in the package, add its upstream license
and source location to `../THIRD_PARTY.md`.

The release package includes binary core modules, so license obligations follow
the enabled module set. Several configured cores are GPL-family or
non-commercial licensed. Keep the source path reproducible through
`cores/manifest.mk` pins plus the managed branches in `unifrog-deps`, and avoid
copying upstream source into this repository.

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
