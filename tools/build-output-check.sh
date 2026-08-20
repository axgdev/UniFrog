#!/bin/sh
set -eu

mode=${1:-full}

require_file() {
	test -s "$1" || { echo "missing or empty: $1" >&2; exit 1; }
}

require_bins() {
	for bin in $1; do
		require_file "$bin"
	done
}

require_common_package() {
	require_file "$OUT_UNIFROG_BIN"
	require_file "$SDCARD_BIOS_PACKAGE"
	require_file "$SDCARD_FIRMWARE_PACKAGE"
	require_file "$FRONTEND_MANIFEST"
	require_file "$FRONTEND_PACKAGE/LICENSE.txt"
	require_file "$FRONTEND_PACKAGE/THIRD_PARTY.md"
	require_bins "${LIBRETRO_CORE_BINS:-}"
	require_bins "${HCRTOS_MEDIA_MODULE_BINS:-}"
	if [ -n "${BLUEMSX_SYSTEM_PACKAGE:-}" ]; then
		require_file "$BLUEMSX_SYSTEM_PACKAGE/Machines/MSX - C-BIOS/config.ini"
		require_file "$BLUEMSX_SYSTEM_PACKAGE/Databases/msxromdb.xml"
	fi
}

case "$mode" in
fastboot)
	require_file "$FASTBOOT_ASD"
	require_common_package
	"$ASDPACK" --check "$FASTBOOT_ASD"
	;;
full)
	require_file "$ASD"
	require_file "$LIBUNIFROG"
	require_common_package
	"$ASDPACK" --check "$ASD"
	require_file "$OUT_UNIFROG_BIN"
	;;
*)
	echo "usage: tools/build-output-check.sh fastboot|full" >&2
	exit 2
	;;
esac
