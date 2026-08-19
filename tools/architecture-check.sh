#!/bin/sh
set -eu

failed=0

reject_matches() {
	label=$1
	pattern=$2
	shift 2
	matches=$(rg -n "$pattern" "$@" 2>/dev/null || true)
	if [ -n "$matches" ]; then
		echo "architecture violation: $label" >&2
		printf '%s\n' "$matches" >&2
		failed=1
	fi
}

# Generic engines must not know about presentation ownership.
reject_matches "libretro imports frontend" \
	'#include [<"].*(frontend|native_frontend)' components/libretro/src
if [ -e components/libretro/src/unifrog_libretro_quick_menu.c ]; then
	echo "architecture violation: quick-menu UI must be frontend-owned" >&2
	failed=1
fi
reject_matches "media imports frontend" \
	'#include [<"].*(frontend|native_frontend)' components/media/src
reject_matches "frontend imports libretro internals" \
	'#include [<"].*unifrog_libretro_internal' components/frontend/src
launch_service_bypasses=$(rg -n \
	'unifrog_libretro_run_path_ex|unifrog_media_play_video_ex|unifrog_reader_run|js2300_run_script_file|unifrog_media_run_audio_diagnostics_ex|unifrog_bug_report_create' \
	components/frontend/src/app --glob '!frontend_services.c' 2>/dev/null || true)
if [ -n "$launch_service_bypasses" ]; then
	echo "architecture violation: frontend bypasses launch services" >&2
	printf '%s\n' "$launch_service_bypasses" >&2
	failed=1
fi
launch_module_bypasses=$(rg -n \
	'frontend_services_(run_game|play_media|run_reader|run_script|run_audio_diagnostics)' \
	components/frontend/src/app --glob '*.c' \
	--glob '!**/frontend_services.c' --glob '!**/frontend_launch.c' \
	2>/dev/null || true)
if [ -n "$launch_module_bypasses" ]; then
	echo "architecture violation: content launch flows must stay in frontend_launch.c" >&2
	printf '%s\n' "$launch_module_bypasses" >&2
	failed=1
fi
frontend_app_flow_leaks=$(rg -n \
	'install_theme_archive|install_update_archive|activate_installed_version|unifrog_storage_fast_probe_run|frontend_services_create_bug_report|unifrog_diag_memory_snapshot|unifrog_core_registry_(find_path|read_header)' \
	components/frontend/src/app/frontend_app.c 2>/dev/null || true)
if [ -n "$frontend_app_flow_leaks" ]; then
	echo "architecture violation: frontend_app.c must stay a thin app shell" >&2
	printf '%s\n' "$frontend_app_flow_leaks" >&2
	failed=1
fi

if [ -d components/frontend/src/native ]; then
	echo "architecture violation: obsolete frontend src/native directory must stay removed" >&2
	failed=1
fi
if [ -e include/unifrog/native_frontend.h ]; then
	echo "architecture violation: public frontend header is include/unifrog/frontend.h" >&2
	failed=1
fi
if find components/frontend/src include/unifrog tests mk docs -name '*native_frontend*' -print -quit | grep -q .; then
	echo "architecture violation: frontend code must not use native_frontend filenames" >&2
	find components/frontend/src include/unifrog tests mk docs -name '*native_frontend*' -print >&2
	failed=1
fi
reject_matches "frontend text uses obsolete native frontend name" \
	'(^|[^A-Za-z])native[ _-]frontend|native_frontend|Native Frontend|Native frontend' \
	components/frontend/src include/unifrog tests mk docs Makefile
reject_matches "build uses obsolete frontend native variables" \
	'FRONTEND_NATIVE_(SOURCES|OBJECTS)' Makefile mk components js2300

if [ -e include/unifrog/unifrog.h ]; then
	echo "architecture violation: include the needed unifrog header directly, not unifrog/unifrog.h" >&2
	failed=1
fi
if [ -e include/unifrog/hcrtos_media_compat.h ]; then
	echo "architecture violation: HCRTOS media ABI declarations live in hcrtos_media_abi.h" >&2
	failed=1
fi
if [ -e include/unifrog/zlib_compat.h ]; then
	echo "architecture violation: zlib portability include is zlib_port.h" >&2
	failed=1
fi
if [ -e components/libretro/src/native_libretro_tramp.c ]; then
	echo "architecture violation: host libretro trampoline source is libretro_host_trampoline.c" >&2
	failed=1
fi
reject_matches "source includes obsolete compatibility header names" \
	'unifrog/(hcrtos_media_compat|zlib_compat|unifrog)\.h|native_libretro_tramp' \
	Makefile mk components foundation include docs README.md THIRD_PARTY.md
reject_matches "build uses obsolete native module names" \
	'native_modules|NATIVE_MODULE_OUTS' Makefile components docs README.md THIRD_PARTY.md

if [ -e components/frontend/src/reader_content.c ]; then
	echo "architecture violation: shared reader content classification must stay in foundation" >&2
	failed=1
fi
if [ -e components/frontend/src/reader.c ]; then
	echo "architecture violation: reader UI must use the reader/unifrog_reader_ui.c path" >&2
	failed=1
fi
if [ -e components/frontend/src/libretro_frontend/libretro_quick_menu.c ]; then
	echo "architecture violation: quick-menu presentation must use the libretro_frontend_ prefix" >&2
	failed=1
fi
if [ -e foundation/src/runtime/unifrog_boot.c ]; then
	echo "architecture violation: SF2000 boot handoff implementation must stay in platform/sf2000" >&2
	failed=1
fi

# Shared components may use public platform services, but must not reach into
# an RTOS SDK, a concrete device node, or the SF2000 mount path directly.
reject_matches "shared component imports platform SDK" \
	'#include [<"](kernel|hcuapi|freertos)/' components/diagnostics \
		components/frontend/src components/libretro
reject_matches "shared component opens a device node" \
	'"/dev/' components/diagnostics components/frontend/src \
		components/libretro
reject_matches "shared component hard-codes the SF2000 mount" \
	'/media/mmcblk0' components/diagnostics components/frontend/src \
		components/libretro
reject_matches "portable media imports platform SDK" \
	'#include [<"](kernel|hcuapi|freertos)/' \
	components/media/src/unifrog_media_config.c \
	components/media/src/unifrog_media_content.c \
	components/media/src/unifrog_media_policy.c

# The reusable JS runtime is deliberately independent of UniFrog.
reject_matches "JS2300 runtime imports UniFrog" \
	'#include [<"]unifrog/' js2300/src/js2300_runtime.c \
	js2300/include/js2300/js2300.h

if find foundation components apps -path '*js2300*' -print -quit | grep -q .; then
	echo "architecture violation: JS2300 implementation exists outside js2300/" >&2
	find foundation components apps -path '*js2300*' -print >&2
	failed=1
fi

# Enforce the final layout as it appears during migration.
if [ -d foundation ]; then
	reject_matches "foundation imports components" \
		'#include [<"].*components/' foundation
	reject_matches "portable foundation imports platform SDK" \
		'#include [<"](kernel|hcuapi|freertos)/' \
		foundation/src/abi foundation/src/archive foundation/src/config \
		foundation/src/device foundation/src/display foundation/src/modules \
		foundation/src/storage
fi

project_includes=$(awk '
	/^PROJECT_INCLUDES :=/ { active=1 }
	active { print }
	/^FFMPEG_INCLUDES :=/ { active=0 }
' Makefile)
case "$project_includes" in
*-Icomponents/*)
	echo "architecture violation: PROJECT_INCLUDES exposes component private paths" >&2
	printf '%s\n' "$project_includes" >&2
	failed=1
	;;
esac

reject_matches "frontend manifest references another component" \
	'components/(libretro|media|diagnostics)/' components/frontend/sources.mk
reject_matches "libretro manifest references another component" \
	'components/(frontend|media|diagnostics)/' components/libretro/sources.mk
reject_matches "media manifest references another component" \
	'components/(frontend|libretro|diagnostics)/' components/media/sources.mk
reject_matches "diagnostics manifest references another component" \
	'components/(frontend|libretro|media)/' components/diagnostics/sources.mk
if [ -d components/libretro ]; then
	reject_matches "libretro component imports frontend" \
	'#include [<"].*(components/frontend|unifrog/(frontend|native_frontend))' \
		components/libretro
fi
if [ -d components/media ]; then
	reject_matches "media component imports frontend" \
	'#include [<"].*(components/frontend|unifrog/(frontend|native_frontend))' \
		components/media
fi

test "$failed" -eq 0
echo "OK architecture"
