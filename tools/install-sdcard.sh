#!/bin/sh
set -eu

: "${ASD:?}"
: "${FASTBOOT_ASD:?}"
: "${OUT:?}"
: "${SDCARD:?}"
: "${SDCARD_BIOS_DIR:?}"
: "${SDCARD_FIRMWARE_DIR:?}"
: "${SDCARD_USER_DIR:?}"
: "${FRONTEND_PACKAGE:?}"
: "${THIRD_PARTY_NOTICE:?}"

progress() {
	if [ "${BUILD_PROGRESS:-0}" = 1 ]; then
		printf '  %-7s %s\n' "$1" "$2"
	fi
}

progress CLEAN "stale root firmware files"
rm -f "$SDCARD/$ASD" "$SDCARD/$FASTBOOT_ASD" "$SDCARD/unifrog.bin" \
	"$SDCARD/firmware/unifrog.bin"
rmdir "$SDCARD/firmware" 2>/dev/null || true

progress INSTALL "$SDCARD_BIOS_DIR/bisrv.asd"
mkdir -p "$SDCARD_BIOS_DIR"
cp "$FASTBOOT_ASD" "$SDCARD_BIOS_DIR/bisrv.asd"

progress INSTALL "$SDCARD_FIRMWARE_DIR/unifrog.bin"
mkdir -p "$SDCARD_FIRMWARE_DIR"
cp "$OUT/unifrog.bin" "$SDCARD_FIRMWARE_DIR/unifrog.bin"

progress INSTALL "$SDCARD/unifrog"
mkdir -p "$SDCARD/unifrog"
rm -rf "$SDCARD/unifrog/cores" "$SDCARD/unifrog/modules"
rm -rf "$SDCARD/unifrog/app" "$SDCARD/unifrog/main.js" \
	"$SDCARD/unifrog/main.js.mqbc" \
	"$SDCARD/unifrog/quick-menu.js" \
	"$SDCARD/unifrog/bytecode-manifest.txt"
cp -R "$FRONTEND_PACKAGE"/. "$SDCARD/unifrog/"
cp LICENSE "$SDCARD/unifrog/LICENSE.txt"
cp "$THIRD_PARTY_NOTICE" "$SDCARD/unifrog/THIRD_PARTY.md"

progress INSTALL "$SDCARD_USER_DIR"
mkdir -p "$SDCARD_USER_DIR/saves" "$SDCARD_USER_DIR/cache" \
	"$SDCARD_USER_DIR/logs" "$SDCARD_USER_DIR/logs/crashlogs" \
	"$SDCARD_USER_DIR/logs/rotatedlogs" "$SDCARD_USER_DIR/logs/reports" \
	"$SDCARD_USER_DIR/updates" "$SDCARD_USER_DIR/versions" \
	"$SDCARD_USER_DIR/themes" "$SDCARD_USER_DIR/languages" \
	"$SDCARD_USER_DIR/fonts" "$SDCARD_USER_DIR/artwork" \
	"$SDCARD_USER_DIR/archive" "$SDCARD_USER_DIR/scripts" \
	"$SDCARD_USER_DIR/firmware"

if [ ! -e "$SDCARD_USER_DIR/unifrog.ini" ] &&
   [ -f "$FRONTEND_PACKAGE/unifrog.ini" ]; then
	cp "$FRONTEND_PACKAGE/unifrog.ini" "$SDCARD_USER_DIR/unifrog.ini"
fi

if [ -n "${LANGUAGE_FILES:-}" ]; then
	cp $LANGUAGE_FILES "$SDCARD_USER_DIR/languages/"
fi
if [ -n "${FONT_FILES:-}" ]; then
	cp $FONT_FILES "$SDCARD_USER_DIR/fonts/"
fi
if [ -n "${SCRIPT_FILES:-}" ]; then
	for script in $SCRIPT_FILES; do
		relative=${script#"$SCRIPT_ROOT"/}
		target="$SDCARD_USER_DIR/scripts/$relative"
		mkdir -p "$(dirname "$target")"
		cp "$script" "$target"
	done
fi

sync
