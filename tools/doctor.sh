#!/bin/sh
set -eu

mode=${1:-full}

require_cmd_words() {
	for cmd in $1; do
		command -v "$cmd" >/dev/null || { echo "missing: $cmd" >&2; exit 1; }
	done
}

require_path() {
	test -e "$1" || { echo "missing: $1" >&2; exit 1; }
}

echo "Toolchain: $TOOLCHAIN"
require_cmd_words "$CC"
require_cmd_words "$AR"
require_cmd_words "$HOSTCC"
test -n "$GCC_LIBDIR" || { echo "missing: GCC libdir under $TOOLCHAIN/lib/gcc/mipsel-mti-elf" >&2; exit 1; }
require_path "$SDK/include"
require_path "$SDK/Makefile"
require_path "$CORES/Makefile"

case "$mode" in
smoke)
	require_path "$CORE_SOURCE_ROOT/libretro-common/.git"
	require_path "$CORE_SOURCE_ROOT/QuickNES_Core/.git"
	require_path "$CORE_SUPPORT_ROOT/zlib/inflate.c"
	;;
full)
	require_cmd_words "$LD"
	require_cmd_words "$NM"
	require_cmd_words "$READELF"
	require_cmd_words "$OBJCOPY"
	require_cmd_words "$DTC"
	require_path "$SYS_LIBDIR"
	require_path "$SDK/lib"
	require_path "$SDK/lib/core/libm.a"
	require_path "$HCRTOS_FFMPEG_SOURCE/configure"
	require_path "$DTS"
	require_path "$CORE_SOURCE_ROOT/libretro-common/.git"
	require_path "$CORE_SUPPORT_ROOT/zstd/build/single_file_libs/zstddeclib-in.c"
	require_path "$CORE_SUPPORT_ROOT/zlib/inflate.c"
	require_path "$LZ4_DIR/lib/lz4frame.c"
	require_path "$STB_DIR/stb_truetype.h"
	if [ "$PACKAGE_NEEDS_CHD" = 1 ]; then
		require_path "$CORE_SUPPORT_ROOT/libchdr/src/libchdr_chd.c"
	fi
	;;
*)
	echo "usage: tools/doctor.sh full|smoke" >&2
	exit 2
	;;
esac

require_path "$JS2300/Makefile"

echo "OK"
if [ "$mode" = full ]; then
	echo "Run 'make print-config' to show resolved paths and tools."
fi
