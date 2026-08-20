#!/bin/sh
set -eu

: "${HOSTCC:=cc}"
: "${BUILD:=build}"
: "${CORE_INCLUDE:=.deps/cores/libretro-common/include}"
: "${JS2300:=js2300}"

probe="$BUILD/host-sanitize-probe"
mkdir -p "$BUILD"
if ! printf 'int main(void) { return 0; }\n' |
	"$HOSTCC" -x c - -o "$probe" -fsanitize=address,undefined \
		-fno-omit-frame-pointer >/dev/null 2>&1; then
	echo "SKIP host sanitizers: $HOSTCC does not support them"
	rm -f "$probe"
	exit 0
fi
rm -f "$probe"

binary="$BUILD/host-foundation-sanitize"
sanitize_build_run() {
	binary=$1
	shift
	"$HOSTCC" -std=c99 -O1 -g -Wall -Wextra -Iinclude -Itests/host \
		-Ijs2300/include \
		-I"$CORE_INCLUDE" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		"$@" -o "$binary"
	"$binary"
}

sanitize_build_run "$binary" tests/host/foundation_test.c \
	foundation/src/runtime/unifrog_path.c \
	foundation/src/runtime/unifrog_artwork.c \
	foundation/src/runtime/unifrog_battery_policy.c \
	foundation/src/runtime/unifrog_boot_policy.c \
	foundation/src/runtime/unifrog_log_policy.c \
	foundation/src/archive/unifrog_zip.c \
	foundation/src/archive/unifrog_zip_writer.c \
	foundation/src/storage/unifrog_storage_profile.c \
	foundation/src/display/unifrog_text.c -lz
sanitize_build_run "$BUILD/host-media-policy-sanitize" \
	tests/host/media_policy_test.c \
	components/media/src/unifrog_media_policy.c \
	components/media/src/unifrog_media_content.c \
	foundation/src/content/unifrog_reader_content.c
sanitize_build_run "$BUILD/host-diagnostic-policy-sanitize" \
	tests/host/diagnostic_policy_test.c \
	components/diagnostics/src/unifrog_diagnostic_policy.c
sanitize_build_run "$BUILD/host-libretro-policy-sanitize" \
	tests/host/libretro_policy_test.c \
	components/libretro/src/unifrog_libretro_policy.c
sanitize_build_run "$BUILD/host-config-sanitize" \
	tests/host/config_test.c \
	foundation/src/config/unifrog_config.c \
	components/frontend/src/app/frontend_config.c \
	components/libretro/src/unifrog_libretro_policy.c
sanitize_build_run "$BUILD/host-frontend-controller-sanitize" \
	tests/host/frontend_controller_test.c \
	components/frontend/src/app/frontend_controller.c \
	components/frontend/src/app/frontend_model.c \
	foundation/src/storage/unifrog_storage_profile.c
sanitize_build_run "$BUILD/host-frontend-services-sanitize" \
	-Icomponents/frontend/src/app \
	-I"$JS2300/include" -I"$BUILD" \
	tests/host/frontend_services_test.c \
	components/frontend/src/app/frontend_services.c
sanitize_build_run "$BUILD/host-core-registry-sanitize" \
	tools/core_registry_check.c \
	components/frontend/src/app/frontend_core_registry.c
echo "OK host sanitizers"
