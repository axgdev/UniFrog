#!/bin/sh
# End-to-end core launch test under frog2k-qemu.
#
# Builds a small FAT32 SD image containing the packaged frontend files, one
# core module, and a test ROM, boots bisrv.asd with an auto-launch settings
# file, then checks the device log for a clean session.
#
# usage: sh tools/qemu_launch_test.sh CORE=<id> ROM=<zip> [FRAMES=240]
#          [IMAGE=path] [KEEP_IMAGE=1] [BOOT_WAIT=90]
#
# Requires mtools, frog2k-qemu (make qemu-build), built firmware, and the
# packaged core (make; make -C cores <CORE>).
set -eu

CORE=fbalpha2012
ROM=
FRAMES=240
IMAGE=
KEEP_IMAGE=0
BOOT_WAIT=90

for arg in "$@"; do
	case "$arg" in
	CORE=*) CORE=${arg#CORE=} ;;
	ROM=*) ROM=${arg#ROM=} ;;
	FRAMES=*) FRAMES=${arg#FRAMES=} ;;
	IMAGE=*) IMAGE=${arg#IMAGE=} ;;
	KEEP_IMAGE=*) KEEP_IMAGE=${arg#KEEP_IMAGE=} ;;
	BOOT_WAIT=*) BOOT_WAIT=${arg#BOOT_WAIT=} ;;
	*) echo "unknown argument: $arg" >&2; exit 2 ;;
	esac
done

test -n "$ROM" || { echo "usage: $0 CORE=<id> ROM=<zip> [FRAMES=n]" >&2; exit 2; }
test -f "$ROM" || { echo "missing ROM: $ROM" >&2; exit 2; }
test -s bisrv.asd || { echo "missing bisrv.asd; run make first" >&2; exit 2; }
QEMU_BIN=${QEMU_BIN:-${QEMU_WORK:-/tmp/sf2000-qemu}/qemu-${QEMU_VERSION:-10.2.2}/build/qemu-system-mipsel}
export QEMU_BIN
test -x "$QEMU_BIN" || { echo "missing $QEMU_BIN; run make qemu-build" >&2; exit 2; }
command -v mcopy >/dev/null || { echo "mtools is required" >&2; exit 2; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/unifrog-qemu-test.XXXXXX")
cleanup() {
	rm -rf "$WORK"
	if test "$KEEP_IMAGE" != 1 && test -n "$IMAGE"; then
		rm -f "$IMAGE"
	fi
}
trap cleanup EXIT INT TERM

if test -z "$IMAGE"; then
	IMAGE="$WORK/sd.img"
fi

dd if=/dev/zero of="$IMAGE" bs=1M count=40 status=none
mkfs.vfat -F 32 -n UNIFROG "$IMAGE" >/dev/null
for dir in unifrog unifrog/cores unifrog_data unifrog_data/languages \
	unifrog_data/fonts ROMS ROMS/Arcade; do
	mmd -i "$IMAGE" "::/$dir"
done
mcopy -i "$IMAGE" -Q output/sdcard/unifrog/manifest.ini \
	output/sdcard/unifrog/unifrog.ini ::/unifrog/
mcopy -i "$IMAGE" -Q "output/sdcard/unifrog/cores/$CORE.bin" ::/unifrog/cores/
for lang in languages/*.ini; do
	mcopy -i "$IMAGE" -Q "$lang" ::/unifrog_data/languages/
done
test ! -f fonts/unifrog-ui.ttf ||
	mcopy -i "$IMAGE" -Q fonts/unifrog-ui.ttf ::/unifrog_data/fonts/

rom_name=$(basename "$ROM")
mcopy -i "$IMAGE" -Q "$ROM" ::/ROMS/Arcade/
{
	echo "audio=1"
	echo "test_launch_path=/media/mmcblk0/ROMS/Arcade/$rom_name"
	echo "test_launch_core=$CORE"
	echo "test_launch_frames=$FRAMES"
} > "$WORK/unifrog.ini"
mcopy -i "$IMAGE" -Q -o "$WORK/unifrog.ini" ::/unifrog_data/unifrog.ini

printf 'sleep %s\nquit\n' "$BOOT_WAIT" > "$WORK/qmp.script"

echo "QEMU    launch test core=$CORE rom=$rom_name frames=$FRAMES"
QEMU_TIMEOUT=$((BOOT_WAIT + 60)) \
	sh tools/qemu_run.sh "$IMAGE" "$WORK/qmp.script" > "$WORK/run.log" 2>&1 || {
	cat "$WORK/run.log" >&2
	echo "FAIL    qemu run" >&2
	exit 1
}

mcopy -i "$IMAGE" -o ::/unifrog_data/logs/log_*.txt "$WORK/" 2>/dev/null

log=$(ls "$WORK"/log_*.txt 2>/dev/null | head -1)
test -n "$log" || { echo "FAIL    no device log extracted" >&2; exit 1; }
mkdir -p build/qemu
cp "$log" build/qemu/launch-test-last.log
log=build/qemu/launch-test-last.log

session=$(awk '/frontend test launch /{found=NR}
	found && NR >= found' "$log")

test -n "$session" || {
	echo "FAIL    auto-launch did not fire; see $log" >&2
	exit 1
}

echo "$session" | grep -Fq "core_module loaded id=$CORE" || {
	echo "FAIL    core module did not load; see $log" >&2
	exit 1
}

if echo "$session" | grep -Eq 'type=reset|exception retained'; then
	echo "FAIL    exception during session; see $log" >&2
	exit 1
fi

echo "$session" | grep -Eq "dispatch core=$CORE ret=0" || {
	echo "FAIL    session did not end cleanly; see $log" >&2
	exit 1
}

if echo "$session" | grep -Fq 'Cannot load this game'; then
	echo "FAIL    core rejected the ROM; see $log" >&2
	exit 1
fi

echo "OK      qemu launch test ($FRAMES frames, ret=0)"
