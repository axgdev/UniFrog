# Configuration discovery and generated default files.

print-config:
	@echo "TOOLCHAIN=$(TOOLCHAIN)"
	@echo "SDK=$(SDK)"
	@echo "DEPS=$(DEPS)"
	@echo "DEP_CHECKOUT=$(DEP_CHECKOUT)"
	@echo "DEP_DEPTH=$(DEP_DEPTH)"
	@echo "CORES=$(CORES)"
	@echo "CORE_IDS=$(CORE_IDS)"
	@echo "FAST_BUILD=$(CORE_BATCH_FAST)"
	@echo "SLOW_CORE_IDS=$(SLOW_CORE_IDS)"
	@echo "EFFECTIVE_CORE_IDS=$(EFFECTIVE_CORE_IDS)"
	@echo "PACKAGE_NEEDS_CHD=$(PACKAGE_NEEDS_CHD)"
	@echo "CORE_SOURCE_ROOT=$(CORE_SOURCE_ROOT)"
	@echo "CORE_SUPPORT_ROOT=$(CORE_SUPPORT_ROOT)"
	@echo "JS2300=$(JS2300)"
	@echo "LVGL_DIR=$(LVGL_DIR)"
	@echo "LZ4_DIR=$(LZ4_DIR)"
	@echo "STB_DIR=$(STB_DIR)"
	@echo "HCRTOS_FFMPEG_URL=$(HCRTOS_FFMPEG_URL)"
	@echo "HCRTOS_FFMPEG_REF=$(HCRTOS_FFMPEG_REF)"
	@echo "HCRTOS_FFMPEG_COMMIT=$(HCRTOS_FFMPEG_COMMIT)"
	@echo "HCRTOS_FFMPEG_PATCH=$(HCRTOS_FFMPEG_PATCH)"
	@echo "HCRTOS_FFMPEG_SOURCE=$(HCRTOS_FFMPEG_SOURCE)"
	@echo "HCRTOS_FFMPEG_INSTALL=$(HCRTOS_FFMPEG_INSTALL)"
	@echo "HOSTCC=$(HOSTCC)"
	@echo "DTC=$(DTC)"
	@echo "SD_MODE=$(SD_MODE)"
	@echo "SD_FORCE_PIO=$(SD_FORCE_PIO)"
	@echo "SD_DMA_MODE=$(SD_DMA_MODE)"
	@echo "STORAGE_BOOT_MOUNT=$(STORAGE_BOOT_MOUNT)"
	@echo "LOG_AUTO_FLUSH_BYTES=$(LOG_AUTO_FLUSH_BYTES)"
	@echo "LOG_FLUSH_EVERY=$(LOG_FLUSH_EVERY)"
	@echo "LOG_DISK_WRITES=$(LOG_DISK_WRITES)"
	@echo "HCRTOS_MEDIA=$(HCRTOS_MEDIA)"
	@echo "SD_CLOCK_FREQUENCY=$(SD_CLOCK_FREQUENCY)"
	@echo "SD_BUS_WIDTH=$(SD_BUS_WIDTH)"
	@echo "SD_CAP_HIGHSPEED=$(SD_CAP_HIGHSPEED)"
	@echo "SD_CAP_UHS=$(SD_CAP_UHS)"
	@echo "SD_UHS_SDR12=$(SD_UHS_SDR12)"
	@echo "SD_UHS_SDR25=$(SD_UHS_SDR25)"
	@echo "SD_UHS_SDR50=$(SD_UHS_SDR50)"
	@echo "SD_EXPERIMENTAL=$(SD_EXPERIMENTAL)"
	@echo "CCACHE=$(if $(CCACHE),$(CCACHE),disabled)"
	@echo "ARCH_CFLAGS=$(ARCH_CFLAGS)"
	@echo "OPT_SIZE=$(OPT_SIZE)"
	@echo "OPT_FAST=$(OPT_FAST)"
	@echo "OPT_AUDIO=$(OPT_AUDIO)"
	@echo "FOUNDATION_CFLAGS=$(FOUNDATION_CFLAGS)"
	@echo "FRONTEND_CFLAGS=$(FRONTEND_CFLAGS)"
	@echo "LIBRETRO_CFLAGS=$(LIBRETRO_CFLAGS)"
	@echo "MEDIA_CFLAGS=$(MEDIA_CFLAGS)"
	@echo "DIAGNOSTICS_CFLAGS=$(DIAGNOSTICS_CFLAGS)"

$(SETTINGS_EXAMPLE): config/options.mk | $(BUILD)
	$(LOG_ECHO) "  GEN     $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)printf '%s\n' $(RUNTIME_SETTINGS_LINES) > "$@"

$(CONFIG_EXAMPLE): config/options.mk | $(BUILD)
	$(LOG_ECHO) "  GEN     $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)printf '%s\n' $(CONFIG_EXAMPLE_LINES) > "$@"

$(DEFAULT_OPTIONS_HEADER): config/options.mk | $(BUILD)
	$(LOG_ECHO) "  GEN     $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)printf '%s\n' $(DEFAULT_OPTIONS_HEADER_LINES) > "$@"

config-check: $(SETTINGS_EXAMPLE) $(CONFIG_EXAMPLE) $(DEFAULT_OPTIONS_HEADER)
	$(LOG_ECHO) "  CHECK   config manifest"
	@set -e; \
	for key in $(RUNTIME_SETTING_KEYS); do \
		if command -v rg >/dev/null 2>&1; then \
			rg -q "frontend_read_key_value\\(line, \"$$key\"|\"$$key\"" \
				"components/frontend/src/app"; \
		else \
			grep -REq "frontend_read_key_value\\(line, \"$$key\"|\"$$key\"" \
				"components/frontend/src/app"; \
		fi || { echo "runtime setting not loaded: $$key"; exit 1; }; \
	done
	@test -s "$(SETTINGS_EXAMPLE)"
	@test -s "$(CONFIG_EXAMPLE)"
	@echo "OK"
