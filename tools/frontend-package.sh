#!/bin/sh
set -eu

for name in FRONTEND_PACKAGE USER_PACKAGE CORE_PACKAGE FRONTEND_MANIFEST SETTINGS_EXAMPLE SETTINGS_PACKAGE ASSOCIATIONS_DEFAULT THIRD_PARTY_NOTICE; do
	eval "value=\${$name:-}"
	if [ -z "$value" ]; then
		echo "missing environment: $name" >&2
		exit 2
	fi
done

rm -rf "$FRONTEND_PACKAGE/app" "$FRONTEND_PACKAGE/user" \
	"$FRONTEND_PACKAGE/saves" "$FRONTEND_PACKAGE/cache" \
	"$FRONTEND_PACKAGE/logs" "$FRONTEND_PACKAGE/updates" \
	"$FRONTEND_PACKAGE/versions" "$FRONTEND_PACKAGE/themes" \
	"$FRONTEND_PACKAGE/languages" "$FRONTEND_PACKAGE/archive" \
	"$FRONTEND_PACKAGE/licenses" \
	"$FRONTEND_PACKAGE/scripts" \
	"$FRONTEND_PACKAGE/main.js" "$FRONTEND_PACKAGE/main.js.mqbc" \
	"$FRONTEND_PACKAGE/quick-menu.js" \
	"$FRONTEND_PACKAGE/bytecode-manifest.txt"
rm -f "$CORE_PACKAGE/pmp-video.bin"
rm -rf "$USER_PACKAGE/probes" "$USER_PACKAGE/languages" "$USER_PACKAGE/scripts"
mkdir -p "$FRONTEND_PACKAGE/firmware" \
	"$FRONTEND_PACKAGE/licenses" \
	"$USER_PACKAGE/saves" "$USER_PACKAGE/cache" \
	"$USER_PACKAGE/logs" "$USER_PACKAGE/logs/crashlogs" \
	"$USER_PACKAGE/logs/rotatedlogs" "$USER_PACKAGE/logs/reports" \
	"$USER_PACKAGE/logs/frontend-driver" \
	"$USER_PACKAGE/updates" "$USER_PACKAGE/versions" \
	"$USER_PACKAGE/themes" "$USER_PACKAGE/languages" \
	"$USER_PACKAGE/fonts" "$USER_PACKAGE/artwork" \
	"$USER_PACKAGE/frogui/fonts" \
	"$USER_PACKAGE/archive" "$USER_PACKAGE/scripts" \
	"$USER_PACKAGE/bug-reports" \
	"$USER_PACKAGE/firmware"

if [ -n "${LANGUAGE_FILES:-}" ]; then
	cp $LANGUAGE_FILES "$USER_PACKAGE/languages/"
fi
if [ -n "${FONT_FILES:-}" ]; then
	cp $FONT_FILES "$USER_PACKAGE/fonts/"
fi
if [ -n "${FROGUI_FONT_FILES:-}" ]; then
	cp $FROGUI_FONT_FILES "$USER_PACKAGE/frogui/fonts/"
	cat > "$USER_PACKAGE/frogui/launcher.frogui" <<'EOF'
# FrogUI launcher marker. This file contains no settings.
# Configure FrogUI in the documented [frogui] section of /unifrog_data/unifrog.ini.
frontend=frogui
EOF
fi
if [ -n "${CORE_LICENSE_SPECS:-}" ]; then
	for spec in $CORE_LICENSE_SPECS; do
		rest=${spec#*|}
		source=${rest%%|*}
		destination=${rest#*|}
		cp "$source" "$FRONTEND_PACKAGE/licenses/$destination"
	done
fi
if [ -n "${SCRIPT_FILES:-}" ]; then
	for script in $SCRIPT_FILES; do
		relative=${script#"$SCRIPT_ROOT"/}
		target="$USER_PACKAGE/scripts/$relative"
		mkdir -p "$(dirname "$target")"
		cp "$script" "$target"
	done
fi

{
	printf '%s\n' 'manifest_version=1'
	printf '%s\n' "firmware_commit=$UNIFROG_GIT_COMMIT"
	printf '%s\n' "firmware_dirty=$UNIFROG_GIT_DIRTY"
	printf '%s\n' "sdk_commit=$UNIFROG_SDK_GIT_COMMIT"
	printf '%s\n' "cores_commit=$UNIFROG_CORES_GIT_COMMIT"
	printf '%s\n' "js2300_commit=$UNIFROG_JS2300_GIT_COMMIT"
	printf '%s\n' "frontend_commit=$UNIFROG_FRONTEND_GIT_COMMIT"
	printf '%s\n' "hcrtos_media=$HCRTOS_MEDIA"
	printf '%s\n' "frontend_impl=frontend"
	printf '%s\n' "generated_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} >"$FRONTEND_MANIFEST"

cp LICENSE "$FRONTEND_PACKAGE/LICENSE.txt"
cp "$THIRD_PARTY_NOTICE" "$FRONTEND_PACKAGE/THIRD_PARTY.md"
{
	echo "# UniFrog configuration."
	echo "#"
	echo "# User configuration lives at /unifrog_data/unifrog.ini."
	echo "# Lines beginning with # or ; are comments. Values use key=value."
	echo "#"
	echo "# Add [core.CORE_ID] or [rom.ABSOLUTE_PATH] sections for launch overrides."
	echo "# Supported override keys: core, audio, gain, cpu, ge_clock, backlight,"
	echo "# frameskip, display, framebuffer, keymap, state_slot, state_auto_load,"
	echo "# state_auto_save, and rtc_offset_minutes. The core key is ROM-only."
	echo "# ROM values override"
	echo "# core values; all value types and ranges are documented below."
	echo "# The in-game UniFrog and Core Options menus can save or clear these"
	echo "# sections for the current core or game without removing the SD card."
	echo "#"
	echo "# Libretro variables exposed by a core use separate option sections:"
	echo "# [core-options.CORE_ID] and [rom-options.ABSOLUTE_PATH]."
	echo "# Option names and accepted values are supplied by each installed core."
	echo
	awk '!/^# UniFrog runtime configuration defaults[.]$/ && \
	     index($0, "# The installer copies this") != 1 && \
	     index($0, "# Edit that file") != 1 && \
	     index($0, "# Lines beginning") != 1' \
		"$SETTINGS_EXAMPLE"
	echo
	cat "$ASSOCIATIONS_DEFAULT"
	echo
	cat <<'EOF'

[reader]
# Reader preferences. All are configurable in the reader menu.
# font_bitmap: 0=vector font, 1=small bitmap font.
# text_percent: 75, 90, 100, 125, or 150.
# margin: 4, 10, or 16 pixels. palette: 0=light, 1=dark, 2=sepia.
# epub_images: 0 hides EPUB images; 1 includes them.
font_bitmap=0
text_percent=100
margin=10
palette=0
epub_images=1
EOF
} > "$SETTINGS_PACKAGE"
grep -q '^\[reader\]$' "$SETTINGS_PACKAGE"
grep -q '^# text_percent: 75, 90, 100, 125, or 150[.]$' "$SETTINGS_PACKAGE"
grep -q '^epub_images=1$' "$SETTINGS_PACKAGE"
rm -f "$FRONTEND_PACKAGE/settings.example.ini" \
	"$FRONTEND_PACKAGE/media.example.ini" "$FRONTEND_PACKAGE/associations.ini"
rm -f "$FRONTEND_PACKAGE"/.package.*.stamp
touch "$FRONTEND_PACKAGE_STAMP"
