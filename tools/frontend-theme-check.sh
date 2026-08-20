#!/bin/sh
set -eu

: "${BUILD:?}"
: "${THEME_ARCHIVE_CHECK:?}"
: "${THEME_VISUAL_CHECK:?}"

work=$BUILD/theme-check
rm -rf "$work"
mkdir -p "$work/theme/640x480/scheme" "$work/theme/640x480/image/wall"
cat >"$work/theme/640x480/scheme/default.ini" <<'EOF'
[background]
BACKGROUND = 101820
BACKGROUND_ALPHA = 255
[list]
LIST_DEFAULT_BACKGROUND = 202830
LIST_DEFAULT_BACKGROUND_ALPHA = 128
LIST_FOCUS_BACKGROUND = 405060
LIST_FOCUS_BACKGROUND_ALPHA = 220
LIST_DEFAULT_TEXT = EEF1E8
EOF
printf 'png placeholder\n' >"$work/theme/640x480/image/wall/default.png"
(cd "$work/theme" && zip -q -r ../sample.muxthm .)
"$THEME_ARCHIVE_CHECK" "$work/sample.muxthm"
"$THEME_VISUAL_CHECK" "$work/theme/640x480/scheme/default.ini" "$work/preview.ppm"

if [ -f /tmp/unifrog-theme-test/Analogue.muxthm ]; then
	"$THEME_ARCHIVE_CHECK" /tmp/unifrog-theme-test/Analogue.muxthm
	unzip -p /tmp/unifrog-theme-test/Analogue.muxthm 640x480/scheme/default.ini >"$work/Analogue-default.ini"
	"$THEME_VISUAL_CHECK" "$work/Analogue-default.ini" "$work/Analogue-preview.ppm"
fi

if [ -d /tmp/unifrog-themes ]; then
	for theme in /tmp/unifrog-themes/*.muxthm; do
		test -f "$theme" || continue
		"$THEME_ARCHIVE_CHECK" "$theme"
	done
fi
