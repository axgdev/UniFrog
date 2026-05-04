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
make -C cores init                  Fetch pinned sources and apply patches
make -C cores                       Build every configured core archive
make -C cores smoke-check           Build libretro-common and QuickNES
make -C cores check                 Verify clean patched checkouts
make -C cores core-status           Show source checkout status
make -C cores pin-status            Compare pins using manifest policy
make -C cores upgrade-pins          Update manifest pins by policy
make -C cores diff-core CORE=gpsp   Show UniFrog patch delta for one core
make -C cores log-core CORE=gpsp    Show UniFrog patch commits for one core
make -C cores update-core CORE=gpsp Refresh one patched checkout
```

## Source Contract

Pinned upstream inputs live in `cores/manifest.mk`:

```text
name|checkout-directory|upstream-url|pin-policy|upstream-commit|sparse-paths
```

`pin-policy` is `head` or `tag`. The default update path follows that policy;
`MODE=head` or `MODE=tag` is only for an explicit override.

UniFrog-specific changes live as committed patch series under
`cores/patches/<name>/`. Keep upstream source edits focused and reviewable.
Prefer build-wrapper changes in `cores/Makefile` for toolchain flags, excluded
host-only sources, warning policy, and static libretro symbol renames.

Do not commit the fetched upstream source trees.

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
