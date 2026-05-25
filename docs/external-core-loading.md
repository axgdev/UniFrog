# External Module Loading

UniFrog keeps libretro cores and large optional native features outside the
main firmware binary. The base firmware owns boot, storage, input, logging,
recovery, and the stable `libunifrog` ABI. Core binaries live under
`/unifrog/cores`; native modules live under `/unifrog/modules`.

## Fixed Memory Slot

The current loader uses a static reserved memory range instead of `malloc`.
The address is not treated as a cross-firmware ABI constant. UniFrog asks the
runtime for `struct unifrog_abi_memory_slot`, which reports the application slot
chosen for this firmware build and board configuration.

`0x87000000` was useful during fastboot because the stage-1 loader proved that
address was safe before the main firmware started. It is not a good long-term
ABI promise: a larger base firmware, a different device memory map, or a larger
application could make that fixed address waste RAM or collide with runtime
memory.

The preferred model is a device-tree reserved region such as
`/hcrtos/memory-mapping/appmem`. HCRTOS keeps normal heap allocations out of
that range, while UniFrog can report it to the external application loader. The
region is static for a running firmware, but its address and size can move
between firmware builds or devices without breaking external apps.

The current policy is to reserve high memory above sysmem while keeping a
sysmem floor for the base firmware, HCRTOS heap, UI state, device drivers,
filesystem, audio, video, and the code needed to recover from an app. This
gives external apps one large contiguous arena. If a device has less RAM, the
reservation can shrink or disappear; the loader must query the ABI and refuse
apps that do not fit.

An external core must not hardcode private UniFrog function addresses. Raw
addresses change whenever the firmware link changes. UniFrog passes a stable
`struct unifrog_abi` pointer to the module entry point. ABI callbacks are
firmware-side trampolines, so a module may call them while its own `$gp` is
active.

MIPS cached virtual addresses start around `0x80000000`, but that does not mean
the application arena overlaps the firmware just because its cached address is
also in that range. Physical RAM `0x00000000` is visible through cached KSEG0 aliases at
`0x80000000`. Firmware linked at `0x80001000` is physical `0x00001000`; an
application arena reported as cached `0x83000000` is physical `0x03000000` on
the current 128 MiB DTS.

## Module Binary Shape

The staged `*.bin` files are UniFrog modules, not renamed static archives and
not normal host shared libraries. The SDK does not provide `dlopen`/`dlsym`,
so each module is linked as a fixed-address image for the appmem slot selected
by the matching firmware build.

The module header is the first bytes in the file and contains:

- magic and header size
- format version and endian marker
- required UniFrog ABI semantic version
- required ABI table size, the ABI version used at build time, and export table
  size for diagnostics
- fixed load address, file end, memory end, BSS range, entry address, and
  module `$gp`
- metadata: core id and supported extensions

The module entry returns a `struct unifrog_core_module_exports` table. Libretro
modules fill the libretro function pointers and run through the normal host;
native modules fill their own optional exports, such as the HCRTOS media
player entry point.

The module linker must account for every allocatable `NOBITS` reservation in
the header's memory range. A core-owned executable cache such as gpSP's `.jit`
section must be explicitly placed before `__unifrog_module_end`/`_ebss`; orphan
placement after those symbols means the loader will not clear or reserve it.

The loader should:

1. Pick the core based on content type or a UI selection policy.
2. Query the ABI application memory slot and verify the app fits.
3. Read the module image into the advertised static memory slot.
4. Clear the module BSS and any NOBITS reservations.
5. Flush data cache and invalidate instruction cache for the loaded range.
6. Call the core entry point with `const struct unifrog_abi *`.
7. Refuse to run if the core requires a newer major ABI, a newer minor ABI, or
   a larger ABI table than the runtime provides.

## `$gp` Boundary

MIPS O32 code built with this toolchain relies on the global pointer register
for some data references. A module has a different `_gp` from the firmware.
Direct cross-boundary function pointers are therefore unsafe.

UniFrog uses two trampoline directions:

- Host-to-core calls load the module `$gp`, call the libretro function, then
  restore the firmware `$gp`.
- Core-to-host libretro callbacks save the module `$gp`, load the firmware
  `$gp`, call the C callback, then restore the module `$gp` before returning.

The same rule applies to ABI callbacks exposed through `struct unifrog_abi`.
Do not bypass these trampolines when adding new callback surfaces.

`$gp` is not the only ABI boundary detail. Generic trampolines are valid only
for functions whose complete argument list fits in the normal register set.
MIPS O32 puts later arguments on the caller stack, and 64-bit arguments can
force otherwise small-looking parameters onto the stack because of alignment.

The concrete failure that exposed this was `fseeko(FILE *, off_t, int)`:
newlib defines `off_t` as 64-bit for this SDK, so `whence` is passed on the
stack. If the ABI trampoline allocates its own frame and does not copy that
stack slot to the host call frame, the firmware-side `fseeko()` receives a bad
`whence`. libretro-common VFS then seeks incorrectly while measuring the ROM,
and cores that load by full path can see a valid file with size zero.

When adding an ABI callback, check the exact O32 calling sequence, not only the
C prototype. Any callback with more than four words of arguments, 64-bit
arguments, structs by value, or variadic arguments needs a purpose-built
trampoline or a wrapper that reduces the ABI shape to register-only arguments.

Useful diagnostics for this class:

- log the firmware commit and core module commit before `retro_load_game()`
- log fullpath/data-mode and the measured content size
- treat impossible progress such as `ROM current=0 total=0` as an ABI or VFS
  boundary failure before chasing core dynarec state
- verify the trampoline with `objdump -dr` so stack argument copies are visible

## Build Outputs

`make core-package` builds self-contained modules for the known cores and
places them in `output/sdcard/unifrog/cores/*.bin`. `make module-package`
builds native modules such as `output/sdcard/unifrog/modules/hcrtos-media.bin`.
The default firmware link does not include libretro core archives:

```make
FIRMWARE_LIBRETRO_CORE_LIBS ?=
```

The module wrapper is additive. Core archives stay upstream-shaped; UniFrog
adds the module header, entry point, SDK glue symbols, cache flush hook, and
optional `unifrog_core_load_progress()` bridge at packaging time.

## Focused Core Development

Developers working on one libretro core do not need to rebuild every core:

```sh
make list-cores
make core CORE=picodrive
make core-archive CORE=picodrive
make core-out CORE=picodrive
make core-package CORE=picodrive
```

`make core CORE=<id>` builds the frontend package and the selected
`output/sdcard/unifrog/cores/<id>.bin`. `make core-archive CORE=<id>` stops
after the upstream static archive under `cores/output/`, which is the fastest
loop when iterating on core compiler flags or upstream source changes.
`make core-out CORE=<id>` links the fixed-address ELF module but does not
`objcopy` it into the SD package.

To add a packaged core, register it once in the top-level `LIBRETRO_MODULES`
list:

```make
LIBRETRO_MODULES += \
	my-core:MY_CORE:my-core:my_core:foo\|bar:my_core:-
```

The fields are:

- user-facing `CORE=` id
- make variable prefix
- module/output basename, using `_` when the ELF object cannot use `-`
- `cores/Makefile` target and archive stem
- supported ROM extensions separated with `\|`
- optional libretro symbol prefix, or `-` if the core exports unprefixed symbols
- optional support archive dependency, or `-`

If the upstream static archive name does not match
`cores/output/<archive-stem>_libretro_sf2000.a`, override
`<PREFIX>_CORE_LIB` after the registration, as `QPSX_CORE_LIB` does. If the
SD-package filename should differ from the module basename, override
`<PREFIX>_CORE_BIN`, as `PCE_FAST_CORE_BIN` and `PMP_VIDEO_CORE_BIN` do. The
core still needs a matching target in `cores/Makefile` and a source pin in
`cores/manifest.mk`.

## Pixel Formats and GE

The HCRTOS framebuffer driver accepts 16 bpp and 32 bpp RGB layouts, and the
GE API supports `RGB565`, `RGB24`, `RGB32`, and `ARGB8888` style surfaces.
UniFrog currently opens `/dev/fb0` as `RGB565` because that is the lowest
bandwidth native scanout path for the SF2000 panel.

For libretro performance work, the useful distinction is:

- A core producing `RGB565` can be sent to the `RGB565` framebuffer with GE
  blit/stretch and no color-depth expansion.
- A core producing `XRGB8888`/`ARGB8888` can already be used as a GE source;
  GE can blit/stretch and convert it into the current `RGB565` framebuffer.
- Using a 32 bpp framebuffer for final scanout would double framebuffer
  bandwidth and memory footprint. It may remove one GE color conversion for
  32 bpp cores, but it is not a guaranteed zero-cost win on this device.

So the practical answer is: 32-bit sources are supported through GE, but the
current performant default should remain `RGB565` scanout until device tests
show that a 32 bpp framebuffer improves a specific core enough to offset the
extra memory bandwidth.

## Versioning

UniFrog ABI versions follow semantic versioning, but core modules should stamp
the oldest ABI they actually require:

- major: breaking ABI change. Old cores need a compatibility layer or rebuild.
- minor: additive ABI change for appended callbacks or an intentional
  compatibility break while UniFrog remains pre-1.0.
- patch: behavior fixes with the same ABI surface.

The stable core floor is `UNIFROG_ABI_CORE_MIN_VERSION`. A normal libretro core
module uses that floor and `UNIFROG_ABI_CORE_MIN_SIZE`, even when it is built
with a newer UniFrog tree. That lets one released core run on several UniFrog
firmware releases.

When adding host services to `struct unifrog_abi`, append them at the end of
the table and make module-side code treat them as optional with
`UNIFROG_ABI_HAS_MEMBER()`. Raise `UNIFROG_CORE_MODULE_REQUIRED_ABI_VERSION`
only for modules that truly require the new callback. Never reorder or remove
existing fields inside a major ABI.

The loader supports old cores by keeping compatible callback behavior and
checking both required semantic version and required ABI table size. If no
compatible table exists, the UI should show a clear unsupported-core message
instead of attempting to jump into the binary.

The native frontend includes Apps -> Core Manager for device-side inspection of
installed `unifrog/cores/*.bin` files. It reads each module header directly on
device and reports the core id, required ABI version, required ABI table size,
built ABI, memory range, export size, supported extensions, and whether the
running firmware can load it.

## Relationship With libretro-common

Normal cores should continue to use libretro and libretro-common where possible.
UniFrog-specialized cores may use the UniFrog ABI for device-specific fast
paths. If a common helper is needed by many cores, prefer adding it to the
UniFrog ABI or to a reusable core support archive with a stable import surface,
rather than copying private frontend code into every core.
