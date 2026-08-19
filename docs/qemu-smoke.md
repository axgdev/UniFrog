# QEMU Smoke Workflow

UniFrog has a direct boot smoke target for the SF2000 machine in the separate
`frog2k-qemu` repository:

```sh
make qemu-smoke \
  QEMU_DIR=/path/to/frog2k-qemu \
  QEMU_WORK=/tmp/sf2000-qemu
```

The target builds QEMU only when its configured binary is absent, builds the
current `bisrv.asd`, and runs it with the bug-fixed SF2000 BIOS. It accepts the
expected timeout for a firmware that continues running, then requires both
the QEMU ASD-load message and UniFrog's `unifrog.main.start` boot trace. The
logs are kept in `$(BUILD)/logs/qemu/` so an early boot failure is inspectable.

The BIOS is proprietary and is intentionally not committed to either
repository. A local run therefore needs the firmware at:

```text
<QEMU_DIR>/firmware/SF2000_XMC_XM25QH40B_4mbit_bugfix.bin
```

## Self-hosted GitHub Actions runner

`.github/workflows/qemu-smoke.yml` uses a self-hosted runner because the BIOS
and the MIPS build inputs are private or too large for a hosted job. Configure
these repository or organization variables on the runner:

```text
UNIFROG_QEMU_DIR       checked-out frog2k-qemu source tree
UNIFROG_QEMU_WORK      persistent QEMU build directory, normally /tmp/sf2000-qemu
UNIFROG_DEPS           cached UniFrog dependency root
UNIFROG_SDK            checked-out unifrog-hcrtos-sdk worktree
UNIFROG_JS2300         checked-out frog2k-javascript worktree
UNIFROG_TOOLCHAIN      frog-toolchain mipsel-mti-elf prefix directory
```

The runner must have `make`, `git`, `dtc`, `curl`, `tar`, `xz`, a host C
compiler, `ccache` (recommended), and the QEMU build dependencies. The
workflow checks all private paths explicitly before compiling; it does not
silently substitute a different firmware, toolchain, or SDK.

For a cold runner, populate the paths using the normal repository workflow:

```sh
make setup-min ci-toolchain
```

Then point the variables at the resulting paths. Keep the QEMU source and
BIOS outside the UniFrog checkout so a clean checkout remains reproducible.
