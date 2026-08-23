#!/bin/sh
set -eu

app=${APP:-unifrog}
asd=${ASD:-bisrv.asd}
out=${OUT:-output}
sdzip=${SDZIP:-output/UniFrog-sdcard.zip}
sdcard=${SDCARD:-/media/mmcblk0}
mode=${1:-common}

case "$mode" in
common)
	cat <<EOF
$app common workflow:
  make setup         Fetch SDK submodule and external source inputs
  make doctor        Check toolchain, SDK, and fetched inputs
  make dev-check     Fast validation, firmware link, and one ${QUICK_CORE:-quicknes} core
  make quick-check   Fast hygiene, Linux tests, firmware link, and one quick core
  make agent-quick-check  Run quick-check quietly and log to build/logs
  make               Build $asd, $out/unifrog.bin, and SD files
  make verify        Build and verify firmware, fastboot, JS, and layout
  make agent-verify  Run verify quietly and write a full build/logs log
  make agent-log-tail  Show the tail of the most recent agent log

Dependencies:
  make deps-status   Show pins vs policy, or override MODE=head|tag
  make dep-status DEP=qpsx
  make dep-update DEP=qpsx REF=<ref>
  make dep-finalize DEP=qpsx REF=<ref>
  make dep-patches-check DEP=qpsx
  make upgrade-deps  Bump pins by policy, or override MODE=head|tag

Core builds:
  make list-cores
  make setup-cores CORE_IDS="gpsp gambatte"
  make core CORE=picodrive
  make core-package CORE_IDS="gpsp gambatte"
  make core-archive CORE=picodrive

Packaging and device:
  make sdcard-package
  make sd-zip        Build $sdzip
  make install       Copy firmware and SD files to SDCARD=$sdcard
  make refresh-sd    Build, install, and sync SD files

Focused checks:
  make dev-check     Link firmware and build only QUICK_CORE=quicknes
  make quick-core-check QUICK_CORE=quicknes
  make host-quick-check  Run fast Linux behavior tests
  make host-verify   Run Linux tests, visuals, and supported sanitizers
  make linux-runner  Build the Linux UniFrog runner
  make linux-run     Run the Linux runner in the terminal
  make linux-cores   Build native libretro cores for the Linux runners
  make architecture-check  Enforce component dependency directions
  make frontend-structure-check  Enforce frontend source layout and names
  make component-sizes  Report linked firmware size by component archive
  make repo-check core-smoke-check frontend-check js2300-check
  make boot-logo-check host-frontend-check host-visual-check config-check
  make fastboot-only-check layout-check asdcheck fastboot-check
  make qemu-smoke       Boot the freshly built ASD in frog2k-qemu
  make qemu-launch-test TEST_CORE=<id> TEST_ROM=<zip> [TEST_FRAMES=n]
                        Auto-launch a ROM through a core in frog2k-qemu and
                        verify a clean session from the device log

Host frontend:
  make linux-runner-check  Build and script the Linux runner
  make host-frontend-check  Test shared menus, core registry, and screenshots
  make host-frontend-run    Open the shared frontend model through libxcb
  make linux-xcb-run        Open the Linux frontend through libxcb

Discovery:
  make print-config
  make help-options
  make -C cores help
  make js2300-check
  Override paths in untracked config.mk, or on the command line.
EOF
	;;
options)
	cat <<'EOF'
Common options:
  V=1                         Show compiler/linker commands
  BUILD_PROGRESS=1            Show progress labels without command echo
  CORE_IDS="gpsp gambatte"    Limit fetched, built, and packaged cores
  FAST_BUILD=0                Build every core including the slow ones
                              (fbalpha2012, mame2000, gpsp, gpsp-gbac-prosty,
                              picodrive, qpsx); sd-zip does this implicitly
  QUICK_CORE=quicknes         Core built by make dev-check and quick-core-check
  LINUX_CORE_IDS="gambatte"   Limit native cores built for Linux runners
  DEP_CHECKOUT=full           Keep full dependency source trees
  DEP_DEPTH=0                 Fetch normal dependency history
  HCRTOS_MEDIA=native         Link native FFmpeg media into unifrog.bin
  HCRTOS_MEDIA=module         Keep HCRTOS media in an SD-loaded module
  HCRTOS_MEDIA=firmware       Link hcplayer/HCRTOS media into unifrog.bin
  MMC_HOST_IMPL=source        Use the source HC15xx MMC host implementation
  MMC_HOST_IMPL=vendor        Keep linking libmmchosthc15.a for fallback

SD diagnostics:
  Boot SD profile is fixed to wide25. Change storage profiles at runtime.
  SD_FORCE_PIO=1              Enable the slow vendor PIO diagnostic path
  SD_DMA_MODE=wrap            Observe stock DMA through linker wrappers
  SD_DMA_MODE=quirks          Enable UniFrog DMA bounce/cache overrides
  STORAGE_BOOT_MOUNT=1        Try the risky pre-menu SD mount path
  make mmc-host-vendor-extract  Dump nm/objdump/strings from libmmchosthc15.a

Logging:
  LOG_AUTO_FLUSH_BYTES=4096   Set buffered log flush threshold
  LOG_DISK_WRITES=0           Keep logs/reports in retained RAM only

All runtime configuration, including advanced media tuning, lives in the
self-documenting /unifrog_data/unifrog.ini.
EOF
	;;
*)
	echo "usage: tools/make-help.sh [common|options]" >&2
	exit 2
	;;
esac
