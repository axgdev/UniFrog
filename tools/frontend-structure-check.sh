#!/bin/sh
set -eu

failed=0

require_path() {
	path=$1
	if [ ! -e "$path" ]; then
		echo "frontend structure violation: missing $path" >&2
		failed=1
	fi
}

reject_path() {
	path=$1
	reason=$2
	if [ -e "$path" ]; then
		echo "frontend structure violation: $reason: $path" >&2
		failed=1
	fi
}

require_manifest_entry() {
	path=$1
	if ! rg -q -F "$path" components/frontend/sources.mk; then
		echo "frontend structure violation: source manifest does not list $path" >&2
		failed=1
	fi
}

require_path include/unifrog/frontend.h
require_path components/frontend/src/app/frontend_internal.h
require_path components/frontend/src/app/frontend_app.c
require_path components/frontend/src/app/frontend_appearance.c
require_path components/frontend/src/app/frontend_apps.c
require_path components/frontend/src/app/frontend_browser.c
require_path components/frontend/src/app/frontend_draw.c
require_path components/frontend/src/app/frontend_input.c
require_path components/frontend/src/app/frontend_items.c
require_path components/frontend/src/app/frontend_paths.c
require_path components/frontend/src/app/frontend_launch.c
require_path components/frontend/src/app/frontend_maintenance.c
require_path components/frontend/src/app/frontend_preferences.c
require_path components/frontend/src/app/frontend_progress.c
require_path components/frontend/src/app/frontend_rom_roots.c
require_path components/frontend/src/app/frontend_services.c
require_path components/frontend/src/app/frontend_system.c
require_path components/frontend/src/app/frontend_theme.c
require_path components/frontend/src/app/frontend_lvgl.c
require_path components/frontend/src/libretro_frontend/libretro_frontend_quick_menu.c
require_path components/frontend/src/reader/unifrog_reader_ui.c
require_path tools/frontend_model_viewer.c

reject_path components/frontend/src/native "obsolete frontend src/native directory"
reject_path include/unifrog/native_frontend.h "obsolete public frontend header"
reject_path tools/host_frontend.c "obsolete shared frontend model viewer name"

for path in components/frontend/src/app/*.c; do
	require_manifest_entry "$path"
done

if find components/frontend/src include/unifrog tests mk docs -name '*native_frontend*' -print -quit | grep -q .; then
	echo "frontend structure violation: obsolete native_frontend filename" >&2
	find components/frontend/src include/unifrog tests mk docs -name '*native_frontend*' -print >&2
	failed=1
fi

stale_build_vars=$(rg -n 'FRONTEND_NATIVE_(SOURCES|OBJECTS)' Makefile mk components js2300 2>/dev/null || true)
if [ -n "$stale_build_vars" ]; then
	echo "frontend structure violation: obsolete FRONTEND_NATIVE build variable" >&2
	printf '%s\n' "$stale_build_vars" >&2
	failed=1
fi

stale_model_viewer_refs=$(rg -n 'tools/host_frontend\.c|\bHOST_FRONTEND(_XCB|_SOURCES)?\b|\$\(BUILD\)/host_frontend' \
	Makefile mk docs README.md tools/make-help.sh 2>/dev/null || true)
if [ -n "$stale_model_viewer_refs" ]; then
	echo "frontend structure violation: obsolete shared frontend model viewer name" >&2
	printf '%s\n' "$stale_model_viewer_refs" >&2
	failed=1
fi

if rg -n -F 'UNIFROG_FRONTEND_GIT_COMMIT := native' Makefile >/dev/null 2>&1; then
	echo "frontend structure violation: frontend commit identity still uses obsolete native placeholder" >&2
	rg -n -F 'UNIFROG_FRONTEND_GIT_COMMIT := native' Makefile >&2
	failed=1
fi

if rg -n -F 'frontend_impl=native' tools/frontend-package.sh >/dev/null 2>&1; then
	echo "frontend structure violation: package manifest still uses obsolete native frontend identity" >&2
	rg -n -F 'frontend_impl=native' tools/frontend-package.sh >&2
	failed=1
fi

matches=$(rg -n '(^|[^A-Za-z])native[ _-]frontend|native_frontend|Native Frontend|Native frontend' \
	components/frontend/src include/unifrog tests mk docs Makefile 2>/dev/null || true)
if [ -n "$matches" ]; then
	echo "frontend structure violation: obsolete native frontend wording" >&2
	printf '%s\n' "$matches" >&2
	failed=1
fi

test "$failed" -eq 0
echo "OK frontend structure"
