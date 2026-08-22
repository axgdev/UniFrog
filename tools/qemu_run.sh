#!/bin/sh
# Boot UniFrog under frog2k-qemu and drive it through the QMP socket.
#
# usage: sh tools/qemu_run.sh <sd.img> <qmp-script> [extra qemu args...]
#
# The QMP script supports: sleep <s>, shot <file.ppm>, down <qcodes>,
# up <qcodes>, quit. Key qcodes use QEMU names (ret, backspace, z, x, ...).
set -eu

IMG="$1"
SCRIPT="$2"
shift 2
WORK=${QEMU_WORK_DIR:-build/qemu}
QEMU_BIN=${QEMU_BIN:-${QEMU_WORK:-/tmp/sf2000-qemu}/qemu-${QEMU_VERSION:-10.2.2}/build/qemu-system-mipsel}
QEMU_BIOS=${QEMU_BIOS:-${QEMU_DIR:-../sf2000_qemu}/firmware/SF2000_XMC_XM25QH40B_4mbit_bugfix.bin}
KERNEL=${KERNEL:-bisrv.asd}

mkdir -p "$WORK"
# Kill stale emulators by exact process name; -f would also match this
# shell when the caller embeds the emulator path in its own command line.
for qname in qemu-system-mips qemu-system-mipsel; do
	pkill -9 -x "$qname" 2>/dev/null || :
done
sleep 1
rm -f "$WORK/qmp.sock" "$WORK/exitcode"

(
	timeout "${QEMU_TIMEOUT:-120}" "$QEMU_BIN" -M sf2000 -bios "$QEMU_BIOS" \
		-kernel "$KERNEL" \
		-drive if=none,id=sd0,file="$IMG",format=raw \
		-display none -serial none \
		-d guest_errors,unimp -D "$WORK/trace.log" \
		-qmp unix:"$WORK/qmp.sock",server,nowait "$@" \
		> "$WORK/console.log" 2>&1
	echo $? > "$WORK/exitcode"
) &

python3 tools/qemu_drive.py "$WORK/qmp.sock" "$SCRIPT"
wait
status=$(cat "$WORK/exitcode" 2>/dev/null || echo killed)
echo "qemu exit: $status"
test "$status" = 0 || test "$status" = 124
