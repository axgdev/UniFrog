# Host-side checks that do not require an SF2000 or a display server.

FRONTEND_MODEL_VIEWER := $(BUILD)/frontend_model_viewer
FRONTEND_MODEL_VIEWER_XCB := $(BUILD)/frontend_model_viewer_xcb
LINUX_RUNNER := $(BUILD)/linux/unifrog-linux
LINUX_XCB_RUNNER := $(BUILD)/linux/unifrog-linux-xcb
LINUX_CORE_ROOT ?= $(abspath $(BUILD)/linux/sdcard/unifrog/cores)
LINUX_DUMMY_CORE := $(LINUX_CORE_ROOT)/dummy_libretro.so
LINUX_DUMMY_ARCHIVE_CORE := $(LINUX_CORE_ROOT)/dummy_archive_libretro.so
LINUX_CORE_BUILD_ROOT := $(abspath $(BUILD)/linux/core-src)
LINUX_CORE_IDS ?= gambatte gpsp snes9x2005 snes9x2002 quicknes fceumm gearboy pce-fast
LINUX_CORE_CC ?= $(or $(shell command -v cc 2>/dev/null),cc)
LINUX_CORE_CXX ?= $(or $(shell command -v c++ 2>/dev/null),c++)
LINUX_CORE_NM ?= $(or $(shell command -v nm 2>/dev/null),nm)
LINUX_RUNNER_ROM_ROOT := $(LINUX_SD_ROOT)/ROMS
LINUX_RUNNER_TEST_ROM := $(LINUX_RUNNER_ROM_ROOT)/sample.gb
LINUX_RUNNER_TEST_ARCHIVE := $(LINUX_RUNNER_ROM_ROOT)/ARCADE/1943.zip
LINUX_SD_ROOT ?= $(abspath $(BUILD)/linux/sdcard)
HOST_VISUAL_DIR ?= $(BUILD)/host-visual
HOST_VISUAL_ARTIFACTS := \
	$(HOST_VISUAL_DIR)/launcher.ppm \
	$(HOST_VISUAL_DIR)/config.ppm \
	$(HOST_VISUAL_DIR)/storage.ppm \
	$(HOST_VISUAL_DIR)/storage-mode.ppm
CORE_REGISTRY_CHECK := $(BUILD)/core_registry_check
FRONTEND_MODEL_CHECK := $(BUILD)/frontend_model_check
HOST_FOUNDATION_TEST := $(BUILD)/host-foundation-test
HOST_FRONTEND_CONTROLLER_TEST := $(BUILD)/host-frontend-controller-test
HOST_FRONTEND_SERVICES_TEST := $(BUILD)/host-frontend-services-test
HOST_FRONTEND_ROM_ROOTS_TEST := $(BUILD)/host-frontend-rom-roots-test
HOST_LIBRETRO_POLICY_TEST := $(BUILD)/host-libretro-policy-test
HOST_MEDIA_POLICY_TEST := $(BUILD)/host-media-policy-test
HOST_DIAGNOSTIC_POLICY_TEST := $(BUILD)/host-diagnostic-policy-test
HOST_CONFIG_TEST := $(BUILD)/host-config-test
HOST_ASSOCIATIONS_TEST := $(BUILD)/host-associations-test
FROGUI_HOST_TEST := $(BUILD)/frogui-host-test
FROGUI_HOST_SD := $(abspath $(BUILD)/frogui-host-sd)
FRONTEND_MODEL_VIEWER_SOURCES := \
	tools/frontend_model_viewer.c \
	components/frontend/src/app/frontend_controller.c \
	components/frontend/src/app/frontend_model.c \
	foundation/src/storage/unifrog_storage_profile.c
LINUX_COMMON_SOURCES := \
	foundation/src/config/unifrog_config.c \
	foundation/src/platform/linux/unifrog_linux_platform.c \
	foundation/src/platform/linux/unifrog_linux_display.c \
	foundation/src/platform/linux/unifrog_linux_audio.c \
	components/frontend/src/app/frontend_model.c \
	components/frontend/src/app/frontend_core_registry.c \
	components/frontend/src/app/frontend_lvgl.c \
	components/frontend/src/libretro_frontend/libretro_frontend_quick_menu.c \
	components/libretro/src/unifrog_libretro_host.c \
	components/libretro/src/unifrog_libretro_runtime.c \
	components/libretro/src/unifrog_libretro_content.c \
	components/libretro/src/unifrog_libretro_session.c \
	components/libretro/src/libretro_host_trampoline.c \
	components/libretro/src/unifrog_libretro_policy.c \
	components/media/src/unifrog_media_policy.c \
	components/diagnostics/src/unifrog_diagnostic_policy.c \
	foundation/src/content/unifrog_reader_content.c \
	foundation/src/archive/unifrog_zip.c \
	foundation/src/archive/unifrog_zip_writer.c \
	foundation/src/runtime/unifrog_bug_report.c \
	foundation/src/runtime/unifrog_clock.c \
	foundation/src/display/unifrog_presenter.c \
	foundation/src/display/unifrog_gfx.c \
	foundation/src/display/unifrog_png.c \
	foundation/src/display/unifrog_surface_alloc.c \
	foundation/src/display/unifrog_text.c \
	foundation/src/display/unifrog_ui.c \
	foundation/src/storage/unifrog_storage_io.c \
	foundation/src/storage/unifrog_storage_profile.c \
	foundation/src/runtime/unifrog_battery_policy.c \
	foundation/src/runtime/unifrog_artwork.c \
	foundation/src/runtime/unifrog_boot_policy.c \
	foundation/src/runtime/unifrog_log_policy.c
LINUX_PRODUCTION_FRONTEND_APP_SOURCES := $(filter-out \
	components/frontend/src/app/frontend_controller.c \
	components/frontend/src/app/frontend_core_registry.c \
	components/frontend/src/app/frontend_model.c \
	components/frontend/src/app/frontend_lvgl.c, \
	$(FRONTEND_APP_SOURCES))
LINUX_PRODUCTION_FRONTEND_SOURCES := \
	foundation/src/platform/linux/unifrog_linux_frontend_services.c \
	components/media/src/unifrog_media_config.c \
	components/media/src/unifrog_media_content.c \
	$(LINUX_PRODUCTION_FRONTEND_APP_SOURCES) \
	foundation/src/display/unifrog_image.c \
	components/frontend/src/reader/unifrog_reader_ui.c
LINUX_RUNNER_SOURCES := \
	apps/linux/src/main.c \
	apps/linux/src/frontend_headless.c \
	$(LINUX_COMMON_SOURCES) \
	$(LINUX_PRODUCTION_FRONTEND_SOURCES)
LINUX_XCB_FRONTEND_SOURCES := \
	apps/linux/src/frontend_xcb_main.c \
	apps/linux/src/frontend_xcb_window.c \
	apps/linux/src/frontend_headless.c \
	$(LINUX_PRODUCTION_FRONTEND_SOURCES)

.PHONY: host-check host-quick-check host-full-check host-verify \
	host-sanitize-check architecture-check frontend-structure-check host-visual-check \
	host-frontend-check host-frontend-run core-registry-check \
	frontend-model-check foundation-host-check frontend-controller-host-check \
	frontend-services-host-check \
	frontend-rom-roots-host-check \
	libretro-policy-host-check media-policy-host-check \
	diagnostic-policy-host-check config-host-check associations-host-check \
	frogui-host-check \
	linux-runner-check linux-runner linux-run \
	linux-cores linux-xcb linux-xcb-check linux-xcb-run

host-check: host-quick-check
	@echo "OK"

host-quick-check: architecture-check frontend-structure-check foundation-host-check \
		frontend-controller-host-check \
		frontend-services-host-check \
		frontend-rom-roots-host-check \
		config-host-check \
		associations-host-check \
		frogui-host-check \
		libretro-policy-host-check \
		media-policy-host-check diagnostic-policy-host-check \
		linux-runner-check \
		frontend-model-check core-registry-check
	@echo "OK"

host-full-check: host-visual-check host-sanitize-check
	@echo "OK"

host-verify: host-quick-check host-full-check
	@echo "OK"

architecture-check:
	$(LOG_ECHO) "  CHECK   architecture boundaries"
	$(Q)sh tools/architecture-check.sh

frontend-structure-check:
	$(LOG_ECHO) "  CHECK   frontend source layout"
	$(Q)sh tools/frontend-structure-check.sh

foundation-host-check: $(HOST_FOUNDATION_TEST)
	$(LOG_ECHO) "  CHECK   foundation host behavior"
	$(Q)$(HOST_FOUNDATION_TEST)

libretro-policy-host-check: $(HOST_LIBRETRO_POLICY_TEST)
	$(LOG_ECHO) "  CHECK   libretro policy behavior"
	$(Q)$(HOST_LIBRETRO_POLICY_TEST)

frontend-controller-host-check: $(HOST_FRONTEND_CONTROLLER_TEST)
	$(LOG_ECHO) "  CHECK   frontend controller behavior"
	$(Q)$(HOST_FRONTEND_CONTROLLER_TEST)

frontend-services-host-check: $(HOST_FRONTEND_SERVICES_TEST)
	$(LOG_ECHO) "  CHECK   frontend launch services"
	$(Q)$(HOST_FRONTEND_SERVICES_TEST)

frontend-rom-roots-host-check: $(HOST_FRONTEND_ROM_ROOTS_TEST)
	$(LOG_ECHO) "  CHECK   frontend ROM roots"
	$(Q)$(HOST_FRONTEND_ROM_ROOTS_TEST)

media-policy-host-check: $(HOST_MEDIA_POLICY_TEST)
	$(LOG_ECHO) "  CHECK   media policy behavior"
	$(Q)$(HOST_MEDIA_POLICY_TEST)

diagnostic-policy-host-check: $(HOST_DIAGNOSTIC_POLICY_TEST)
	$(LOG_ECHO) "  CHECK   diagnostic policy behavior"
	$(Q)$(HOST_DIAGNOSTIC_POLICY_TEST)

config-host-check: $(HOST_CONFIG_TEST)
	$(LOG_ECHO) "  CHECK   unified configuration behavior"
	$(Q)$(HOST_CONFIG_TEST)

associations-host-check: $(HOST_ASSOCIATIONS_TEST)
	$(LOG_ECHO) "  CHECK   file association behavior"
	$(Q)$(HOST_ASSOCIATIONS_TEST)

frogui-host-check: $(FROGUI_HOST_TEST)
	$(LOG_ECHO) "  CHECK   FrogUI frontend integration"
	$(Q)rm -rf "$(FROGUI_HOST_SD)"
	$(Q)$(FROGUI_HOST_TEST)

host-sanitize-check: $(DEFAULT_OPTIONS_HEADER)
	$(LOG_ECHO) "  CHECK   host sanitizers"
	$(Q)HOSTCC="$(HOSTCC)" BUILD="$(BUILD)" JS2300="$(JS2300)" \
		CORE_INCLUDE="$(CORE_SOURCE_ROOT)/libretro-common/include" \
		sh tools/host-sanitize.sh

host-frontend-check: frontend-model-check core-registry-check host-visual-check
	@echo "OK"

host-visual-check: $(FRONTEND_MODEL_VIEWER)
	$(LOG_ECHO) "  CHECK   shared frontend visual artifacts"
	$(Q)rm -rf "$(HOST_VISUAL_DIR)"
	$(Q)mkdir -p "$(HOST_VISUAL_DIR)"
	$(Q)$(FRONTEND_MODEL_VIEWER) --headless "$(HOST_VISUAL_DIR)"
	$(Q)for artifact in $(HOST_VISUAL_ARTIFACTS); do test -s "$$artifact"; done
	$(LOG_ECHO) "  OK      $(HOST_VISUAL_DIR)"

host-frontend-run: $(FRONTEND_MODEL_VIEWER_XCB)
	$(Q)$(FRONTEND_MODEL_VIEWER_XCB) --xcb

linux-runner: $(LINUX_RUNNER)
	@echo "$(LINUX_RUNNER)"

linux-cores:
	$(Q)CORE_SOURCE_ROOT="$(abspath $(CORE_SOURCE_ROOT))" \
		BUILD_ROOT="$(LINUX_CORE_BUILD_ROOT)" OUTPUT_ROOT="$(LINUX_CORE_ROOT)" \
		CORE_IDS="$(LINUX_CORE_IDS)" JOBS="$(JOBS)" \
		HOST_CC="$(LINUX_CORE_CC)" HOST_CXX="$(LINUX_CORE_CXX)" \
		HOST_NM="$(LINUX_CORE_NM)" \
		VERBOSE="$(if $(V),1,0)" \
		sh tools/build-linux-cores.sh

linux-run: $(LINUX_RUNNER)
	$(Q)$(LINUX_RUNNER)

linux-xcb: $(LINUX_XCB_RUNNER)
	@echo "$(LINUX_XCB_RUNNER)"

linux-xcb-check: $(LINUX_XCB_RUNNER)
	$(LOG_ECHO) "  CHECK   linux XCB runner"
	$(Q)test -x "$(LINUX_XCB_RUNNER)"
	$(Q)mkdir -p "$(LINUX_SD_ROOT)/ROMS" "$(LINUX_SD_ROOT)/unifrog" \
		"$(LINUX_SD_ROOT)/unifrog_data"
	$(Q)$(LINUX_XCB_RUNNER) --render-ppm \
		"$(BUILD)/linux/frontend.ppm" >/dev/null 2>&1
	$(Q)test -s "$(BUILD)/linux/frontend.ppm"
	$(Q)test "$$(od -An -tu1 -j 15 "$(BUILD)/linux/frontend.ppm" | \
		tr -s ' ' '\n' | awk '$$1 > 0 { count++ } END { print count + 0 }')" \
		-gt 100
	$(Q)$(LINUX_XCB_RUNNER) --script down,down,down,enter \
		--render-ppm "$(BUILD)/linux/apps.ppm" >/dev/null 2>&1
	$(Q)test -s "$(BUILD)/linux/apps.ppm"
	$(Q)! cmp -s "$(BUILD)/linux/frontend.ppm" "$(BUILD)/linux/apps.ppm"
	$(Q)$(LINUX_XCB_RUNNER) \
		--script down,down,down,enter,down,down,down,down,down,enter \
		--render-ppm "$(BUILD)/linux/cores.ppm" \
		>"$(BUILD)/linux/cores.log" 2>&1
	$(Q)test -s "$(BUILD)/linux/cores.ppm"
	$(Q)grep -Eq 'frontend core_registry count=[1-9][0-9]*' \
		"$(BUILD)/linux/cores.log"

linux-xcb-run: $(LINUX_XCB_RUNNER)
	$(Q)$(LINUX_XCB_RUNNER) --xcb

linux-runner-check: $(LINUX_RUNNER) $(LINUX_DUMMY_CORE) \
		$(LINUX_DUMMY_ARCHIVE_CORE)
	$(LOG_ECHO) "  CHECK   linux runner"
	$(Q)mkdir -p "$(LINUX_RUNNER_ROM_ROOT)" "$(LINUX_SD_ROOT)/unifrog_data"
	$(Q)dd if=/dev/zero of="$(LINUX_RUNNER_TEST_ROM)" bs=1024 count=1 >/dev/null 2>&1
	$(Q)$(LINUX_RUNNER) \
		--render-ppm "$(BUILD)/linux/runner.ppm" \
		>/dev/null 2>&1
	$(Q)test -s "$(BUILD)/linux/runner.ppm"
	$(Q)$(LINUX_RUNNER) \
		--script "down, down, down, enter" \
		--render-ppm "$(BUILD)/linux/apps.ppm" \
		>/dev/null 2>&1
	$(Q)test -s "$(BUILD)/linux/apps.ppm"
	$(Q)! cmp -s "$(BUILD)/linux/runner.ppm" "$(BUILD)/linux/apps.ppm"
	$(Q)$(LINUX_RUNNER) \
		--script down,down,down,enter,enter \
		--render-ppm "$(BUILD)/linux/browser.ppm" \
		>/dev/null 2>&1
	$(Q)test -s "$(BUILD)/linux/browser.ppm"
	$(Q)! cmp -s "$(BUILD)/linux/apps.ppm" "$(BUILD)/linux/browser.ppm"
	$(Q)printf '%s\n' \
		'[core-options.dummy]' \
		'unifrog_dummy_speed=slow' \
		'[rom-options.$(LINUX_RUNNER_TEST_ROM)]' \
		'unifrog_dummy_speed=fast' \
		> "$(LINUX_SD_ROOT)/unifrog_data/unifrog.ini"
	$(Q)$(LINUX_RUNNER) --run-rom "$(LINUX_RUNNER_TEST_ROM)" \
		--core-id dummy --core-path "$(LINUX_DUMMY_CORE)" --max-frames 3 \
		>"$(BUILD)/linux/core-run.log" 2>&1
	$(Q)rg -q 'dummy core option unifrog_dummy_speed=fast' \
		"$(BUILD)/linux/core-run.log"
	$(Q)mkdir -p "$(dir $(LINUX_RUNNER_TEST_ARCHIVE))"
	$(Q)printf 'multi-file arcade archive placeholder\n' \
		> "$(LINUX_RUNNER_TEST_ARCHIVE)"
	$(Q)$(LINUX_RUNNER) --run-rom "$(LINUX_RUNNER_TEST_ARCHIVE)" \
		--core-id dummy-archive --core-path "$(LINUX_DUMMY_ARCHIVE_CORE)" \
		--max-frames 3 >"$(BUILD)/linux/archive-core-run.log" 2>&1
	$(Q)rg -q 'kind=native_archive' "$(BUILD)/linux/archive-core-run.log"
	$(Q)! $(LINUX_RUNNER) --run-rom "$(LINUX_RUNNER_TEST_ROM)" \
		--core-id dummy --core-path "$(LINUX_DUMMY_CORE)" \
		--max-frames invalid >/dev/null 2>&1

core-registry-check: $(CORE_REGISTRY_CHECK)
	$(LOG_ECHO) "  CHECK   installed core registry"
	$(Q)$(CORE_REGISTRY_CHECK)

frontend-model-check: $(FRONTEND_MODEL_CHECK)
	$(LOG_ECHO) "  CHECK   shared frontend model"
	$(Q)$(FRONTEND_MODEL_CHECK)

$(FRONTEND_MODEL_VIEWER): $(FRONTEND_MODEL_VIEWER_SOURCES) include/unifrog/frontend_model.h \
		include/unifrog/storage_profile.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -Iinclude $(FRONTEND_MODEL_VIEWER_SOURCES) -o $@

$(FRONTEND_MODEL_VIEWER_XCB): $(FRONTEND_MODEL_VIEWER_SOURCES) include/unifrog/frontend_model.h \
		include/unifrog/storage_profile.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)command -v pkg-config >/dev/null
	$(Q)pkg-config --exists xcb
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -DUNIFROG_HOST_XCB=1 -Iinclude \
		$$(pkg-config --cflags xcb) $(FRONTEND_MODEL_VIEWER_SOURCES) \
		$$(pkg-config --libs xcb) -o $@

$(LINUX_RUNNER): $(LINUX_RUNNER_SOURCES) $(DEFAULT_OPTIONS_HEADER) \
		include/unifrog/frontend_controller.h \
		include/unifrog/frontend_model.h include/unifrog/storage_profile.h \
		include/unifrog/core_registry.h include/unifrog/platform.h \
		include/unifrog/perf.h include/unifrog/backlight.h \
		include/unifrog/scpu.h include/unifrog/battery.h \
		include/unifrog/input.h include/unifrog/libretro_policy.h \
		include/unifrog/media_policy.h include/unifrog/diagnostic_policy.h \
		include/unifrog/zip.h include/unifrog/zlib_port.h \
		include/unifrog/fb.h include/unifrog/ge.h include/unifrog/ui.h \
		include/unifrog/frontend_lvgl.h include/unifrog/png.h \
		include/unifrog/audio.h include/unifrog/libretro_host.h \
		include/unifrog/libretro_session.h include/unifrog/frontend_services.h \
		| $(BUILD) linux-cores
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -I$(BUILD) \
		-Ifoundation/src -Icomponents/frontend/src/app \
		-Icomponents/media/src -Ijs2300/include -I$(JS2300)/include \
		-I$(NANOSVG_DIR) \
		-I$(CORE_SOURCE_ROOT)/libretro-common/include \
		-I$(STB_DIR) -I$(LZ4_DIR)/lib -I$(ZSTD_DIR) \
		-I$(ZSTD_DIR)/common -I$(ZSTD_DIR)/decompress \
		-DUNIFROG_GFX_NO_LVGL=1 -DUNIFROG_LIBRETRO_NATIVE_DLOPEN=1 \
		-DUNIFROG_LIBRETRO_NO_COMPRESSED=1 \
		-DUNIFROG_CORE_REGISTRY_NATIVE_DLOPEN=1 \
		-DUNIFROG_LOG_AUTO_FLUSH_BYTES=$(LOG_AUTO_FLUSH_BYTES) \
		-DUNIFROG_SD_ROOT=\"$(LINUX_SD_ROOT)\" \
		$(LINUX_RUNNER_SOURCES) -pthread -lz -ldl -lm -o $@

$(LINUX_XCB_RUNNER): $(LINUX_COMMON_SOURCES) $(LINUX_XCB_FRONTEND_SOURCES) \
		$(LINUX_DUMMY_CORE) \
		$(DEFAULT_OPTIONS_HEADER) \
		include/unifrog/frontend_controller.h \
		include/unifrog/frontend_model.h include/unifrog/storage_profile.h \
		include/unifrog/core_registry.h include/unifrog/platform.h \
		include/unifrog/perf.h include/unifrog/backlight.h \
		include/unifrog/scpu.h include/unifrog/battery.h \
		include/unifrog/input.h include/unifrog/libretro_policy.h \
		include/unifrog/media_policy.h include/unifrog/diagnostic_policy.h \
		include/unifrog/zip.h include/unifrog/zlib_port.h \
		include/unifrog/fb.h include/unifrog/ge.h include/unifrog/ui.h \
		include/unifrog/frontend_lvgl.h include/unifrog/png.h \
		include/unifrog/audio.h include/unifrog/libretro_host.h \
		include/unifrog/libretro_session.h include/unifrog/frontend_services.h \
		| $(BUILD) linux-cores
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)command -v pkg-config >/dev/null
	$(Q)pkg-config --exists xcb
	$(Q)mkdir -p $(dir $@)
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -I$(BUILD) \
		-Ifoundation/src -Icomponents/frontend/src/app \
		-Icomponents/media/src -Ijs2300/include -I$(JS2300)/include \
		-I$(NANOSVG_DIR) \
		-I$(CORE_SOURCE_ROOT)/libretro-common/include \
		-I$(STB_DIR) -I$(LZ4_DIR)/lib -I$(ZSTD_DIR) \
		-I$(ZSTD_DIR)/common -I$(ZSTD_DIR)/decompress \
		$$(pkg-config --cflags xcb) \
		-DUNIFROG_GFX_NO_LVGL=1 -DUNIFROG_LIBRETRO_NATIVE_DLOPEN=1 \
		-DUNIFROG_LIBRETRO_NO_COMPRESSED=1 -DUNIFROG_LINUX_XCB=1 \
		-DUNIFROG_CORE_REGISTRY_NATIVE_DLOPEN=1 \
		-DUNIFROG_LOG_AUTO_FLUSH_BYTES=$(LOG_AUTO_FLUSH_BYTES) \
		-DUNIFROG_SD_ROOT=\"$(LINUX_SD_ROOT)\" \
		$(LINUX_COMMON_SOURCES) $(LINUX_XCB_FRONTEND_SOURCES) -pthread -lz -ldl -lm \
		$$(pkg-config --libs xcb) -o $@

$(LINUX_DUMMY_CORE): tools/linux_dummy_libretro.c \
		$(CORE_SOURCE_ROOT)/libretro-common/include/libretro.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -fPIC -shared \
		-I$(CORE_SOURCE_ROOT)/libretro-common/include \
		tools/linux_dummy_libretro.c -o $@

$(LINUX_DUMMY_ARCHIVE_CORE): tools/linux_dummy_libretro.c \
		$(CORE_SOURCE_ROOT)/libretro-common/include/libretro.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -fPIC -shared \
		-DUNIFROG_DUMMY_FULLPATH=1 \
		-I$(CORE_SOURCE_ROOT)/libretro-common/include \
		tools/linux_dummy_libretro.c -o $@

$(CORE_REGISTRY_CHECK): tools/core_registry_check.c \
		components/frontend/src/app/frontend_core_registry.c \
		include/unifrog/core_registry.h include/unifrog/core_module.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -Iinclude \
		-I$(CORE_SOURCE_ROOT)/libretro-common/include \
		tools/core_registry_check.c \
		components/frontend/src/app/frontend_core_registry.c -o $@

$(FRONTEND_MODEL_CHECK): tools/frontend_model_check.c \
		components/frontend/src/app/frontend_model.c \
		foundation/src/storage/unifrog_storage_profile.c \
		foundation/src/display/unifrog_text.c include/unifrog/frontend_model.h \
		include/unifrog/storage_profile.h include/unifrog/text.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -Iinclude \
		tools/frontend_model_check.c \
		components/frontend/src/app/frontend_model.c \
		foundation/src/storage/unifrog_storage_profile.c \
		foundation/src/display/unifrog_text.c -o $@

$(HOST_FOUNDATION_TEST): tests/host/foundation_test.c tests/host/test.h \
		foundation/src/runtime/unifrog_path.c \
		foundation/src/storage/unifrog_storage_profile.c \
		foundation/src/runtime/unifrog_battery_policy.c \
		foundation/src/runtime/unifrog_artwork.c \
		foundation/src/runtime/unifrog_boot_policy.c \
		foundation/src/runtime/unifrog_log_policy.c \
		foundation/src/archive/unifrog_zip.c \
		foundation/src/archive/unifrog_zip_writer.c \
		foundation/src/display/unifrog_text.c include/unifrog/path.h \
		include/unifrog/storage_profile.h include/unifrog/text.h \
		include/unifrog/log.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -Itests/host \
		tests/host/foundation_test.c foundation/src/runtime/unifrog_path.c \
		foundation/src/storage/unifrog_storage_profile.c \
		foundation/src/runtime/unifrog_battery_policy.c \
		foundation/src/runtime/unifrog_artwork.c \
		foundation/src/runtime/unifrog_boot_policy.c \
		foundation/src/runtime/unifrog_log_policy.c \
		foundation/src/archive/unifrog_zip.c \
		foundation/src/archive/unifrog_zip_writer.c \
		foundation/src/display/unifrog_text.c \
		-lz -o $@

$(HOST_LIBRETRO_POLICY_TEST): tests/host/libretro_policy_test.c \
		tests/host/test.h components/libretro/src/unifrog_libretro_policy.c \
		include/unifrog/libretro_policy.h include/unifrog/libretro_host.h \
		include/unifrog/ge.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -Itests/host \
		tests/host/libretro_policy_test.c \
		components/libretro/src/unifrog_libretro_policy.c -o $@

$(HOST_FRONTEND_CONTROLLER_TEST): tests/host/frontend_controller_test.c \
		tests/host/test.h \
		components/frontend/src/app/frontend_controller.c \
		components/frontend/src/app/frontend_model.c \
		foundation/src/storage/unifrog_storage_profile.c \
		include/unifrog/frontend_controller.h \
		include/unifrog/frontend_model.h include/unifrog/storage_profile.h \
		| $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -Itests/host \
		tests/host/frontend_controller_test.c \
		components/frontend/src/app/frontend_controller.c \
		components/frontend/src/app/frontend_model.c \
		foundation/src/storage/unifrog_storage_profile.c -o $@

$(HOST_FRONTEND_SERVICES_TEST): tests/host/frontend_services_test.c \
		tests/host/test.h \
		components/frontend/src/app/frontend_services.c \
		components/frontend/src/app/frontend_internal.h \
		include/unifrog/frontend_services.h \
		include/unifrog/libretro_host.h include/unifrog/media.h \
		include/unifrog/reader.h include/unifrog/bug_report.h \
		$(DEFAULT_OPTIONS_HEADER) | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -Itests/host \
		-Icomponents/frontend/src/app \
		-Ijs2300/include -I$(JS2300)/include -I$(BUILD) \
		-I$(CORE_SOURCE_ROOT)/libretro-common/include \
		tests/host/frontend_services_test.c \
		components/frontend/src/app/frontend_services.c -o $@

$(HOST_FRONTEND_ROM_ROOTS_TEST): tests/host/frontend_rom_roots_test.c \
		tests/host/test.h \
		components/frontend/src/app/frontend_rom_roots.c \
		components/frontend/src/app/frontend_paths.c \
		components/frontend/src/app/frontend_text.c \
		foundation/src/display/unifrog_text.c \
		components/frontend/src/app/frontend_internal.h \
		include/unifrog/paths.h include/unifrog/text.h \
		$(DEFAULT_OPTIONS_HEADER) | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -Itests/host \
		-Icomponents/frontend/src/app \
		-I$(BUILD) \
		-I$(CORE_SOURCE_ROOT)/libretro-common/include \
		tests/host/frontend_rom_roots_test.c \
		components/frontend/src/app/frontend_rom_roots.c \
		components/frontend/src/app/frontend_paths.c \
		components/frontend/src/app/frontend_text.c \
		foundation/src/display/unifrog_text.c -o $@

$(HOST_MEDIA_POLICY_TEST): tests/host/media_policy_test.c tests/host/test.h \
		components/media/src/unifrog_media_policy.c \
		components/media/src/unifrog_media_content.c \
		foundation/src/content/unifrog_reader_content.c \
		include/unifrog/media_policy.h include/unifrog/media_content.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -Itests/host \
		tests/host/media_policy_test.c \
		components/media/src/unifrog_media_policy.c \
		components/media/src/unifrog_media_content.c \
		foundation/src/content/unifrog_reader_content.c -o $@

$(HOST_DIAGNOSTIC_POLICY_TEST): tests/host/diagnostic_policy_test.c \
		tests/host/test.h \
		components/diagnostics/src/unifrog_diagnostic_policy.c \
		include/unifrog/diagnostic_policy.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -Itests/host \
		tests/host/diagnostic_policy_test.c \
		components/diagnostics/src/unifrog_diagnostic_policy.c -o $@

$(HOST_CONFIG_TEST): tests/host/config_test.c tests/host/test.h \
		foundation/src/config/unifrog_config.c \
		components/frontend/src/app/frontend_config.c \
		components/libretro/src/unifrog_libretro_policy.c \
		include/unifrog/config.h include/unifrog/frontend_config.h \
		include/unifrog/libretro_host.h include/unifrog/libretro_policy.h \
		| $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -Itests/host \
		tests/host/config_test.c foundation/src/config/unifrog_config.c \
		components/frontend/src/app/frontend_config.c \
		components/libretro/src/unifrog_libretro_policy.c -o $@

$(HOST_ASSOCIATIONS_TEST): tests/host/associations_test.c tests/host/test.h \
		components/frontend/src/app/frontend_associations.c \
		foundation/src/display/unifrog_text.c \
		components/frontend/src/app/frontend_internal.h \
		include/unifrog/config.h include/unifrog/text.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -Itests/host \
		-Icomponents/frontend/src/app \
		-I$(BUILD) \
		-I$(CORE_SOURCE_ROOT)/libretro-common/include \
		tests/host/associations_test.c \
		components/frontend/src/app/frontend_associations.c \
		foundation/src/display/unifrog_text.c -o $@

$(FROGUI_HOST_TEST): tools/frogui_host_check.c \
		$(FROGUI_SOURCE_DIR)/frogui_libretro.c \
		$(FROGUI_SOURCE_DIR)/font.c $(FROGUI_SOURCE_DIR)/render.c \
		$(FROGUI_SOURCE_DIR)/theme.c $(FROGUI_SOURCE_DIR)/stb_truetype.h \
		foundation/src/config/unifrog_config.c \
		include/unifrog/config.h include/unifrog/libretro_extension.h \
		include/unifrog/paths.h | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -std=c99 -Iinclude -I$(FROGUI_SOURCE_DIR) \
		-DSCREEN_WIDTH=320 -DSCREEN_HEIGHT=240 -DUI_SCALE=75 \
		-DUNIFROG_SD_ROOT=\"$(FROGUI_HOST_SD)\" \
		tools/frogui_host_check.c \
		$(FROGUI_SOURCE_DIR)/frogui_libretro.c \
		$(FROGUI_SOURCE_DIR)/font.c $(FROGUI_SOURCE_DIR)/render.c \
		$(FROGUI_SOURCE_DIR)/theme.c foundation/src/config/unifrog_config.c \
		-lm -o $@
