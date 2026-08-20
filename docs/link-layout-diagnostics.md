# Link Layout Diagnostics

This note captures a failure class that looks like a random runtime crash but is
really a linker and allocator ownership bug. It is not specific to gpSP. Any
runtime-owned no-load area can trigger the same pattern if the linker places it
outside the boundary used by HCRTOS to start the system heap.

## Invariant

HCRTOS treats `_ebss` as the end of firmware-owned zeroed runtime memory. During
boot it clears the BSS range, and the SDK heap starts at `_ebss`. Therefore every
allocatable no-load reservation owned by UniFrog must be covered by `_ebss`, or
it must live in an explicitly separate memory owner that the SDK heap cannot
allocate from.

Examples of reservations that need this treatment:

- JIT and translation caches
- DMA rings and work buffers
- persistent scratch arenas
- handoff state areas
- fixed frame or audio staging buffers
- any future `NOLOAD` section with `A` in its ELF flags

The dangerous case is an orphan section. GNU ld can place an input section after
the current `_ebss` assignment. That makes the section visible in the ELF file
but invisible to the heap boundary, so normal `malloc()` traffic can overwrite
the reservation while the owner still believes it has exclusive memory.

## Symptoms

This class usually does not crash at the point where the bad layout is created.
The later crash often appears in unrelated kernel, driver, or queue code after
some allocator activity has reused the same RAM.

Common signs:

- `BADV` looks like a MIPS instruction word instead of a pointer.
- The faulting code is a list, queue, work, driver, audio, or display path that
  did not recently change.
- The exception reports a branch-delay fault; decode `CAUSE` and inspect the
  instruction in the delay slot, not only `EPC`.
- File logs stop too early because runtime logging is deferred or because the
  corruption happens while a core is running.
- Address-only fixes move the symptom but do not remove the corruption source.

The gpSP dynarec incident matched this pattern: generated code lived in a
translation cache that was placed after `_ebss`, while the HCRTOS heap also began
at `_ebss`. A later work-queue exception used an instruction-looking value as a
pointer because allocator traffic had overlapped executable cache memory.

## Diagnosis Checklist

Start with the link image before changing runtime code:

```sh
make layout-check
$(CROSS_COMPILE)nm -n output/sf2000.out | grep -E '(_ebss|__.*start|__.*end)'
$(CROSS_COMPILE)readelf -SW output/sf2000.out
```

For every allocatable `NOBITS` section, confirm that:

- the section appears explicitly in `linker/ldscript.ld`
- it has exported start and end symbols if runtime code depends on it
- its end address is at or below `_ebss`
- it is intentionally executable only when it contains generated code

When diagnosing a device exception:

- map `EPC` and `RA` with `addr2line` against `output/sf2000.out`
- disassemble around `EPC` and check whether the exception happened in a branch
  delay slot
- compare `BADV` with known memory ranges and common MIPS instruction encodings
- check the map/readelf output before assuming a bad load address or bad core
  entry point

## Prevention

Do not rely on orphan section placement for runtime reservations. Add each
reservation to the linker script with an explicit ownership comment and keep the
heap boundary after all UniFrog-owned allocatable `NOBITS` reservations.

The build has a guard for this now:

```sh
make layout-check
make check
```

`layout-check` scans `output/sf2000.out`, external core module ELFs under
`build/core_modules/*.out`, and runtime module ELFs under `build/runtime_modules`.
It fails if any allocatable no-load section ends after `_ebss`. This catches the
failure class early for JIT caches, DMA buffers, and future fixed reservations.
It intentionally checks the ELF layout rather than a single symbol name, so new
sections are covered without adding a special case.

Runtime diagnostics are still useful when a core runs for a while and then
freezes. Prefer live, bounded counters over high-volume file logging:

- current core stage
- last frame number and video size
- last guest PC or core-provided progress marker when available
- last audio batch count and failure count
- current presenter timing bucket

Keep those diagnostics rate-limited or on-screen. File logging from inside a hot
core loop can change timing, fill buffers, or hide the real failure behind a
logging crash.

## Adding A New Reservation

Before landing a new fixed runtime area:

1. Add the section explicitly to `linker/ldscript.ld`.
2. Align it for the hardware owner that will use it.
3. Export `__name_start` and `__name_end` symbols if C code needs the range.
4. Keep `_ebss` after the reservation unless it is outside sysmem by design.
5. Run `make layout-check` and inspect the printed ranges.
6. Document the owner and lifetime in `docs/memory-layout.md`.

For high-memory handoff or stage areas, also prove that the application arena and
external core arenas cannot overwrite them. The same ownership rule applies even
when the memory is outside normal `.bss`.
