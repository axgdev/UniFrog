# Host architecture is needed by the default toolchain URL in config/options.mk.
TOOLCHAIN_UNAME_M := $(shell uname -m)
TOOLCHAIN_HOST_ARCH := $(if $(filter aarch64 arm64,$(TOOLCHAIN_UNAME_M)),arm64,$(if $(filter x86_64 amd64,$(TOOLCHAIN_UNAME_M)),x86_64,$(TOOLCHAIN_UNAME_M)))

# Local overrides are read before defaults so copied `?=` lines work.
-include config.mk
V ?=
include config/options.mk
DEP ?=
CORE ?=
QUICK_CORE ?= quicknes
DEP_EFFECTIVE := $(strip $(DEP))

ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif
ifeq ($(filter --output-sync% -O%,$(MAKEFLAGS)),)
MAKEFLAGS += --output-sync=target
endif
ifeq ($(V),)
MAKEFLAGS += --silent
endif

.SUFFIXES:
.DEFAULT_GOAL := all

# Output names and build directories.
BUILD := build
OUT := output
TARGET := sf2000
ASD := bisrv.asd
FASTBOOT_ASD := fastboot.asd
QEMU_DIR ?= ../sf2000_qemu
QEMU_WORK ?= /tmp/sf2000-qemu
QEMU_VERSION ?= 10.2.2
QEMU_BIN ?= $(QEMU_WORK)/qemu-$(QEMU_VERSION)/build/qemu-system-mipsel
QEMU_BIOS ?= $(QEMU_DIR)/firmware/SF2000_XMC_XM25QH40B_4mbit_bugfix.bin
QEMU_TIMEOUT ?= 30
QEMU_LOG_DIR ?= $(BUILD)/logs/qemu
QEMU_CONSOLE_LOG := $(QEMU_LOG_DIR)/unifrog.console
QEMU_TRACE_LOG := $(QEMU_LOG_DIR)/unifrog.trace
FRONTEND_PACKAGE := $(OUT)/sdcard/unifrog
CORE_PACKAGE := $(OUT)/sdcard/unifrog/cores
MODULE_PACKAGE := $(OUT)/sdcard/unifrog/modules
USER_PACKAGE := $(OUT)/sdcard/unifrog_data
FRONTEND_MANIFEST := $(FRONTEND_PACKAGE)/manifest.ini
SETTINGS_EXAMPLE := $(BUILD)/config/settings.example.ini
ASSOCIATIONS_DEFAULT := components/frontend/assets/defaults/associations.ini
CONFIG_EXAMPLE := $(BUILD)/config/config.example.mk
DEFAULT_OPTIONS_HEADER := $(BUILD)/unifrog_default_options.h
LANGUAGE_FILES := $(wildcard languages/*.ini)
FONT_FILES := $(wildcard fonts/*)
SCRIPT_ROOT := js2300/scripts
SCRIPT_FILES := $(shell find $(SCRIPT_ROOT) -type f 2>/dev/null | sort)
SDCARD_BIOS_PACKAGE := $(OUT)/sdcard/bios/bisrv.asd
SDCARD_FIRMWARE_PACKAGE := $(OUT)/sdcard/unifrog/firmware/unifrog.bin
SDZIP ?= $(OUT)/UniFrog-sdcard.zip
ZIP_COMPRESSION ?= -1
CI_SDZIP ?= $(abspath UniFrog-local-sdcard.zip)
CI_ZIP_COMPRESSION ?= -1
THIRD_PARTY_NOTICE ?= THIRD_PARTY.md
SDCARD ?= /media/mmcblk0
ASDPACK := $(BUILD)/asdpack
THEME_ARCHIVE_CHECK := $(BUILD)/theme_archive_check
THEME_VISUAL_CHECK := $(BUILD)/theme_visual_check
DTS ?= board/hc15xx/common/dts/sf2000_min.dts
DTS_NAME := $(basename $(notdir $(DTS)))
DTS_PRE := $(BUILD)/$(DTS_NAME).dtb.dts.tmp
DTB := $(BUILD)/$(DTS_NAME).dtb
DTB_ASM := $(BUILD)/$(DTS_NAME)_dtb.S
DTB_OBJ := $(BUILD)/$(DTS_NAME)_dtb.o
DTC ?= dtc
EMBED_DTB ?= 1
CCACHE ?= $(shell command -v ccache 2>/dev/null)
CCACHE_PREFIX := $(if $(CCACHE),$(CCACHE) )

# Cross tools for the device image, plus a host compiler for the ASD packer.
CC := $(CCACHE_PREFIX)$(CROSS_COMPILE)gcc
CXX := $(CCACHE_PREFIX)$(CROSS_COMPILE)g++
LD := $(CROSS_COMPILE)ld
AR := $(CROSS_COMPILE)ar
NM := $(CROSS_COMPILE)nm
READELF := $(CROSS_COMPILE)readelf
OBJCOPY := $(CROSS_COMPILE)objcopy
HOSTCC ?= $(or $(shell command -v tcc 2>/dev/null),gcc)
# Host checks are production-shaped builds too: keep them small and exercise
# the optimizer without making the default checks needlessly expensive.
HOSTCFLAGS ?= -Os -Wall -Wextra -D_DEFAULT_SOURCE
MIPS_ARCH ?= mips32
MIPS_TUNE ?= mips32
ARCH_CFLAGS ?= -march=$(MIPS_ARCH) -mtune=$(MIPS_TUNE)
OPT_SIZE ?= -Os
OPT_FAST ?= -O2
OPT_AUDIO ?= -Os
FOUNDATION_CFLAGS ?=
FRONTEND_CFLAGS ?=
LIBRETRO_CFLAGS ?=
MEDIA_CFLAGS ?=
DIAGNOSTICS_CFLAGS ?=

# Build-option validation. SD card experiments happen through runtime storage
# profiles now; the boot-time DTB profile is intentionally fixed and simple.
OPT_FLAGS := -O0 -O1 -O2 -O3 -Os -Og -Ofast
HCRTOS_MEDIA_MODES := native module firmware
MMC_HOST_IMPL_MODES := vendor source

ifneq ($(SD_MODE),wide25)
$(error SD_MODE is fixed to wide25; change storage profiles at runtime)
endif
ifneq ($(filter $(HCRTOS_MEDIA),$(HCRTOS_MEDIA_MODES)),$(HCRTOS_MEDIA))
$(error HCRTOS_MEDIA must be one of: $(HCRTOS_MEDIA_MODES))
endif
ifneq ($(filter $(MMC_HOST_IMPL),$(MMC_HOST_IMPL_MODES)),$(MMC_HOST_IMPL))
$(error MMC_HOST_IMPL must be one of: $(MMC_HOST_IMPL_MODES))
endif

SD_BUS_WIDTH := 4
SD_CLOCK_FREQUENCY := 25000000
SD_CAP_HIGHSPEED := 0
SD_CAP_UHS := 0
SD_UHS_SDR12 := 0
SD_UHS_SDR25 := 0
SD_UHS_SDR50 := 0
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0

GCC_LIBDIR ?= $(firstword $(wildcard $(TOOLCHAIN)/lib/gcc/mipsel-mti-elf/*))
SYS_LIBDIR := $(TOOLCHAIN)/mipsel-mti-elf/lib
Q := $(if $(V),,@)
LOG_ECHO := $(if $(filter 1,$(BUILD_PROGRESS)),@echo,@:)
UNIFROG_GIT_COMMIT := $(shell git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_BUILD_EPOCH := $(shell git show -s --format=%ct HEAD 2>/dev/null || echo 1704067200)
UNIFROG_GIT_TAG := $(shell git describe --tags --exact-match 2>/dev/null || true)
UNIFROG_BOOT_VERSION := $(shell tag=$$(git describe --tags --exact-match 2>/dev/null); if test -n "$$tag"; then printf '%s\n' "$$tag"; else commit=$$(git rev-parse --short=12 HEAD 2>/dev/null || echo unknown); printf 'revision %s\n' "$$commit"; fi)
UNIFROG_GIT_DIRTY := $(shell git diff-index --quiet --ignore-submodules=dirty HEAD -- 2>/dev/null && echo 0 || echo 1)
UNIFROG_SDK_GIT_COMMIT := $(shell git -C $(SDK) rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_CORES_GIT_COMMIT := $(shell git -C $(CORE_SOURCE_ROOT)/libretro-common rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
JS2300_REV := $(shell git -C $(JS2300) rev-parse --short=12 HEAD 2>/dev/null || echo missing)
UNIFROG_JS2300_GIT_COMMIT := $(JS2300_REV)
UNIFROG_FRONTEND_GIT_COMMIT := $(UNIFROG_GIT_COMMIT)
# Treat SDK headers as system headers so third-party/newlib warnings do not
# obscure warnings from the UniFrog source itself.
PROJECT_INCLUDES := -Iinclude -Ifoundation/src \
	-Iapps/fastboot/include \
	-Ijs2300/include \
	-I$(BUILD) -I$(CORE_SOURCE_ROOT)/libretro-common/include \
	-I$(JS2300)/include -I$(LVGL_DIR) -I$(LVGL_DIR)/src -I$(STB_DIR)
FFMPEG_INCLUDES := $(if $(HCRTOS_FFMPEG_INCLUDE),-isystem $(HCRTOS_FFMPEG_INCLUDE))
SDK_INCLUDES := \
	-isystem $(SDK)/include \
	-isystem $(SDK)/include/hcrtos \
	-isystem $(SDK)/include/newlib \
	-isystem $(SDK)/include/vendor

DTS_CPPFLAGS := \
	-Idts/include \
	-I$(SDK)/include/hcrtos \
	-DUNIFROG_SD_CLOCK_FREQUENCY=198000000 \
	-DUNIFROG_SD_MAX_FREQUENCY=$(SD_CLOCK_FREQUENCY) \
	-DUNIFROG_SD_BUS_WIDTH=$(SD_BUS_WIDTH) \
	-DUNIFROG_SD_CAP_HIGHSPEED=$(SD_CAP_HIGHSPEED) \
	-DUNIFROG_SD_CAP_UHS=$(SD_CAP_UHS) \
	-DUNIFROG_SD_UHS_SDR12=$(SD_UHS_SDR12) \
	-DUNIFROG_SD_UHS_SDR25=$(SD_UHS_SDR25) \
	-DUNIFROG_SD_UHS_SDR50=$(SD_UHS_SDR50) \
	-DUNIFROG_SD_NO_1V8=$(SD_NO_1V8) \
	-nostdinc \
	-undef \
	-D__ASSEMBLY__ \
	-D__DTS__ \
	-x assembler-with-cpp

DTCFLAGS := \
	-Wno-reg_format \
	-Wno-unit_address_vs_reg \
	-Wno-unit_address_format \
	-Wno-avoid_default_addr_size \
	-Wno-avoid_unnecessary_addr_size \
	-Wno-alias_paths \
	-Wno-graph_child_address \
	-Wno-graph_port \
	-Wno-gpios_property \
	-Wno-interrupts_property \
	-Wno-i2c_bus_reg \
	-Wno-spi_bus_reg \
	-Wno-unique_unit_address \
	-Wno-simple_bus_reg \
	-Wno-pci_device_reg \
	-Wno-pci_bridge \
	-Wno-pci_device_bus_num

# Defines mirror the original HCRTOS app build environment. Build identity is
# split out so only objects that print it rebuild when the git state changes.
CONFIG_DEFINES := \
	-D_FORTIFY_SOURCE=0 \
	-D__HCRTOS__ \
	-D__DEBUG__ \
	-DNOT_SUPPORT_4K \
	-DSOC_HC15XX \
	-DSF2000 \
	$(if $(filter firmware module,$(HCRTOS_MEDIA)),-DSUPPORT_FFPLAYER) \
	-DUNIFROG_HCRTOS_MEDIA=\"$(HCRTOS_MEDIA)\" \
	-DUNIFROG_HCRTOS_MEDIA_NATIVE=$(if $(filter native,$(HCRTOS_MEDIA)),1,0) \
	-DUNIFROG_HCRTOS_MEDIA_MODULE=$(if $(filter module,$(HCRTOS_MEDIA)),1,0) \
	-DUNIFROG_HCRTOS_MEDIA_FIRMWARE=$(if $(filter firmware,$(HCRTOS_MEDIA)),1,0) \
	-DUNIFROG_ENABLE_HCPLAYER=$(if $(filter firmware,$(HCRTOS_MEDIA)),1,0) \
	-DUNIFROG_SD_MODE=\"$(SD_MODE)\" \
	-DUNIFROG_SD_FORCE_PIO=$(SD_FORCE_PIO) \
	-DUNIFROG_SD_DMA_MODE=\"$(SD_DMA_MODE)\" \
	-DUNIFROG_SD_DMA_MODE_QUIRKS=$(if $(filter quirks,$(SD_DMA_MODE)),1,0) \
	-DUNIFROG_SD_DMA_MODE_WRAP=$(if $(filter wrap,$(SD_DMA_MODE)),1,0) \
	-DUNIFROG_MMC_HOST_IMPL=\"$(MMC_HOST_IMPL)\" \
	-DUNIFROG_MMC_HOST_SOURCE_DEFAULT=$(if $(filter source,$(MMC_HOST_IMPL)),1,0) \
	-DUNIFROG_STORAGE_BOOT_MOUNT=$(STORAGE_BOOT_MOUNT) \
	-DUNIFROG_LOG_AUTO_FLUSH_BYTES=$(LOG_AUTO_FLUSH_BYTES) \
	-DUNIFROG_LOG_DISK_WRITES=$(LOG_DISK_WRITES) \
	-DUNIFROG_SD_EXPERIMENTAL=$(SD_EXPERIMENTAL)

IDENTITY_DEFINES := \
	-DUNIFROG_GIT_COMMIT=\"$(UNIFROG_GIT_COMMIT)\" \
	-DUNIFROG_GIT_TAG=\"$(UNIFROG_GIT_TAG)\" \
	-DUNIFROG_GIT_DIRTY=$(UNIFROG_GIT_DIRTY) \
	-DUNIFROG_SDK_GIT_COMMIT=\"$(UNIFROG_SDK_GIT_COMMIT)\" \
	-DUNIFROG_CORES_GIT_COMMIT=\"$(UNIFROG_CORES_GIT_COMMIT)\" \
	-DUNIFROG_JS2300_GIT_COMMIT=\"$(UNIFROG_JS2300_GIT_COMMIT)\" \
	-DUNIFROG_FRONTEND_GIT_COMMIT=\"$(UNIFROG_FRONTEND_GIT_COMMIT)\" \
	-DUNIFROG_BUILD_EPOCH=$(UNIFROG_BUILD_EPOCH)

DEFINES := $(CONFIG_DEFINES) $(IDENTITY_DEFINES)

CFLAGS := -EL $(ARCH_CFLAGS) $(OPT_SIZE) -pipe -msoft-float -fsigned-char -W \
	-ffunction-sections -fdata-sections -G0 \
	-Wformat=2 \
	-Wno-error=cast-function-type \
	-Wno-error=builtin-declaration-mismatch \
	-Wno-error=format-truncation= \
	-Wno-error=int-conversion \
	$(DEFINES) \
	$(PROJECT_INCLUDES) \
	$(FFMPEG_INCLUDES) \
	$(SDK_INCLUDES)
CFLAGS_NOOPT = $(filter-out $(OPT_FLAGS),$(CFLAGS))
CFLAGS_FAST = $(CFLAGS_NOOPT) $(OPT_FAST)
CFLAGS_AUDIO = $(CFLAGS_NOOPT) $(OPT_AUDIO)
CFLAGS_VIDEO = $(CFLAGS_FAST)

ifeq ($(MIPS_ARCH),mips32)
CFLAGS += -DSF2000_HAVE_MIPS_WAIT=1
endif

CFLAGS_NO_IDENTITY = $(filter-out $(IDENTITY_DEFINES),$(CFLAGS))
CFLAGS_NOOPT_NO_IDENTITY = $(filter-out $(OPT_FLAGS),$(CFLAGS_NO_IDENTITY))
CFLAGS_FAST_NO_IDENTITY = $(CFLAGS_NOOPT_NO_IDENTITY) $(OPT_FAST)
CFLAGS_AUDIO_NO_IDENTITY = $(CFLAGS_NOOPT_NO_IDENTITY) $(OPT_AUDIO)
CFLAGS_VIDEO_NO_IDENTITY = $(CFLAGS_FAST_NO_IDENTITY)

LIBDIRS := \
	-L$(HCRTOS_FFMPEG_INSTALL)/lib \
	-L$(SDK)/lib/core \
	-L$(SDK)/lib/vendor \
	-L$(SDK)/lib/plugins/audio \
	-L$(GCC_LIBDIR) \
	-L$(SYS_LIBDIR)

# HCRTOS requires the app entry script, SoC peripheral map, and global memory
# layout script to be passed together.
LDSCRIPTS := \
	-T linker/entry.ld \
	-T linker/hc15xx/peripherals.ld \
	-T linker/ldscript.ld

WRAPS := \
	--wrap memset \
	--wrap memcpy \
	--wrap memmove \
	--wrap strcpy \
	--wrap strcat \
	--wrap strncat \
	--wrap strncpy \
	--wrap __errno \
	--wrap rename \
	--wrap dma_map_sg \
	--wrap dma_unmap_sg \
	--wrap mmc_request_done

# `-Os` is a compiler option; GNU ld rejects it. Section garbage collection
# is the linker-side size optimization for this image.
LDFLAGS := -EL --static $(LIBDIRS) $(LDSCRIPTS) --gc-sections -n $(WRAPS) --allow-multiple-definition

HCRTOS_DISPLAY_LDLIBS := \
	-lviddrv

HCRTOS_MEDIA_LDLIBS := \
	$(if $(filter firmware module,$(HCRTOS_MEDIA)),-lffplayer) \
	-lavformat \
	-lavcodec \
	-lavutil \
	-lswresample \
	-lswscale \
	-lntfs

# Normal archives are pulled as needed by the linker.
LDLIBS := \
	$(if $(filter native firmware,$(HCRTOS_MEDIA)),$(HCRTOS_MEDIA_LDLIBS)) \
	$(HCRTOS_DISPLAY_LDLIBS) \
	-lge \
	-lz \
	-lkernel \
	-llnx \
	-lpthread \
	-lm \
	-lstdc++ \
	-lsupc++ \
	-lc \
	-lgcc

CORE_WHOLE_LIBS := \
	-lauddrv \
	-lauddsp \
	-lmmc \
	-lefuse

ifneq ($(MMC_HOST_IMPL),source)
CORE_WHOLE_LIBS += -lmmchosthc15
endif

# HCRTOS media codec plugins are used by the native decoder path.  UniFrog owns
# the playback orchestration and links only codec/driver support libraries.
HCRTOS_MEDIA_WHOLE_LIBS := \
	-ldsc \
	-lmp3nddec \
	-lmp3 \
	-laacdec \
	-lpcmdec \
	-lflacdec \
	-lflac \
	-lvorbisdec \
	-ltremor \
	-lwmadec \
	-lwmaprodec \
	-lwmapro \
	-lopusdec \
	-lopus \
	-lradec \
	-lra \
	-lviddrv_h264dec \
	-lviddrv_mpeg2dec \
	-lviddrv_vc1dec \
	-lviddrv_vp8dec \
	-lviddrv_mpeg4dec \
	-lviddrv_imagedec

WHOLE_LIBS := $(if $(filter native firmware,$(HCRTOS_MEDIA)),$(HCRTOS_MEDIA_WHOLE_LIBS)) $(CORE_WHOLE_LIBS)

include foundation/sources.mk
include components/frontend/sources.mk
include components/libretro/sources.mk
include components/media/sources.mk
include components/diagnostics/sources.mk
include js2300/sources.mk

FRONTEND_RUNTIME_OBJECTS := $(FRONTEND_APP_OBJECTS) $(JS2300_HOST_OBJECTS)

APP_OBJECTS := \
	$(BUILD)/runtime/main.o

BUILD_IDENTITY_OBJECTS := \
	$(BUILD)/runtime/main.o \
	$(FRONTEND_RUNTIME_OBJECTS) \
	$(BUILD)/libretro/unifrog_libretro_host.o \
	$(BUILD)/libretro/unifrog_libretro_runtime.o \
	$(FRONTEND_QUICK_MENU_OBJECT) \
	$(BUILD)/libretro/unifrog_libretro_content.o \
	$(BUILD)/platform/sf2000/unifrog_platform.o

UNIFROG_OBJECTS := \
	$(FOUNDATION_RUNTIME_OBJECTS) \
	$(DIAGNOSTICS_COMPONENT_OBJECTS) \
	$(LIBRETRO_UNIFROG_OBJECTS) \
	$(MEDIA_UNIFROG_OBJECTS) \
	$(FRONTEND_READER_OBJECT)

LZ4_SRCS := lz4.c lz4frame.c lz4hc.c xxhash.c
LZ4_OBJS := $(addprefix $(BUILD)/support/lz4/,$(LZ4_SRCS:.c=.o))
LZ4_CFLAGS := $(CFLAGS_FAST) -w -I$(LZ4_DIR)/lib
ZSTD_DIR := $(CORE_SUPPORT_ROOT)/zstd/lib
ZSTD_DECODER_SRC := $(CORE_SUPPORT_ROOT)/zstd/build/single_file_libs/zstddeclib-in.c
ZSTD_DECODER_OBJ := $(BUILD)/third_party/zstddeclib.o
ZSTD_CFLAGS := $(CFLAGS_FAST) -w \
	-DZSTD_DISABLE_ASM=1 \
	-I$(ZSTD_DIR) \
	-I$(ZSTD_DIR)/common \
	-I$(ZSTD_DIR)/decompress

UNIFROG_OBJECTS += $(LZ4_OBJS) $(ZSTD_DECODER_OBJ)

LVGL_FONT_OBJECTS := \
	$(BUILD)/lvgl/font/lv_font_loader.o \
	$(BUILD)/lvgl/font/lv_font_fmt_txt.o \
	$(BUILD)/lvgl/misc/lv_fs.o \
	$(BUILD)/lvgl/misc/lv_gc.o \
	$(BUILD)/lvgl/misc/lv_ll.o \
	$(BUILD)/lvgl/misc/lv_mem.o \
	$(BUILD)/lvgl/misc/lv_txt.o \
	$(BUILD)/lvgl/misc/lv_txt_ap.o \
	$(BUILD)/lvgl/misc/lv_utils.o \
	$(BUILD)/lvgl/extra/libs/fsdrv/lv_fs_stdio.o
UNIFROG_OBJECTS += $(LVGL_FONT_OBJECTS)

LIBUNIFROG := $(OUT)/libunifrog.a
FRONTEND_COMPONENT_OBJECTS := \
	$(FRONTEND_RUNTIME_OBJECTS) \
	$(FRONTEND_QUICK_MENU_OBJECT) \
	$(FRONTEND_READER_OBJECT)
FOUNDATION_COMPONENT_OBJECTS := $(filter-out \
	$(LIBRETRO_COMPONENT_OBJECTS) \
	$(MEDIA_COMPONENT_OBJECTS) \
	$(DIAGNOSTICS_COMPONENT_OBJECTS) \
	$(FRONTEND_READER_OBJECT),$(UNIFROG_OBJECTS))
COMPONENT_ARCHIVE_DIR := $(BUILD)/components
LIBFOUNDATION_COMPONENT := $(COMPONENT_ARCHIVE_DIR)/libfoundation.a
LIBFRONTEND_COMPONENT := $(COMPONENT_ARCHIVE_DIR)/libfrontend.a
LIBLIBRETRO_COMPONENT := $(COMPONENT_ARCHIVE_DIR)/liblibretro.a
LIBMEDIA_COMPONENT := $(COMPONENT_ARCHIVE_DIR)/libmedia.a
LIBDIAGNOSTICS_COMPONENT := $(COMPONENT_ARCHIVE_DIR)/libdiagnostics.a
FIRMWARE_COMPONENT_LIBS := \
	$(LIBFRONTEND_COMPONENT) \
	$(LIBLIBRETRO_COMPONENT) \
	$(LIBMEDIA_COMPONENT) \
	$(LIBDIAGNOSTICS_COMPONENT) \
	$(LIBFOUNDATION_COMPONENT)
LIBJS2300 := $(JS2300)/output/libjs2300.a
LIBRETRO_COMMON_LIB := $(CORES)/output/libretro-common-sf2000.a
FASTBOOT_STAGE_OUT := $(OUT)/fastboot-stage1.out
FASTBOOT_STAGE_BIN := $(BUILD)/fastboot/stage1.bin
FASTBOOT_STUB_OUT := $(OUT)/fastboot-stub.out
FASTBOOT_STUB_BIN := $(BUILD)/fastboot/stub.bin
FASTBOOT_STAGE_OBJ := $(BUILD)/fastboot/stage1.o
FASTBOOT_STAGE_ENTRY_OBJ := $(BUILD)/fastboot/stage_entry.o
FASTBOOT_STUB_OBJ := $(BUILD)/fastboot/stub.o
BOOT_LOGO_TOOL := $(BUILD)/bootlogo-tool
BOOT_LOGO_SRC := assets/boot/unifrog-logo.png
BOOT_LOGO_STAMPED_PPM := $(BUILD)/boot/unifrog-logo-stamped.ppm
BOOT_LOGO_RGB565_INC := $(BUILD)/boot/unifrog-logo-rgb565.inc
BOOT_LOGO_STAMP := $(BUILD)/boot/unifrog-logo.stamp
CHD_SUPPORT_CORE_LIB := $(CORES)/output/libchdr-support-sf2000.a
FIRMWARE_LIBRETRO_CORE_LIBS ?=
include $(CORES)/manifest.mk
FROGUI_SOURCE_DIR := $(CORE_SOURCE_ROOT)/FrogUI
FROGUI_FONT_FILES = $(if $(filter frogui,$(EFFECTIVE_CORE_IDS)),\
	$(FROGUI_SOURCE_DIR)/fonts/GamePocket-Regular-ZeroKern.ttf \
	$(FROGUI_SOURCE_DIR)/fonts/monogram.ttf \
	$(FROGUI_SOURCE_DIR)/fonts/LICENSE.md)
INTERNAL_CORE_PACKAGE_SPECS := \
	js2300:JS2300:js2300:js2300:js2300:js\|mjs\|ch8\|chip8:-:-
ALL_CORE_PACKAGE_SPECS := $(CORE_PACKAGE_SPECS) $(INTERNAL_CORE_PACKAGE_SPECS)
CORE_BUILD_DEPS = $(CORES)/Makefile $(CORES)/manifest.mk $(CORE_REV_STAMP)
CORE_MAKE_ARGS := \
	TOOLCHAIN=$(TOOLCHAIN) \
	CROSS_COMPILE=$(CROSS_COMPILE) \
	SDK=$(abspath $(SDK)) \
	DEP_CHECKOUT=$(DEP_CHECKOUT) \
	DEP_DEPTH=$(DEP_DEPTH) \
	DEP_GIT_ENV="$(DEP_GIT_ENV)" \
	CORE_IDS="$(strip $(if $(CORE_IDS),$(CORE_IDS),$(CORE)))" \
	CORE_SOURCE_ROOT=$(abspath $(CORE_SOURCE_ROOT)) \
	CORE_SUPPORT_ROOT=$(abspath $(CORE_SUPPORT_ROOT)) \
	CCACHE=$(CCACHE) \
	JOBS=$(JOBS)
CORE_BATCH_GOALS := all check verify sdcard-package sd-zip install refresh-sd refresh-sd-clean
CORE_BATCH_BUILD := $(if $(strip $(CORE_IDS)),0,$(if $(filter $(CORE_BATCH_GOALS),$(MAKECMDGOALS)),1,0))
CORE_SMOKE_MAKE_FLAGS := --no-print-directory
ifeq ($(V),)
CORE_SMOKE_MAKE_FLAGS += --silent
endif
define LIBRETRO_MODULE_REGISTER
$(2)_CORE_ID := $(1)
$(2)_CORE_LIB := $(CORES)/output/$(5)_libretro_sf2000.a
$(2)_CORE_BIN := $(CORE_PACKAGE)/$(3).bin
$(2)_CORE_OUT := $(BUILD)/core_modules/$(3).out
$(2)_CORE_ENTRY := $(BUILD)/core_modules/$(3)_entry.o
$(2)_CORE_MODULE := $(3)
$(2)_CORE_TARGET := $(4)
$(2)_CORE_ARCHIVE_STEM := $(5)
$(2)_CORE_EXTENSIONS := $(6)
$(2)_CORE_SYMBOL_PREFIX := $(if $(filter -,$(7)),,$(7))
$(2)_CORE_SUPPORT_LIBS := $(if $(filter -,$(8)),,$(8))
CORE_VAR_$(1) := $(2)
endef

$(foreach module,$(ALL_CORE_PACKAGE_SPECS),$(eval $(call LIBRETRO_MODULE_REGISTER,$(word 1,$(subst :, ,$(module))),$(word 2,$(subst :, ,$(module))),$(word 3,$(subst :, ,$(module))),$(word 4,$(subst :, ,$(module))),$(word 5,$(subst :, ,$(module))),$(word 6,$(subst :, ,$(module))),$(word 7,$(subst :, ,$(module))),$(word 8,$(subst :, ,$(module))))))

EXTERNAL_LIBRETRO_CORE_VARS := $(foreach module,$(CORE_PACKAGE_SPECS),$(word 2,$(subst :, ,$(module))))
INTERNAL_LIBRETRO_CORE_VARS := $(foreach module,$(INTERNAL_CORE_PACKAGE_SPECS),$(word 2,$(subst :, ,$(module))))
LIBRETRO_CORE_VARS := $(EXTERNAL_LIBRETRO_CORE_VARS) $(INTERNAL_LIBRETRO_CORE_VARS)
LIBRETRO_CORE_IDS := $(foreach var,$(LIBRETRO_CORE_VARS),$($(var)_CORE_ID))
JS2300_CORE_LIB := $(BUILD)/core_modules/js2300_libretro_core.a
JS2300_CORE_SUPPORT_LIBS := $(LIBJS2300)
JS2300_CORE_OBJECTS := $(BUILD)/core_modules/js2300_libretro_core.o
EFFECTIVE_CORE_IDS := $(strip $(if $(CORE_IDS),$(CORE_IDS),$(LIBRETRO_CORE_IDS)))
SELECTED_CORE_LICENSE_SPECS := $(foreach spec,$(CORE_LICENSE_SPECS),$(if $(filter $(word 1,$(subst |, ,$(spec))),$(EFFECTIVE_CORE_IDS)),$(spec)))
SELECTED_CORE_LICENSE_FILES := $(foreach spec,$(SELECTED_CORE_LICENSE_SPECS),$(word 2,$(subst |, ,$(spec))))
UNKNOWN_CORE_IDS := $(strip $(foreach id,$(EFFECTIVE_CORE_IDS),$(if $(CORE_VAR_$(id)),,$(id))))
ifneq ($(UNKNOWN_CORE_IDS),)
$(error unknown CORE_IDS='$(UNKNOWN_CORE_IDS)'; supported cores: $(LIBRETRO_CORE_IDS))
endif
PACKAGE_LIBRETRO_CORE_VARS := $(foreach id,$(EFFECTIVE_CORE_IDS),$(CORE_VAR_$(id)))
PACKAGE_LIBRETRO_SUPPORT_LIBS := $(foreach var,$(PACKAGE_LIBRETRO_CORE_VARS),$($(var)_CORE_SUPPORT_LIBS))
PACKAGE_NEEDS_CHD := $(if $(filter $(CHD_SUPPORT_CORE_LIB),$(PACKAGE_LIBRETRO_SUPPORT_LIBS)),1,0)
LIBRETRO_CORE_BINS := $(foreach var,$(PACKAGE_LIBRETRO_CORE_VARS),$($(var)_CORE_BIN))
LIBRETRO_CORE_MODULE_OUTS := $(foreach var,$(PACKAGE_LIBRETRO_CORE_VARS),$($(var)_CORE_OUT))
LIBRETRO_CORE_ENTRY_OBJECTS := $(foreach var,$(PACKAGE_LIBRETRO_CORE_VARS),$($(var)_CORE_ENTRY))
ifneq ($(CORE),)
SELECTED_CORE_VAR := $(CORE_VAR_$(CORE))
ifeq ($(SELECTED_CORE_VAR),)
$(error unknown CORE='$(CORE)'; supported cores: $(LIBRETRO_CORE_IDS))
endif
SELECTED_CORE_BIN := $($(SELECTED_CORE_VAR)_CORE_BIN)
else
SELECTED_CORE_BIN :=
endif
HCRTOS_MEDIA_MODULE_BIN := $(MODULE_PACKAGE)/hcrtos-media.bin
HCRTOS_MEDIA_MODULE_OUT := $(BUILD)/runtime_modules/hcrtos_media.out
HCRTOS_MEDIA_MODULE_ARCHIVE := $(BUILD)/runtime_modules/hcrtos_media.a
HCRTOS_MEDIA_MODULE_BINS := $(if $(filter module,$(HCRTOS_MEDIA)),$(HCRTOS_MEDIA_MODULE_BIN))
HCRTOS_MEDIA_MODULE_LDLIBS = \
	$(HCRTOS_MEDIA_LDLIBS) \
	$(HCRTOS_DISPLAY_LDLIBS) \
	-lauddrv \
	-lauddsp \
	-lge \
	-lz \
	-lkernel \
	-llnx \
	-lpthread \
	$(CORE_MODULE_LDLIBS)
RUNTIME_MODULE_OUTS := $(if $(filter module,$(HCRTOS_MEDIA)),$(HCRTOS_MEDIA_MODULE_OUT))
UNIFROG_CORE_MODULE_BASE ?= 0x82000000
CORE_MODULE_CFLAGS := $(CFLAGS) -mno-abicalls -fno-pic
CORE_MODULE_CFLAGS_NO_IDENTITY := $(filter-out $(IDENTITY_DEFINES),$(CORE_MODULE_CFLAGS))
CORE_MODULE_LDFLAGS := -EL --static $(LIBDIRS) \
	-T linker/hc15xx/peripherals.ld \
	-T linker/core-module.ld \
	--defsym __UNIFROG_MODULE_BASE=$(UNIFROG_CORE_MODULE_BASE) \
	--gc-sections -n --allow-multiple-definition
CORE_MODULE_LDLIBS := \
	-lm \
	-lstdc++ \
	-lsupc++ \
	-lc \
	-lgcc
FASTBOOT_CFLAGS := -EL $(ARCH_CFLAGS) -Os -pipe -msoft-float -fsigned-char -W \
	-ffunction-sections -fdata-sections -G0 \
	-ffreestanding -fno-builtin -fno-pic -mno-abicalls \
	-nostdinc -I$(GCC_LIBDIR)/include -Iapps/fastboot/include

DTS_INPUTS := $(DTS) $(shell test ! -d dts/include || find dts/include -type f | sort)
JS2300_INPUTS := $(shell find js2300/src js2300/include js2300/scripts -type f 2>/dev/null | sort) \
	$(shell find $(JS2300)/src $(JS2300)/include -type f 2>/dev/null | sort) \
	$(JS2300)/Makefile

BUILD_CONFIG_TOKEN := $(shell printf '%s\n' \
	'CC=$(CC)' 'CXX=$(CXX)' 'LD=$(LD)' 'AR=$(AR)' 'OBJCOPY=$(OBJCOPY)' \
	'HOSTCC=$(HOSTCC)' 'HOSTCFLAGS=$(HOSTCFLAGS)' \
	'ARCH_CFLAGS=$(ARCH_CFLAGS)' 'OPT_SIZE=$(OPT_SIZE)' \
	'OPT_FAST=$(OPT_FAST)' 'OPT_AUDIO=$(OPT_AUDIO)' \
	'FOUNDATION_CFLAGS=$(FOUNDATION_CFLAGS)' \
	'FRONTEND_CFLAGS=$(FRONTEND_CFLAGS)' \
	'LIBRETRO_CFLAGS=$(LIBRETRO_CFLAGS)' \
	'MEDIA_CFLAGS=$(MEDIA_CFLAGS)' \
	'DIAGNOSTICS_CFLAGS=$(DIAGNOSTICS_CFLAGS)' \
	'CONFIG_DEFINES=$(CONFIG_DEFINES)' 'PROJECT_INCLUDES=$(PROJECT_INCLUDES)' \
	'SDK_INCLUDES=$(SDK_INCLUDES)' 'CFLAGS=$(CFLAGS_NO_IDENTITY)' \
	'CFLAGS_FAST=$(CFLAGS_FAST_NO_IDENTITY)' \
	'CFLAGS_AUDIO=$(CFLAGS_AUDIO_NO_IDENTITY)' \
	'CFLAGS_VIDEO=$(CFLAGS_VIDEO_NO_IDENTITY)' \
	'LDFLAGS=$(LDFLAGS)' 'LDLIBS=$(LDLIBS)' 'WHOLE_LIBS=$(WHOLE_LIBS)' \
	'UNIFROG_OBJECTS=$(UNIFROG_OBJECTS)' \
	'FIRMWARE_COMPONENT_LIBS=$(FIRMWARE_COMPONENT_LIBS)' \
	'CORE_MODULE_CFLAGS=$(CORE_MODULE_CFLAGS_NO_IDENTITY)' \
	'CORE_MODULE_LDFLAGS=$(CORE_MODULE_LDFLAGS)' \
	'CORE_MODULE_LDLIBS=$(CORE_MODULE_LDLIBS)' \
	'DTS_CPPFLAGS=$(DTS_CPPFLAGS)' 'DTCFLAGS=$(DTCFLAGS)' \
	'EMBED_DTB=$(EMBED_DTB)' | cksum | awk '{print $$1}')
BUILD_IDENTITY_TOKEN := $(shell printf '%s\n' \
	'UNIFROG_GIT_COMMIT=$(UNIFROG_GIT_COMMIT)' \
	'UNIFROG_BUILD_EPOCH=$(UNIFROG_BUILD_EPOCH)' \
	'UNIFROG_BOOT_VERSION=$(UNIFROG_BOOT_VERSION)' \
	'UNIFROG_GIT_DIRTY=$(UNIFROG_GIT_DIRTY)' \
	'UNIFROG_SDK_GIT_COMMIT=$(UNIFROG_SDK_GIT_COMMIT)' \
	'UNIFROG_CORES_GIT_COMMIT=$(UNIFROG_CORES_GIT_COMMIT)' \
	'UNIFROG_JS2300_GIT_COMMIT=$(UNIFROG_JS2300_GIT_COMMIT)' \
	'UNIFROG_FRONTEND_GIT_COMMIT=$(UNIFROG_FRONTEND_GIT_COMMIT)' \
	'HCRTOS_MEDIA=$(HCRTOS_MEDIA)' | cksum | awk '{print $$1}')
FASTBOOT_CONFIG_TOKEN := $(shell printf '%s\n' \
	'CC=$(CC)' 'LD=$(LD)' 'OBJCOPY=$(OBJCOPY)' \
	'FASTBOOT_CFLAGS=$(FASTBOOT_CFLAGS)' \
	'FASTBOOT_STAGE_BIN=$(FASTBOOT_STAGE_BIN)' | cksum | awk '{print $$1}')
DTS_MODE_TOKEN := $(shell printf '%s\n' \
	'SD_MODE=$(SD_MODE)' 'SD_CLOCK_FREQUENCY=$(SD_CLOCK_FREQUENCY)' \
	'SD_BUS_WIDTH=$(SD_BUS_WIDTH)' 'SD_CAP_HIGHSPEED=$(SD_CAP_HIGHSPEED)' \
	'SD_CAP_UHS=$(SD_CAP_UHS)' 'SD_UHS_SDR12=$(SD_UHS_SDR12)' \
	'SD_UHS_SDR25=$(SD_UHS_SDR25)' 'SD_UHS_SDR50=$(SD_UHS_SDR50)' \
	'SD_NO_1V8=$(SD_NO_1V8)' 'SD_EXPERIMENTAL=$(SD_EXPERIMENTAL)' | cksum | awk '{print $$1}')
CORE_CONFIG_TOKEN := $(shell { \
	printf '%s\n' 'TOOLCHAIN=$(TOOLCHAIN)' 'CROSS_COMPILE=$(CROSS_COMPILE)' \
		'SDK=$(abspath $(SDK))' 'CORE_SOURCE_ROOT=$(abspath $(CORE_SOURCE_ROOT))' \
		'CORE_SUPPORT_ROOT=$(abspath $(CORE_SUPPORT_ROOT))' 'CCACHE=$(CCACHE)' \
		'JOBS=$(JOBS)'; \
	for f in $(CORES)/manifest.mk $(DTS_INPUTS); do test -f "$$f" && cksum "$$f"; done; \
} | cksum | awk '{print $$1}')
CORE_ARCHIVE_STAMP := $(BUILD)/core-archives.$(CORE_CONFIG_TOKEN).stamp
# `cores all` also builds CHD support when the default core set contains
# picodrive or PCE Fast. Make that shared archive an explicit prerequisite so
# the root batch build cannot launch a second sub-make for it concurrently.
CORE_BATCH_SUPPORT_DEPS := $(if $(filter 1,$(CORE_BATCH_BUILD)),$(if $(filter $(CHD_SUPPORT_CORE_LIB),$(PACKAGE_LIBRETRO_SUPPORT_LIBS)),$(CHD_SUPPORT_CORE_LIB),),)
JS2300_CONFIG_TOKEN := $(shell { \
	printf '%s\n' 'TOOLCHAIN=$(TOOLCHAIN)' 'CROSS_COMPILE=$(CROSS_COMPILE)' \
		'JS2300=$(abspath $(JS2300))' 'JS2300_REF=$(JS2300_REF)' \
		'JS2300_REV=$(JS2300_REV)' 'HOSTCC=$(HOSTCC)'; \
	for f in $(JS2300_INPUTS); do test -f "$$f" && cksum "$$f"; done; \
} | cksum | awk '{print $$1}')
FRONTEND_CONFIG_TOKEN := $(shell { \
	printf '%s\n' 'OUT=$(abspath $(OUT))/frontend' \
		'identity=$(BUILD_IDENTITY_TOKEN)'; \
} | cksum | awk '{print $$1}')

# Stable stamp paths with token contents prevent stale per-token stamps from
# making an older SD/DTS configuration look up to date after mode switches.
BUILD_CONFIG_STAMP := $(BUILD)/build-config.stamp
BUILD_IDENTITY_STAMP := $(BUILD)/build-identity.stamp
FASTBOOT_CONFIG_STAMP := $(BUILD)/fastboot-config.stamp
DTS_MODE_STAMP := $(BUILD)/sd-mode.stamp
CORE_REV_STAMP := $(BUILD)/core-config.stamp
SDK_BUILD_STAMP := $(BUILD)/sdk-build.stamp
SDK_KERNEL_LIB := $(SDK)/lib/core/libkernel.a
HCRTOS_FFMPEG_STAMP := $(HCRTOS_FFMPEG_INSTALL)/.unifrog-ffmpeg.stamp
HCRTOS_FFMPEG_SOURCE_CONFIG := $(BUILD)/hcrtos-ffmpeg-source.stamp
HCRTOS_FFMPEG_SOURCE_STAMP := $(HCRTOS_FFMPEG_SOURCE)/.git/unifrog-source.stamp
HCRTOS_FFMPEG_CONFIGURE_LOG := $(BUILD)/logs/hcrtos-ffmpeg-configure.log
HCRTOS_FFMPEG_BUILD_LOG := $(BUILD)/logs/hcrtos-ffmpeg-build.log
HCRTOS_FFMPEG_INSTALL_LOG := $(BUILD)/logs/hcrtos-ffmpeg-install.log
JS2300_FETCH_STAMP := $(JS2300)/.unifrog-js2300-$(JS2300_REF)
HCRTOS_FFMPEG_SOURCE_TOKEN := $(shell { \
	printf '%s\n' 'DEP_DEPTH=$(DEP_DEPTH)' \
		'HCRTOS_FFMPEG_URL=$(HCRTOS_FFMPEG_URL)' \
		'HCRTOS_FFMPEG_REF=$(HCRTOS_FFMPEG_REF)' \
		'HCRTOS_FFMPEG_COMMIT=$(HCRTOS_FFMPEG_COMMIT)' \
		'HCRTOS_FFMPEG_PATCH=$(HCRTOS_FFMPEG_PATCH)'; \
} | cksum | awk '{print $$1}')
ROOT_DEPS_ENV := \
	BUILD_PROGRESS="$(BUILD_PROGRESS)" \
	DEP_DEPTH="$(DEP_DEPTH)" \
	LVGL_DIR="$(LVGL_DIR)" \
	LVGL_URL="$(LVGL_URL)" \
	LVGL_REF="$(LVGL_REF)" \
	HCRTOS_FFMPEG_SOURCE="$(HCRTOS_FFMPEG_SOURCE)" \
	HCRTOS_FFMPEG_URL="$(HCRTOS_FFMPEG_URL)" \
	HCRTOS_FFMPEG_REF="$(HCRTOS_FFMPEG_REF)" \
	HCRTOS_FFMPEG_COMMIT="$(HCRTOS_FFMPEG_COMMIT)" \
	HCRTOS_FFMPEG_PATCH="$(HCRTOS_FFMPEG_PATCH)" \
	HCRTOS_FFMPEG_SOURCE_STAMP="$(HCRTOS_FFMPEG_SOURCE_STAMP)"
define root-deps-cmd
$(DEP_GIT_ENV) $(ROOT_DEPS_ENV) DEP="$(1)" REF="$(2)" sh tools/root-deps.sh $(3)
endef
HCRTOS_FFMPEG_BUILD_ENV := \
	BUILD_PROGRESS="$(BUILD_PROGRESS)" \
	V="$(V)" \
	MAKE="$(MAKE)" \
	HCRTOS_FFMPEG_SOURCE="$(HCRTOS_FFMPEG_SOURCE)" \
	HCRTOS_FFMPEG_SOURCE_ABS="$(abspath $(HCRTOS_FFMPEG_SOURCE))" \
	HCRTOS_FFMPEG_BUILD="$(BUILD)/hcrtos-ffmpeg" \
	HCRTOS_FFMPEG_INSTALL="$(HCRTOS_FFMPEG_INSTALL)" \
	HCRTOS_FFMPEG_INSTALL_ABS="$(abspath $(HCRTOS_FFMPEG_INSTALL))" \
	HCRTOS_FFMPEG_STAMP="$(HCRTOS_FFMPEG_STAMP)" \
	HCRTOS_FFMPEG_CONFIGURE_LOG="$(HCRTOS_FFMPEG_CONFIGURE_LOG)" \
	HCRTOS_FFMPEG_BUILD_LOG="$(HCRTOS_FFMPEG_BUILD_LOG)" \
	HCRTOS_FFMPEG_INSTALL_LOG="$(HCRTOS_FFMPEG_INSTALL_LOG)" \
	HCRTOS_FFMPEG_DEMUXERS="$(HCRTOS_FFMPEG_DEMUXERS)" \
	HCRTOS_FFMPEG_PARSERS="$(HCRTOS_FFMPEG_PARSERS)" \
	HCRTOS_FFMPEG_DECODERS="$(HCRTOS_FFMPEG_DECODERS)" \
	HCRTOS_FFMPEG_INSTALL_TARGETS="$(HCRTOS_FFMPEG_INSTALL_TARGETS)" \
	HCRTOS_FFMPEG_WARN_CFLAGS="$(HCRTOS_FFMPEG_WARN_CFLAGS)" \
	HCRTOS_FFMPEG_ABI_CFLAGS="$(HCRTOS_FFMPEG_ABI_CFLAGS)" \
	CROSS_COMPILE="$(CROSS_COMPILE)" \
	AR="$(AR)" \
	NM="$(NM)" \
	ARCH_CFLAGS="$(ARCH_CFLAGS)" \
	OPT_SIZE="$(OPT_SIZE)" \
	SDK_ABS="$(abspath $(SDK))"
JS2300_CONFIG_STAMP := $(BUILD)/js2300-config.stamp
FRONTEND_CONFIG_STAMP := $(BUILD)/frontend-config.stamp
FRONTEND_PACKAGE_STAMP := $(FRONTEND_PACKAGE)/.package.$(FRONTEND_CONFIG_TOKEN).stamp
define update-token-stamp
tmp="$@.tmp"; \
printf '%s\n' '$(1)' > "$$tmp"; \
if cmp -s "$$tmp" "$@"; then rm -f "$$tmp"; else mv "$$tmp" "$@"; fi
endef

ifeq ($(EMBED_DTB),1)
APP_OBJECTS += $(DTB_OBJ)
endif

DEVICE_OBJECTS := $(APP_OBJECTS) $(UNIFROG_OBJECTS) \
	$(LIBRETRO_CORE_ENTRY_OBJECTS) $(BUILD)/core_modules/support.o \
	$(HCRTOS_MEDIA_MODULE_OBJECTS)
FASTBOOT_OBJECTS := $(FASTBOOT_STAGE_OBJ) $(FASTBOOT_STAGE_ENTRY_OBJ) \
	$(FASTBOOT_STUB_OBJ)

.PHONY: FORCE
FORCE:

$(BUILD_CONFIG_STAMP): FORCE | $(BUILD)
	$(Q)$(call update-token-stamp,BUILD_CONFIG_TOKEN=$(BUILD_CONFIG_TOKEN))

$(BUILD_IDENTITY_STAMP): FORCE | $(BUILD)
	$(Q)$(call update-token-stamp,BUILD_IDENTITY_TOKEN=$(BUILD_IDENTITY_TOKEN))

$(FASTBOOT_CONFIG_STAMP): FORCE | $(BUILD)
	$(Q)$(call update-token-stamp,FASTBOOT_CONFIG_TOKEN=$(FASTBOOT_CONFIG_TOKEN))

$(DTS_MODE_STAMP): FORCE | $(BUILD)
	$(Q)$(call update-token-stamp,DTS_MODE_TOKEN=$(DTS_MODE_TOKEN))

$(CORE_REV_STAMP): FORCE | $(BUILD)
	$(Q)$(call update-token-stamp,CORE_CONFIG_TOKEN=$(CORE_CONFIG_TOKEN))

$(JS2300_CONFIG_STAMP): FORCE | $(BUILD)
	$(Q)$(call update-token-stamp,JS2300_CONFIG_TOKEN=$(JS2300_CONFIG_TOKEN))

$(FRONTEND_CONFIG_STAMP): FORCE | $(BUILD)
	$(Q)$(call update-token-stamp,FRONTEND_CONFIG_TOKEN=$(FRONTEND_CONFIG_TOKEN))

$(HCRTOS_FFMPEG_SOURCE_CONFIG): FORCE | $(BUILD)
	$(Q)$(call update-token-stamp,HCRTOS_FFMPEG_SOURCE_TOKEN=$(HCRTOS_FFMPEG_SOURCE_TOKEN))

$(DEVICE_OBJECTS): $(BUILD_CONFIG_STAMP)
$(FASTBOOT_OBJECTS): $(FASTBOOT_CONFIG_STAMP)

# These objects print build identity in device logs. Rebuild them only when the
# embedded identity changes, and let dependent core modules relink from the
# updated libunifrog archive.
$(BUILD_IDENTITY_OBJECTS): $(BUILD_IDENTITY_STAMP)
.DELETE_ON_ERROR:
.PHONY: all help help-options setup setup-min setup-cores doctor smoke-doctor deps deps-status \
	dep-status dep-edit dep-update dep-finalize dep-refresh dep-log dep-diff dep-patches-check \
	upgrade-pins upgrade-deps repo-check dev-check quick-check check verify clean distclean rebuild list-cores \
	core core-archive core-out ffmpeg config-check host-check host-visual-check host-frontend-check \
	host-frontend-run linux-runner linux-run linux-runner-check \
	core-registry-check frontend-model-check frontend-structure-check core-manifest-check \
	deps-alpine deps-ubuntu deps-sdk deps-js2300 deps-support deps-cores deps-core-smoke deps-lvgl deps-ffmpeg \
	frontend-package core-package module-package sdcard-package sd-zip install refresh-sd refresh-sd-clean \
	asdcheck fastboot-check fastboot-only-check layout-check firmware-link-check boot-logo-check js2300-check frontend-check \
	quick-core-check core-smoke-check sdk dtb lib fastboot fastboot-only size component-sizes \
	ci-deps ci-smoke-deps \
	ci-toolchain ci-commit-smoke ci-commit-check ci-sd-zip print-config mmc-host-vendor-extract \
	qemu-build qemu-smoke

all: $(ASD) sdcard-package

# QEMU is maintained in its own repository because it is also useful for
# stock-firmware and kernel work. Build it only when the configured binary is
# absent; a prebuilt binary keeps the normal smoke loop fast.
qemu-build:
	$(Q)if test -x "$(QEMU_BIN)"; then :; else \
		test -d "$(QEMU_DIR)" || { \
			echo "missing QEMU source directory: $(QEMU_DIR)" >&2; \
			echo "set QEMU_DIR or build frog2k-qemu separately" >&2; exit 1; \
		}; \
		$(MAKE) --no-print-directory -C "$(QEMU_DIR)" build \
			QEMU_WORK="$(QEMU_WORK)" QEMU_JOBS="$(JOBS)"; \
	fi

qemu-smoke: qemu-build $(ASD)
	$(LOG_ECHO) "  QEMU    SF2000 UniFrog boot"
	$(Q)test -x "$(QEMU_BIN)"
	$(Q)test -s "$(QEMU_BIOS)"
	$(Q)mkdir -p "$(QEMU_LOG_DIR)"
	$(Q)set -eu; \
	status=0; \
	timeout "$(QEMU_TIMEOUT)s" "$(QEMU_BIN)" \
		-M sf2000 -bios "$(QEMU_BIOS)" -kernel "$(ASD)" \
		-display none -serial none -monitor none \
		-d guest_errors,unimp -D "$(QEMU_TRACE_LOG)" \
		>"$(QEMU_CONSOLE_LOG)" 2>&1 || status=$$?; \
	if test "$$status" -ne 124; then \
		cat "$(QEMU_CONSOLE_LOG)" >&2; \
		test "$$status" -eq 0; \
	fi; \
	grep -Fq 'sf2000: loaded ASD' "$(QEMU_CONSOLE_LOG)" || { \
		cat "$(QEMU_CONSOLE_LOG)" >&2; exit 1; \
	}; \
	grep -Fq 'name=unifrog.main.start' "$(QEMU_TRACE_LOG)" || { \
		cat "$(QEMU_TRACE_LOG)" >&2; exit 1; \
	}
	$(LOG_ECHO) "  OK      QEMU boot trace"
setup: deps ci-toolchain
setup-min: deps-sdk deps-js2300 deps-lvgl deps-ffmpeg ffmpeg deps-support ci-toolchain
setup-cores: deps-sdk deps-js2300 deps-lvgl deps-ffmpeg ffmpeg deps-cores ci-toolchain

include mk/agent.mk

verify:
	$(Q)$(MAKE) --no-print-directory quick-check
	$(Q)$(MAKE) --no-print-directory host-full-check
	$(Q)$(MAKE) --no-print-directory check
	$(Q)$(MAKE) --no-print-directory fastboot-check

help:
	@ASD="$(ASD)" OUT="$(OUT)" SDZIP="$(SDZIP)" SDCARD="$(SDCARD)" sh tools/make-help.sh common

help-options:
	@sh tools/make-help.sh options

mmc-host-vendor-extract:
	@OUT_DIR="$(BUILD)/vendor/mmchosthc15" CROSS_COMPILE="$(CROSS_COMPILE)" SDK="$(SDK)" \
		sh tools/extract-mmchosthc15.sh

include mk/config-checks.mk

list-cores:
	@printf '%s\n' $(LIBRETRO_CORE_IDS)

ifeq ($(CORE),)
core core-archive core-out:
	@echo "usage: make $@ CORE=<id>"
	@echo "supported cores: $(LIBRETRO_CORE_IDS)"
	@exit 2
else
core: $(FRONTEND_PACKAGE_STAMP) $(SELECTED_CORE_BIN)

core-archive: $($(SELECTED_CORE_VAR)_CORE_LIB)

core-out: $($(SELECTED_CORE_VAR)_CORE_OUT)
endif
deps: deps-sdk deps-js2300 deps-lvgl deps-cores deps-ffmpeg ffmpeg

deps-alpine:
	apk add git make dtc tcc tcc-libs-static musl-dev zlib-dev ccache curl tar xz zip \
		pkgconf libxcb-dev

deps-ubuntu:
	@echo "sudo apt-get update && sudo apt-get install -y git make device-tree-compiler tcc zlib1g-dev ccache curl xz-utils zip"

deps-sdk:
	git config --global --add safe.directory "$(abspath .)" 2>/dev/null || true
	git submodule sync unifrog-hcrtos-sdk
	git submodule update --init --depth 1 --filter=blob:none --jobs "$(JOBS)" \
		unifrog-hcrtos-sdk

deps-js2300: $(JS2300_FETCH_STAMP)

$(JS2300_FETCH_STAMP):
	$(Q)set -eu; \
	if test -d "$(JS2300)/.git"; then \
		git -C "$(JS2300)" diff --quiet --ignore-submodules --; \
		git -C "$(JS2300)" diff --cached --quiet --; \
	else \
		test ! -e "$(JS2300)" || { echo "dependency path exists but is not a git checkout: $(JS2300)" >&2; exit 1; }; \
		mkdir -p "$(dir $(JS2300))"; \
		git clone --filter=blob:none --no-checkout "$(JS2300_URL)" "$(JS2300)"; \
	fi; \
	git -C "$(JS2300)" fetch --depth=1 origin "$(JS2300_REF)"; \
	git -C "$(JS2300)" checkout --detach "$(JS2300_REF)"; \
	test "$$(git -C "$(JS2300)" rev-parse HEAD)" = "$(JS2300_REF)"; \
	printf '%s\n' "$(JS2300_REF)" > "$@"

deps-lvgl:
	$(Q)$(call root-deps-cmd,lvgl,,setup)

deps-ffmpeg: $(HCRTOS_FFMPEG_SOURCE_STAMP)

$(HCRTOS_FFMPEG_SOURCE_STAMP): $(HCRTOS_FFMPEG_SOURCE_CONFIG) $(HCRTOS_FFMPEG_PATCH) Makefile
	$(Q)$(call root-deps-cmd,ffmpeg,,setup)

ffmpeg: $(HCRTOS_FFMPEG_STAMP)

$(HCRTOS_FFMPEG_STAMP): $(HCRTOS_FFMPEG_SOURCE_STAMP) Makefile | $(BUILD)
	$(Q)$(HCRTOS_FFMPEG_BUILD_ENV) sh tools/hcrtos-ffmpeg-build.sh

dep-status:
	@case "$(DEP_EFFECTIVE)" in \
	"" ) $(call root-deps-cmd,,,$@); $(MAKE) --no-print-directory -C $(CORES) dep-status $(CORE_MAKE_ARGS) ;; \
	lvgl|ffmpeg|hcrtos-ffmpeg) $(call root-deps-cmd,$(DEP_EFFECTIVE),,$@) ;; \
	*) $(MAKE) --no-print-directory -C $(CORES) dep-status DEP="$(DEP_EFFECTIVE)" $(CORE_MAKE_ARGS) ;; \
	esac

dep-edit:
	@test -n "$(DEP_EFFECTIVE)" || { echo "usage: make dep-edit DEP=<dependency>"; exit 1; }
	@case "$(DEP_EFFECTIVE)" in \
	lvgl|ffmpeg|hcrtos-ffmpeg) $(call root-deps-cmd,$(DEP_EFFECTIVE),,$@) ;; \
	*) $(MAKE) --no-print-directory -C $(CORES) dep-edit DEP="$(DEP_EFFECTIVE)" $(CORE_MAKE_ARGS) ;; \
	esac

dep-update:
	@test -n "$(DEP_EFFECTIVE)" || { echo "usage: make dep-update DEP=<dependency> [REF=<ref>]"; exit 1; }
	@case "$(DEP_EFFECTIVE)" in \
	lvgl|ffmpeg|hcrtos-ffmpeg) $(call root-deps-cmd,$(DEP_EFFECTIVE),$(REF),$@) ;; \
	*) $(MAKE) --no-print-directory -C $(CORES) dep-update DEP="$(DEP_EFFECTIVE)" REF="$(REF)" $(CORE_MAKE_ARGS) ;; \
	esac

dep-refresh:
	@test -n "$(DEP_EFFECTIVE)" || { echo "usage: make dep-refresh DEP=<dependency>"; exit 1; }
	@case "$(DEP_EFFECTIVE)" in \
	lvgl|ffmpeg|hcrtos-ffmpeg) $(call root-deps-cmd,$(DEP_EFFECTIVE),,$@) ;; \
	*) $(MAKE) --no-print-directory -C $(CORES) dep-refresh DEP="$(DEP_EFFECTIVE)" $(CORE_MAKE_ARGS) ;; \
	esac

dep-finalize:
	@test -n "$(DEP_EFFECTIVE)" || { echo "usage: make dep-finalize DEP=<dependency> REF=<ref>"; exit 1; }
	@test -n "$(REF)" || { echo "usage: make dep-finalize DEP=<dependency> REF=<ref>"; exit 1; }
	@case "$(DEP_EFFECTIVE)" in \
	lvgl|ffmpeg|hcrtos-ffmpeg) $(call root-deps-cmd,$(DEP_EFFECTIVE),$(REF),$@) ;; \
	*) $(MAKE) --no-print-directory -C $(CORES) dep-finalize DEP="$(DEP_EFFECTIVE)" REF="$(REF)" $(CORE_MAKE_ARGS) ;; \
	esac

dep-log:
	@test -n "$(DEP_EFFECTIVE)" || { echo "usage: make dep-log DEP=<dependency>"; exit 1; }
	@case "$(DEP_EFFECTIVE)" in \
	lvgl|ffmpeg|hcrtos-ffmpeg) $(call root-deps-cmd,$(DEP_EFFECTIVE),,$@) ;; \
	*) $(MAKE) --no-print-directory -C $(CORES) dep-log DEP="$(DEP_EFFECTIVE)" $(CORE_MAKE_ARGS) ;; \
	esac

dep-diff:
	@test -n "$(DEP_EFFECTIVE)" || { echo "usage: make dep-diff DEP=<dependency>"; exit 1; }
	@case "$(DEP_EFFECTIVE)" in \
	lvgl|ffmpeg|hcrtos-ffmpeg) $(call root-deps-cmd,$(DEP_EFFECTIVE),,$@) ;; \
	*) $(MAKE) --no-print-directory -C $(CORES) dep-diff DEP="$(DEP_EFFECTIVE)" $(CORE_MAKE_ARGS) ;; \
	esac

dep-patches-check:
	@case "$(DEP_EFFECTIVE)" in \
	"" ) $(call root-deps-cmd,,,$@); $(MAKE) --no-print-directory -C $(CORES) dep-patches-check $(CORE_MAKE_ARGS) ;; \
	lvgl|ffmpeg|hcrtos-ffmpeg) $(call root-deps-cmd,$(DEP_EFFECTIVE),,$@) ;; \
	*) $(MAKE) --no-print-directory -C $(CORES) dep-patches-check CORE_IDS="$(DEP_EFFECTIVE)" $(CORE_MAKE_ARGS) ;; \
	esac

core-manifest-check:
	$(Q)BUILD="$(BUILD)" CORES="$(CORES)" \
		ASSOCIATIONS_DEFAULT="$(ASSOCIATIONS_DEFAULT)" \
		CORE_PACKAGE_SPECS="$(CORE_PACKAGE_SPECS)" \
		CORE_LICENSE_SPECS="$(CORE_LICENSE_SPECS)" \
		NESTED_CORE_SPECS="$(NESTED_CORE_SPECS)" \
		CORE_SPECS="$(CORE_SPECS)" \
		LIBRETRO_CORE_IDS="$(LIBRETRO_CORE_IDS)" \
		sh tools/core-manifest-check.sh

deps-status:
	$(Q)PIN_MODE="$(PIN_MODE)" LVGL_REF="$(LVGL_REF)" LVGL_URL="$(LVGL_URL)" \
		sh tools/root-pins.sh status
	$(Q)$(MAKE) --no-print-directory -C $(CORES) pin-status PIN_MODE=$(PIN_MODE) \
		CORE_SOURCE_ROOT=$(abspath $(CORE_SOURCE_ROOT)) \
		CORE_SUPPORT_ROOT=$(abspath $(CORE_SUPPORT_ROOT))

upgrade-pins:
	$(Q)PIN_MODE="$(PIN_MODE)" LVGL_REF="$(LVGL_REF)" LVGL_URL="$(LVGL_URL)" \
		sh tools/root-pins.sh upgrade
	$(Q)$(MAKE) --no-print-directory -C $(CORES) upgrade-pins PIN_MODE=$(PIN_MODE) \
		CORE_SOURCE_ROOT=$(abspath $(CORE_SOURCE_ROOT)) \
		CORE_SUPPORT_ROOT=$(abspath $(CORE_SUPPORT_ROOT))

upgrade-deps: upgrade-pins
	$(Q)$(MAKE) --no-print-directory deps PIN_MODE=$(PIN_MODE)

deps-support:
	$(Q)$(MAKE) -C $(CORES) support-init $(CORE_MAKE_ARGS)

deps-cores:
	$(Q)$(MAKE) -C $(CORES) init $(CORE_MAKE_ARGS)

deps-core-smoke:
	$(Q)$(MAKE) -C $(CORES) smoke-init $(CORE_MAKE_ARGS)
doctor:
	$(Q)TOOLCHAIN="$(TOOLCHAIN)" CC="$(CC)" LD="$(LD)" AR="$(AR)" \
		NM="$(NM)" READELF="$(READELF)" OBJCOPY="$(OBJCOPY)" \
		HOSTCC="$(HOSTCC)" DTC="$(DTC)" GCC_LIBDIR="$(GCC_LIBDIR)" \
		SYS_LIBDIR="$(SYS_LIBDIR)" SDK="$(SDK)" CORES="$(CORES)" \
		HCRTOS_FFMPEG_SOURCE="$(HCRTOS_FFMPEG_SOURCE)" DTS="$(DTS)" \
		CORE_SOURCE_ROOT="$(CORE_SOURCE_ROOT)" \
		CORE_SUPPORT_ROOT="$(CORE_SUPPORT_ROOT)" LZ4_DIR="$(LZ4_DIR)" \
		STB_DIR="$(STB_DIR)" PACKAGE_NEEDS_CHD="$(PACKAGE_NEEDS_CHD)" \
		JS2300="$(JS2300)" sh tools/doctor.sh full

smoke-doctor:
	$(Q)TOOLCHAIN="$(TOOLCHAIN)" CC="$(CC)" AR="$(AR)" HOSTCC="$(HOSTCC)" \
		GCC_LIBDIR="$(GCC_LIBDIR)" SDK="$(SDK)" CORES="$(CORES)" \
		CORE_SOURCE_ROOT="$(CORE_SOURCE_ROOT)" \
		CORE_SUPPORT_ROOT="$(CORE_SUPPORT_ROOT)" JS2300="$(JS2300)" \
		sh tools/doctor.sh smoke

repo-check:
	$(LOG_ECHO) "  CHECK   repository hygiene"
	$(Q)MAKE_CMD="$(MAKE)" sh tools/repo-check.sh

quick-check:
	$(Q)$(MAKE) --no-print-directory repo-check
	$(Q)$(MAKE) --no-print-directory config-check
	$(Q)$(MAKE) --no-print-directory doctor
	$(LOG_ECHO) "  CHECK   Linux host suite"
	$(Q)$(MAKE) --no-print-directory host-quick-check LINUX_CORE_IDS="$(QUICK_CORE)"
	$(LOG_ECHO) "  CHECK   core smoke"
	$(Q)$(MAKE) --no-print-directory core-smoke-check
	$(LOG_ECHO) "  CHECK   frontend"
	$(Q)$(MAKE) --no-print-directory frontend-check
	$(LOG_ECHO) "  CHECK   js2300 script runtime"
	$(Q)$(MAKE) --no-print-directory js2300-check
	$(LOG_ECHO) "  CHECK   boot logo"
	$(Q)$(MAKE) --no-print-directory boot-logo-check
	$(LOG_ECHO) "  CHECK   firmware link"
	$(Q)$(MAKE) --no-print-directory firmware-link-check
	$(LOG_ECHO) "  CORE    $(QUICK_CORE) happy path"
	$(Q)$(MAKE) --no-print-directory quick-core-check
	@echo "OK"

dev-check:
	$(Q)$(MAKE) --no-print-directory repo-check
	$(Q)$(MAKE) --no-print-directory config-check
	$(Q)$(MAKE) --no-print-directory doctor
	$(LOG_ECHO) "  CHECK   Linux host suite"
	$(Q)$(MAKE) --no-print-directory host-quick-check LINUX_CORE_IDS="$(QUICK_CORE)"
	$(LOG_ECHO) "  CHECK   frontend"
	$(Q)$(MAKE) --no-print-directory frontend-check
	$(LOG_ECHO) "  CHECK   js2300 script runtime"
	$(Q)$(MAKE) --no-print-directory js2300-check
	$(LOG_ECHO) "  CHECK   boot logo"
	$(Q)$(MAKE) --no-print-directory boot-logo-check
	$(LOG_ECHO) "  CHECK   firmware link"
	$(Q)$(MAKE) --no-print-directory firmware-link-check
	$(LOG_ECHO) "  CORE    $(QUICK_CORE) happy path"
	$(Q)$(MAKE) --no-print-directory quick-core-check
	@echo "OK"

quick-core-check:
	$(Q)$(MAKE) --no-print-directory core CORE="$(QUICK_CORE)"

firmware-link-check: $(OUT)/$(TARGET).out
	$(LOG_ECHO) "  OK      firmware link"

core-smoke-check:
	$(Q)$(MAKE) $(CORE_SMOKE_MAKE_FLAGS) -C $(CORES) smoke-check $(CORE_MAKE_ARGS)

sdk: $(SDK_BUILD_STAMP)

$(SDK_BUILD_STAMP): $(SDK_KERNEL_LIB) | $(BUILD)
	$(Q)touch $@

$(SDK_KERNEL_LIB): $(DTS_INPUTS) | $(BUILD)
	$(Q)$(MAKE) -C $(SDK) check TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS) \
		SD_MODE=$(SD_MODE) BOARD_DTS=$(abspath $(DTS)) \
		DTS_INCLUDE=$(abspath dts/include)
	$(Q)test -s $@

js2300-check: deps-js2300
	$(Q)$(MAKE) -C $(JS2300) check TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) HOSTCC=$(HOSTCC) CCACHE=$(CCACHE)

frontend-check: core-registry-check frontend-model-check
	$(Q)$(MAKE) --no-print-directory frontend-theme-check
	$(LOG_ECHO) "  OK      frontend sources"

frontend-theme-check: $(THEME_ARCHIVE_CHECK) $(THEME_VISUAL_CHECK) tools/frontend-theme-check.sh
	$(LOG_ECHO) "  CHECK   muxthm theme archive"
	$(LOG_ECHO) "  CHECK   theme visual"
	$(Q)BUILD="$(BUILD)" THEME_ARCHIVE_CHECK="$(THEME_ARCHIVE_CHECK)" \
		THEME_VISUAL_CHECK="$(THEME_VISUAL_CHECK)" \
		sh tools/frontend-theme-check.sh

include mk/host-checks.mk

frontend-package: $(FRONTEND_PACKAGE_STAMP)

$(FRONTEND_PACKAGE_STAMP): \
	Makefile $(FRONTEND_CONFIG_STAMP) $(BUILD_IDENTITY_STAMP) LICENSE \
	$(THIRD_PARTY_NOTICE) $(LANGUAGE_FILES) $(FONT_FILES) $(SCRIPT_FILES) \
	$(SELECTED_CORE_LICENSE_FILES) \
	$(FROGUI_FONT_FILES) \
	$(SETTINGS_EXAMPLE) $(ASSOCIATIONS_DEFAULT) \
	tools/frontend-package.sh | $(OUT)
	$(Q)FRONTEND_PACKAGE="$(FRONTEND_PACKAGE)" USER_PACKAGE="$(USER_PACKAGE)" \
		CORE_PACKAGE="$(CORE_PACKAGE)" FRONTEND_MANIFEST="$(FRONTEND_MANIFEST)" \
		SETTINGS_EXAMPLE="$(SETTINGS_EXAMPLE)" SETTINGS_PACKAGE="$(FRONTEND_PACKAGE)/unifrog.ini" \
		ASSOCIATIONS_DEFAULT="$(ASSOCIATIONS_DEFAULT)" \
		THIRD_PARTY_NOTICE="$(THIRD_PARTY_NOTICE)" \
		CORE_LICENSE_SPECS="$(SELECTED_CORE_LICENSE_SPECS)" \
		LANGUAGE_FILES="$(LANGUAGE_FILES)" FONT_FILES="$(FONT_FILES)" \
		FROGUI_FONT_FILES="$(FROGUI_FONT_FILES)" \
		SCRIPT_ROOT="$(SCRIPT_ROOT)" \
		SCRIPT_FILES="$(SCRIPT_FILES)" \
		UNIFROG_GIT_COMMIT="$(UNIFROG_GIT_COMMIT)" \
		UNIFROG_GIT_DIRTY="$(UNIFROG_GIT_DIRTY)" \
		UNIFROG_SDK_GIT_COMMIT="$(UNIFROG_SDK_GIT_COMMIT)" \
		UNIFROG_CORES_GIT_COMMIT="$(UNIFROG_CORES_GIT_COMMIT)" \
		UNIFROG_JS2300_GIT_COMMIT="$(UNIFROG_JS2300_GIT_COMMIT)" \
		UNIFROG_FRONTEND_GIT_COMMIT="$(UNIFROG_FRONTEND_GIT_COMMIT)" \
		HCRTOS_MEDIA="$(HCRTOS_MEDIA)" \
		FRONTEND_PACKAGE_STAMP="$@" sh tools/frontend-package.sh

core-package: $(FRONTEND_PACKAGE_STAMP) $(if $(CORE),$(SELECTED_CORE_BIN),$(LIBRETRO_CORE_BINS))

module-package: $(FRONTEND_PACKAGE_STAMP) $(HCRTOS_MEDIA_MODULE_BINS)
ifeq ($(HCRTOS_MEDIA_MODULE_BINS),)
	$(Q)rm -rf $(MODULE_PACKAGE)
endif

sdcard-package: $(SDCARD_BIOS_PACKAGE) $(SDCARD_FIRMWARE_PACKAGE) core-package module-package

$(CORE_PACKAGE):
	$(Q)mkdir -p $@

$(MODULE_PACKAGE):
	$(Q)mkdir -p $@

$(dir $(SDCARD_BIOS_PACKAGE)) $(dir $(SDCARD_FIRMWARE_PACKAGE)):
	$(Q)mkdir -p $@

$(SDCARD_BIOS_PACKAGE): $(FASTBOOT_ASD) | $(dir $(SDCARD_BIOS_PACKAGE))
	$(LOG_ECHO) "  SDCARD  $@"
	$(Q)if test -f $@ && cmp -s $< $@; then touch $@; else cp $< $@; fi
$(SDCARD_FIRMWARE_PACKAGE): $(OUT)/unifrog.bin | $(dir $(SDCARD_FIRMWARE_PACKAGE))
	$(LOG_ECHO) "  SDCARD  $@"
	$(Q)rm -f $(OUT)/sdcard/firmware/unifrog.bin
	$(Q)rmdir $(OUT)/sdcard/firmware 2>/dev/null || true
	$(Q)if test -f $@ && cmp -s $< $@; then touch $@; else cp $< $@; fi
define CORE_ENTRY_DEFINES_RULE
$(1): CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"$(2)\" -DUNIFROG_MODULE_EXTENSIONS=\"$(3)\" $(if $(4),-DUNIFROG_MODULE_SYMBOL_PREFIX=$(4))
endef

$(foreach var,$(LIBRETRO_CORE_VARS),$(eval $(call CORE_ENTRY_DEFINES_RULE,$($(var)_CORE_ENTRY),$($(var)_CORE_ID),$($(var)_CORE_EXTENSIONS),$($(var)_CORE_SYMBOL_PREFIX))))

$(BUILD)/core_modules/%_entry.o: foundation/src/modules/unifrog_core_module_entry.c include/unifrog/core_module.h | $(BUILD)
	$(LOG_ECHO) "  CC      $< ($*)"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) $(CORE_MODULE_DEFINES) -MD -MP -c $< -o $@

$(BUILD)/core_modules/support.o: foundation/src/modules/unifrog_core_module_support.c include/unifrog/abi.h | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/runtime_modules/media/%.o: components/media/src/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $< (runtime module)"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/runtime_modules/media/unifrog_media.o: components/media/src/platform/sf2000/unifrog_media.c
$(BUILD)/runtime_modules/media/unifrog_media_buffered_io.o: components/media/src/platform/sf2000/unifrog_media_buffered_io.c
$(BUILD)/runtime_modules/media/unifrog_media_direct_audio.o: components/media/src/platform/sf2000/unifrog_media_direct_audio.c
$(BUILD)/runtime_modules/media/unifrog_media_gb300_audio_diag.o: components/media/src/platform/sf2000/unifrog_media_gb300_audio_diag.c

$(BUILD)/runtime_modules/media/unifrog_media.o \
$(BUILD)/runtime_modules/media/unifrog_media_buffered_io.o \
$(BUILD)/runtime_modules/media/unifrog_media_direct_audio.o \
$(BUILD)/runtime_modules/media/unifrog_media_gb300_audio_diag.o:
	$(LOG_ECHO) "  CC      $< (runtime module)"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) -MD -MP -c \
		components/media/src/platform/sf2000/$(@F:.o=.c) -o $@

$(HCRTOS_MEDIA_MODULE_ARCHIVE): $(HCRTOS_MEDIA_MODULE_OBJECTS) | $(OUT)
	$(LOG_ECHO) "  AR      $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(AR) rcs $@ $^

$(HCRTOS_MEDIA_MODULE_OUT): $(HCRTOS_MEDIA_MODULE_ARCHIVE) \
	linker/core-module.ld linker/hc15xx/peripherals.ld \
	$(SDK_BUILD_STAMP) $(HCRTOS_FFMPEG_STAMP) $(BUILD_CONFIG_STAMP) | $(OUT)
	$(LOG_ECHO) "  LD      $@"
	$(Q)$(LD) $(CORE_MODULE_LDFLAGS) -o $@ -Map $@.map -u unifrog_core_module_entry --start-group $(HCRTOS_MEDIA_MODULE_ARCHIVE) $(HCRTOS_MEDIA_MODULE_LDLIBS) --whole-archive $(HCRTOS_MEDIA_WHOLE_LIBS) --no-whole-archive --end-group

$(HCRTOS_MEDIA_MODULE_BIN): $(HCRTOS_MEDIA_MODULE_OUT) | $(MODULE_PACKAGE)
	$(LOG_ECHO) "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(BUILD)/support/lz4/%.o: $(LZ4_DIR)/lib/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(LZ4_CFLAGS) -MD -MP -c $< -o $@

$(ZSTD_DECODER_OBJ): $(ZSTD_DECODER_SRC) | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(ZSTD_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/core_modules/js2300_libretro_core.o: js2300/src/libretro_core/js2300_libretro_core.c $(JS2300)/include/js2300/js2300.h | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) -MD -MP -c $< -o $@

$(JS2300_CORE_LIB): $(JS2300_CORE_OBJECTS) | $(BUILD)
	$(LOG_ECHO) "  AR      $@"
	$(Q)rm -f $@
	$(Q)$(AR) rcs $@ $^

$(BUILD)/lvgl/%.o: $(LVGL_DIR)/src/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS_FAST) -w -MD -MP -c $< -o $@

define CORE_MODULE_RULES
$(BUILD)/core_modules/$(1).out: $(BUILD)/core_modules/$(1)_entry.o $(BUILD)/core_modules/support.o $$($(2)_CORE_LIB) $$($(2)_CORE_SUPPORT_LIBS) $(LIBRETRO_COMMON_LIB) $(LIBUNIFROG) linker/core-module.ld linker/hc15xx/peripherals.ld $(SDK_BUILD_STAMP) $(BUILD_CONFIG_STAMP) | $(OUT)
	$(LOG_ECHO) "  LD      $$@"
	$(Q)$(LD) $(CORE_MODULE_LDFLAGS) -o $$@ -Map $$@.map -u unifrog_core_module_entry --start-group $$(filter %.o %.a,$$^) $(CORE_MODULE_LDLIBS) --end-group

$(3): $(BUILD)/core_modules/$(1).out | $(CORE_PACKAGE)
	$(LOG_ECHO) "  OBJCOPY $$@"
	$(Q)$(OBJCOPY) -O binary $$< $$@
endef

$(foreach var,$(LIBRETRO_CORE_VARS),$(eval $(call CORE_MODULE_RULES,$($(var)_CORE_MODULE),$(var),$($(var)_CORE_BIN))))

$(BUILD) $(OUT):
	$(Q)mkdir -p $@

$(BUILD)/%.o: foundation/src/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/frontend/app/%.o: components/frontend/src/app/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/frontend/%.o: components/frontend/src/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/libretro/%.o: components/libretro/src/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/libretro/%.o: components/libretro/src/%.S | $(BUILD)
	$(LOG_ECHO) "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -D__ASSEMBLY__ -MD -MP -c $< -o $@

$(BUILD)/media/%.o: components/media/src/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/media/unifrog_media.o: components/media/src/platform/sf2000/unifrog_media.c
$(BUILD)/media/unifrog_media_buffered_io.o: components/media/src/platform/sf2000/unifrog_media_buffered_io.c
$(BUILD)/media/unifrog_media_direct_audio.o: components/media/src/platform/sf2000/unifrog_media_direct_audio.c
$(BUILD)/media/unifrog_media_gb300_audio_diag.o: components/media/src/platform/sf2000/unifrog_media_gb300_audio_diag.c

$(BUILD)/media/unifrog_media.o \
$(BUILD)/media/unifrog_media_buffered_io.o \
$(BUILD)/media/unifrog_media_direct_audio.o \
$(BUILD)/media/unifrog_media_gb300_audio_diag.o:
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c \
		components/media/src/platform/sf2000/$(@F:.o=.c) -o $@

$(BUILD)/runtime/unifrog_sdk_optional_stubs.o: components/media/src/unifrog_sdk_optional_stubs.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/diagnostics/%.o: components/diagnostics/src/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/runtime/main.o: apps/firmware/src/main.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/js2300/%.o: js2300/src/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/runtime/unifrog_boot_logo.o: $(BOOT_LOGO_RGB565_INC)
$(BUILD)/runtime/unifrog_fastboot_stage.o: \
	foundation/src/runtime/unifrog_fastboot_stage.S $(FASTBOOT_STAGE_BIN) | $(BUILD)
	$(LOG_ECHO) "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -D__ASSEMBLY__ \
		-DFASTBOOT_STAGE_BIN_PATH=\"$(FASTBOOT_STAGE_BIN)\" \
		-MD -MP -c $< -o $@
$(BUILD)/frontend/app/frontend_app.o: $(DEFAULT_OPTIONS_HEADER)
$(BUILD)/platform/sf2000/display/unifrog_fb.o $(BUILD)/platform/sf2000/display/unifrog_ge.o $(BUILD)/display/unifrog_presenter.o $(BUILD)/display/unifrog_surface_alloc.o: CFLAGS := $(CFLAGS_VIDEO)
$(BUILD)/display/unifrog_image.o: CFLAGS += -I$(STB_DIR) -I$(NANOSVG_DIR) -Wno-unused-function -Wno-maybe-uninitialized -Wno-deprecated-declarations
$(BUILD)/display/unifrog_gfx.o $(BUILD)/platform/sf2000/unifrog_perf.o $(BUILD)/platform/sf2000/unifrog_scpu.o: CFLAGS := $(CFLAGS_FAST)
$(BUILD)/platform/sf2000/audio/unifrog_audio.o $(BUILD)/platform/sf2000/audio/unifrog_audio_gb300_diag.o: CFLAGS := $(CFLAGS_AUDIO)
$(BUILD)/media/unifrog_media_config.o $(BUILD)/runtime_modules/media/unifrog_media_config.o $(BUILD)/media/unifrog_media.o $(BUILD)/media/unifrog_media_buffered_io.o $(BUILD)/media/unifrog_media_direct_audio.o $(BUILD)/media/unifrog_media_gb300_audio_diag.o $(BUILD)/runtime_modules/media/unifrog_media.o $(BUILD)/runtime_modules/media/unifrog_media_buffered_io.o $(BUILD)/runtime_modules/media/unifrog_media_direct_audio.o $(BUILD)/runtime_modules/media/unifrog_media_gb300_audio_diag.o: $(HCRTOS_FFMPEG_STAMP) $(DEFAULT_OPTIONS_HEADER)
ifneq ($(filter native firmware,$(HCRTOS_MEDIA)),)
$(BUILD)/display/unifrog_image.o: $(HCRTOS_FFMPEG_STAMP)
endif
$(BUILD)/runtime/unifrog_sdk_optional_stubs.o: $(DEFAULT_OPTIONS_HEADER)
$(BUILD)/media/unifrog_media.o $(BUILD)/media/unifrog_media_buffered_io.o $(BUILD)/media/unifrog_media_direct_audio.o $(BUILD)/media/unifrog_media_gb300_audio_diag.o: CFLAGS += $(HCRTOS_FFMPEG_ABI_CFLAGS)
$(HCRTOS_MEDIA_MODULE_OBJECTS): CORE_MODULE_CFLAGS += -Icomponents/media/src
$(BUILD)/runtime_modules/media/unifrog_media.o $(BUILD)/runtime_modules/media/unifrog_media_buffered_io.o $(BUILD)/runtime_modules/media/unifrog_media_direct_audio.o $(BUILD)/runtime_modules/media/unifrog_media_gb300_audio_diag.o: CORE_MODULE_CFLAGS += $(HCRTOS_FFMPEG_ABI_CFLAGS)
$(BUILD)/libretro/unifrog_libretro_host.o \
$(BUILD)/libretro/unifrog_libretro_runtime.o \
$(BUILD)/libretro/unifrog_libretro_session.o: CFLAGS := $(CFLAGS_FAST) -I$(LZ4_DIR)/lib -I$(ZSTD_DIR)
$(BUILD)/frontend/libretro_frontend/libretro_frontend_quick_menu.o $(BUILD)/libretro/unifrog_libretro_content.o: CFLAGS := $(CFLAGS_AUDIO) -I$(LZ4_DIR)/lib -I$(ZSTD_DIR)
$(FOUNDATION_COMPONENT_OBJECTS): CFLAGS += $(FOUNDATION_CFLAGS)
$(FRONTEND_OWNED_COMPONENT_OBJECTS): CFLAGS += -Icomponents/frontend/src/app
$(FRONTEND_COMPONENT_OBJECTS): CFLAGS += $(FRONTEND_CFLAGS)
$(LIBRETRO_COMPONENT_OBJECTS): CFLAGS += -Icomponents/libretro/src $(LIBRETRO_CFLAGS)
$(MEDIA_COMPONENT_OBJECTS): CFLAGS += -Icomponents/media/src $(MEDIA_CFLAGS)
$(DIAGNOSTICS_COMPONENT_OBJECTS): CFLAGS += -Icomponents/diagnostics/src $(DIAGNOSTICS_CFLAGS)

$(BUILD)/%.o: foundation/src/%.S | $(BUILD)
	$(LOG_ECHO) "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -D__ASSEMBLY__ -MD -MP -c $< -o $@

$(FASTBOOT_STAGE_OBJ): apps/fastboot/src/stage1.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(FASTBOOT_CFLAGS) -MD -MP -c $< -o $@

$(FASTBOOT_STAGE_ENTRY_OBJ): apps/fastboot/src/stage_entry.S | $(BUILD)
	$(LOG_ECHO) "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(FASTBOOT_CFLAGS) -D__ASSEMBLY__ -MD -MP -c $< -o $@

$(FASTBOOT_STAGE_OUT): $(FASTBOOT_STAGE_ENTRY_OBJ) $(FASTBOOT_STAGE_OBJ) \
	linker/fastboot/stage.ld $(FASTBOOT_CONFIG_STAMP) | $(OUT)
	$(LOG_ECHO) "  LD      $@"
	$(Q)$(LD) -EL --static -n --gc-sections -T linker/fastboot/stage.ld -Map $@.map -o $@ $(filter %.o,$^)

$(FASTBOOT_STAGE_BIN): $(FASTBOOT_STAGE_OUT) | $(BUILD)
	$(LOG_ECHO) "  OBJCOPY $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(OBJCOPY) -O binary $< $@

$(FASTBOOT_STUB_OBJ): apps/fastboot/src/stub.S $(FASTBOOT_STAGE_BIN) | $(BUILD)
	$(LOG_ECHO) "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(FASTBOOT_CFLAGS) -D__ASSEMBLY__ \
		-DFASTBOOT_STAGE_BIN_PATH=\"$(FASTBOOT_STAGE_BIN)\" \
		-MD -MP -c $< -o $@

$(FASTBOOT_STUB_OUT): $(FASTBOOT_STUB_OBJ) linker/fastboot/stub.ld \
	$(FASTBOOT_CONFIG_STAMP) | $(OUT)
	$(LOG_ECHO) "  LD      $@"
	$(Q)$(LD) -EL --static -n --gc-sections -T linker/fastboot/stub.ld -Map $@.map -o $@ $(filter %.o,$^)

$(FASTBOOT_STUB_BIN): $(FASTBOOT_STUB_OUT)
	$(LOG_ECHO) "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(BOOT_LOGO_TOOL): tools/bootlogo.c $(CORE_SUPPORT_ROOT)/zlib/inflate.c $(BUILD_CONFIG_STAMP) | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -I$(CORE_SUPPORT_ROOT)/zlib \
		tools/bootlogo.c \
		$(CORE_SUPPORT_ROOT)/zlib/adler32.c \
		$(CORE_SUPPORT_ROOT)/zlib/crc32.c \
		$(CORE_SUPPORT_ROOT)/zlib/inffast.c \
		$(CORE_SUPPORT_ROOT)/zlib/inflate.c \
		$(CORE_SUPPORT_ROOT)/zlib/inftrees.c \
		$(CORE_SUPPORT_ROOT)/zlib/uncompr.c \
		$(CORE_SUPPORT_ROOT)/zlib/zutil.c \
		-o $@

$(THEME_ARCHIVE_CHECK): tools/theme_archive_check.c $(CORE_SUPPORT_ROOT)/zlib/inflate.c $(BUILD_CONFIG_STAMP) | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(HOSTCC) $(HOSTCFLAGS) -I$(CORE_SUPPORT_ROOT)/zlib \
		$< \
		$(CORE_SUPPORT_ROOT)/zlib/adler32.c \
		$(CORE_SUPPORT_ROOT)/zlib/crc32.c \
		$(CORE_SUPPORT_ROOT)/zlib/inffast.c \
		$(CORE_SUPPORT_ROOT)/zlib/inflate.c \
		$(CORE_SUPPORT_ROOT)/zlib/inftrees.c \
		$(CORE_SUPPORT_ROOT)/zlib/zutil.c \
		-o $@

$(THEME_VISUAL_CHECK): tools/theme_visual_check.c $(BUILD_CONFIG_STAMP) | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(HOSTCC) $(HOSTCFLAGS) $< -o $@

$(BOOT_LOGO_STAMP): $(BOOT_LOGO_SRC) $(BOOT_LOGO_TOOL) $(BUILD_IDENTITY_STAMP)
	$(LOG_ECHO) "  BOOTLOGO $(UNIFROG_BOOT_VERSION)"
	$(Q)mkdir -p $(dir $(BOOT_LOGO_STAMP))
	$(Q)$(BOOT_LOGO_TOOL) $(BOOT_LOGO_SRC) "$(UNIFROG_BOOT_VERSION)" $(BOOT_LOGO_STAMPED_PPM) $(BOOT_LOGO_RGB565_INC)
	$(Q)touch $@

$(BOOT_LOGO_STAMPED_PPM) $(BOOT_LOGO_RGB565_INC): $(BOOT_LOGO_STAMP)

boot-logo-check: $(BOOT_LOGO_STAMPED_PPM) $(BOOT_LOGO_RGB565_INC)
	@test -s $(BOOT_LOGO_STAMPED_PPM)
	@test -s $(BOOT_LOGO_RGB565_INC)
	$(LOG_ECHO) "  OK      boot logo"

$(DTS_PRE): $(DTS_INPUTS) $(DTS_MODE_STAMP) $(BUILD_CONFIG_STAMP) | $(BUILD)
	@echo "  CPP     $<"
	$(Q)$(CC) $(DTS_CPPFLAGS) -Wp,-MD,$@.d -E -o $@ $<

$(DTB): $(DTS_PRE) $(BUILD_CONFIG_STAMP)
	@echo "  DTC     $@"
	$(Q)$(DTC) -O dtb -o $@ -b 0 $(DTCFLAGS) -d $@.d $<

$(DTB_ASM): $(DTB) | $(BUILD)
	$(LOG_ECHO) "  GEN     $@"
	$(Q)printf '%s\n' \
		'.section .rodata' \
		'.balign 16' \
		'.globl __dtb_start' \
		'__dtb_start:' \
		'.incbin "$(abspath $<)"' \
		'.balign 64' \
		'.globl __dtb_end' \
		'__dtb_end:' \
		'.globl __dtb_size' \
		'__dtb_size:' \
		'.word __dtb_end - __dtb_start' > $@

$(DTB_OBJ): $(DTB_ASM)
	$(LOG_ECHO) "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(LIBUNIFROG): $(UNIFROG_OBJECTS) $(BUILD_CONFIG_STAMP) | $(OUT)
	$(LOG_ECHO) "  AR      $@"
	$(Q)rm -f $@
	$(Q)$(AR) rcs $@ $(filter %.o,$^)

define COMPONENT_ARCHIVE_RULE
$(1): $(2) $(BUILD_CONFIG_STAMP) | $(BUILD)
	$$(LOG_ECHO) "  AR      $$@"
	$$(Q)mkdir -p $$(dir $$@)
	$$(Q)rm -f $$@
	$$(Q)$$(AR) rcs $$@ $$(filter %.o,$$^)
endef

$(eval $(call COMPONENT_ARCHIVE_RULE,$(LIBFOUNDATION_COMPONENT),$(FOUNDATION_COMPONENT_OBJECTS)))
$(eval $(call COMPONENT_ARCHIVE_RULE,$(LIBFRONTEND_COMPONENT),$(FRONTEND_COMPONENT_OBJECTS)))
$(eval $(call COMPONENT_ARCHIVE_RULE,$(LIBLIBRETRO_COMPONENT),$(LIBRETRO_COMPONENT_OBJECTS)))
$(eval $(call COMPONENT_ARCHIVE_RULE,$(LIBMEDIA_COMPONENT),$(MEDIA_COMPONENT_OBJECTS)))
$(eval $(call COMPONENT_ARCHIVE_RULE,$(LIBDIAGNOSTICS_COMPONENT),$(DIAGNOSTICS_COMPONENT_OBJECTS)))

$(OUT)/$(TARGET).out: $(APP_OBJECTS) $(FIRMWARE_COMPONENT_LIBS) $(LIBJS2300) \
	$(LIBRETRO_COMMON_LIB) $(FIRMWARE_LIBRETRO_CORE_LIBS) \
	$(SDK_BUILD_STAMP) $(HCRTOS_FFMPEG_STAMP) $(BUILD_CONFIG_STAMP) | $(OUT)
	$(LOG_ECHO) "  LD      $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ -Map $@.map --start-group $(filter %.o %.a,$^) $(LDLIBS) --whole-archive $(WHOLE_LIBS) --no-whole-archive --end-group

$(LIBJS2300): $(JS2300_FETCH_STAMP) $(JS2300_INPUTS) $(JS2300_CONFIG_STAMP)
	@echo "  JS2300  runtime"
	$(Q)$(MAKE) -C $(JS2300) TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) HOSTCC=$(HOSTCC) CCACHE=$(CCACHE)
	$(Q)test -s $@ && touch $@

define CORE_ARCHIVE_RULE
$(if $(filter 1,$(CORE_BATCH_BUILD)),$(1): $(CORE_ARCHIVE_STAMP),$(1): $(2) $(3))
	$(LOG_ECHO) "  CORE    $(4)"
ifeq ($(CORE_BATCH_BUILD),1)
	$(Q)test -s $$@ && touch $$@
else
	$(Q)$(MAKE) -C $(CORES) $(4) $(CORE_MAKE_ARGS)
	$(Q)test -s $$@ && touch $$@
endif
endef

$(CORE_ARCHIVE_STAMP): $(CORE_BUILD_DEPS) $(CORE_BATCH_SUPPORT_DEPS) | $(BUILD)
	$(LOG_ECHO) "  CORE    all"
	$(Q)$(MAKE) -C $(CORES) all $(CORE_MAKE_ARGS)
	$(Q)touch $@

$(foreach var,$(EXTERNAL_LIBRETRO_CORE_VARS),$(eval $(call CORE_ARCHIVE_RULE,$($(var)_CORE_LIB),$(CORE_BUILD_DEPS),$($(var)_CORE_SUPPORT_LIBS),$($(var)_CORE_TARGET))))

$(LIBRETRO_COMMON_LIB): $(CORE_BUILD_DEPS)
	$(LOG_ECHO) "  CORELIB libretro-common"
	$(Q)$(MAKE) -C $(CORES) libretro-common $(CORE_MAKE_ARGS)
	$(Q)test -s $@ && touch $@

$(CHD_SUPPORT_CORE_LIB): $(CORE_BUILD_DEPS)
	$(LOG_ECHO) "  CORELIB libchdr-support"
	$(Q)$(MAKE) -C $(CORES) chd-support $(CORE_MAKE_ARGS)
	$(Q)test -s $@ && touch $@

$(OUT)/$(TARGET).bin: $(OUT)/$(TARGET).out
	$(LOG_ECHO) "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(ASDPACK): tools/asdpack.c $(BUILD_CONFIG_STAMP) | $(BUILD)
	$(LOG_ECHO) "  HOSTCC  $<"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) $< -o $@

$(ASD): $(OUT)/$(TARGET).bin $(ASDPACK)
	$(LOG_ECHO) "  PACK    $@"
	$(Q)$(ASDPACK) $< $@

$(OUT)/unifrog.bin: $(OUT)/$(TARGET).bin | $(OUT)
	@echo "  COPY    $@"
	$(Q)if test -f $@ && cmp -s $< $@; then touch $@; else cp $< $@; fi

$(FASTBOOT_ASD): $(FASTBOOT_STUB_BIN) $(ASDPACK)
	$(LOG_ECHO) "  PACK    $@"
	$(Q)$(ASDPACK) $< $@
dtb: $(DTB)

lib: $(LIBUNIFROG)

asdcheck: $(ASD)
	$(LOG_ECHO) "  CHECK   $(ASD)"
	$(Q)$(ASDPACK) --check $(ASD)

fastboot: $(FASTBOOT_ASD) $(OUT)/unifrog.bin

fastboot-only: $(FASTBOOT_ASD)

fastboot-only-check: fastboot-only
	@test -s $(FASTBOOT_ASD)
	$(LOG_ECHO) "  CHECK   $(FASTBOOT_ASD)"
	$(Q)$(ASDPACK) --check $(FASTBOOT_ASD)
	$(LOG_ECHO) "  OK      $(FASTBOOT_ASD)"

fastboot-check: sdcard-package tools/build-output-check.sh
	$(LOG_ECHO) "  CHECK   $(FASTBOOT_ASD)"
	$(Q)FASTBOOT_ASD="$(FASTBOOT_ASD)" ASDPACK="$(ASDPACK)" \
		OUT_UNIFROG_BIN="$(OUT)/unifrog.bin" \
		SDCARD_BIOS_PACKAGE="$(SDCARD_BIOS_PACKAGE)" \
		SDCARD_FIRMWARE_PACKAGE="$(SDCARD_FIRMWARE_PACKAGE)" \
		FRONTEND_MANIFEST="$(FRONTEND_MANIFEST)" \
		FRONTEND_PACKAGE="$(FRONTEND_PACKAGE)" \
		LIBRETRO_CORE_BINS="$(LIBRETRO_CORE_BINS)" \
		HCRTOS_MEDIA_MODULE_BINS="$(HCRTOS_MEDIA_MODULE_BINS)" \
		sh tools/build-output-check.sh fastboot
	$(LOG_ECHO) "  OK      $(FASTBOOT_ASD)"

fastboot-check: layout-check

layout-check: $(OUT)/$(TARGET).out $(LIBRETRO_CORE_MODULE_OUTS) $(RUNTIME_MODULE_OUTS)
	$(LOG_ECHO) "  CHECK   link layout"
	$(Q)NM="$(NM)" READELF="$(READELF)" sh tools/layout-check.sh $^

check: sdk $(ASD) sdcard-package layout-check tools/build-output-check.sh
	$(LOG_ECHO) "  CHECK   $(ASD)"
	$(Q)ASD="$(ASD)" LIBUNIFROG="$(LIBUNIFROG)" ASDPACK="$(ASDPACK)" \
		OUT_UNIFROG_BIN="$(OUT)/unifrog.bin" \
		SDCARD_BIOS_PACKAGE="$(SDCARD_BIOS_PACKAGE)" \
		SDCARD_FIRMWARE_PACKAGE="$(SDCARD_FIRMWARE_PACKAGE)" \
		FRONTEND_MANIFEST="$(FRONTEND_MANIFEST)" \
		FRONTEND_PACKAGE="$(FRONTEND_PACKAGE)" \
		LIBRETRO_CORE_BINS="$(LIBRETRO_CORE_BINS)" \
		HCRTOS_MEDIA_MODULE_BINS="$(HCRTOS_MEDIA_MODULE_BINS)" \
		sh tools/build-output-check.sh full
	$(LOG_ECHO) "  OK      $(ASD)"

size: $(ASD) module-package
	@ls -lh $(ASD) $(OUT)/$(TARGET).bin $(OUT)/$(TARGET).out $(LIBUNIFROG) $(DTB) $(HCRTOS_MEDIA_MODULE_BINS)

component-sizes: $(OUT)/$(TARGET).out
	$(Q)sh tools/component-sizes.sh $(OUT)/$(TARGET).out.map

install: fastboot-check layout-check
	$(Q)ASD="$(ASD)" FASTBOOT_ASD="$(FASTBOOT_ASD)" OUT="$(OUT)" \
		SDCARD="$(SDCARD)" SDCARD_BIOS_DIR="$(SDCARD)/bios" \
		SDCARD_FIRMWARE_DIR="$(SDCARD)/unifrog/firmware" \
		SDCARD_USER_DIR="$(SDCARD)/unifrog_data" FRONTEND_PACKAGE="$(FRONTEND_PACKAGE)" \
		THIRD_PARTY_NOTICE="$(THIRD_PARTY_NOTICE)" \
		LANGUAGE_FILES="$(LANGUAGE_FILES)" FONT_FILES="$(FONT_FILES)" \
		SCRIPT_ROOT="$(SCRIPT_ROOT)" \
		SCRIPT_FILES="$(SCRIPT_FILES)" \
		BUILD_PROGRESS="$(BUILD_PROGRESS)" sh tools/install-sdcard.sh

rebuild:
	$(Q)$(MAKE) clean
	$(Q)$(MAKE)

refresh-sd:
	$(LOG_ECHO) "  REFRESH incremental SDCARD=$(SDCARD)"
	$(Q)$(MAKE) install SDCARD=$(SDCARD)

refresh-sd-clean:
	$(Q)$(MAKE) clean
	$(Q)$(MAKE) -C $(SDK) clean TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS)
	$(Q)$(MAKE) -C $(CORES) clean $(CORE_MAKE_ARGS)
	$(Q)$(MAKE) -C $(JS2300) clean
	$(Q)$(MAKE) install SDCARD=$(SDCARD)

sd-zip: sdcard-package
	@command -v zip >/dev/null || { echo "missing: zip"; exit 1; }
	$(LOG_ECHO) "  ZIP     $(SDZIP)"
	$(Q)mkdir -p $(dir $(SDZIP))
	$(Q)rm -f $(SDZIP)
	$(Q)cd $(OUT)/sdcard && zip $(ZIP_COMPRESSION) -r $(abspath $(SDZIP)) . >/dev/null
ci-deps: deps

ci-smoke-deps: deps-sdk deps-js2300 deps-core-smoke

ci-toolchain:
	$(Q)TOOLCHAIN="$(TOOLCHAIN)" TOOLCHAIN_URL="$(TOOLCHAIN_URL)" \
		TOOLCHAIN_SHA256="$(TOOLCHAIN_SHA256)" TOOLCHAIN_ARCHIVE="$(TOOLCHAIN_ARCHIVE)" \
		sh tools/ci-toolchain.sh

ci-commit-smoke: ci-smoke-deps ci-toolchain
	$(Q)$(MAKE) --no-print-directory repo-check
	$(Q)$(MAKE) --no-print-directory smoke-doctor TOOLCHAIN=$(TOOLCHAIN)
	$(LOG_ECHO) "  CHECK   core smoke"
	$(Q)$(MAKE) --no-print-directory core-smoke-check TOOLCHAIN=$(TOOLCHAIN)
	$(LOG_ECHO) "  CHECK   frontend"
	$(Q)$(MAKE) --no-print-directory frontend-check
	$(LOG_ECHO) "  CHECK   js2300 script runtime"
	$(Q)$(MAKE) --no-print-directory js2300-check TOOLCHAIN=$(TOOLCHAIN) HOSTCC=$(HOSTCC)
	$(LOG_ECHO) "  CHECK   boot logo"
	$(Q)$(MAKE) --no-print-directory boot-logo-check
	@echo "OK"

ci-commit-check: ci-deps ci-toolchain
	$(Q)$(MAKE) quick-check TOOLCHAIN=$(TOOLCHAIN)
	@echo "  CI      sdk"
	$(Q)$(MAKE) --no-print-directory sdk TOOLCHAIN=$(TOOLCHAIN) HOSTCC=$(HOSTCC)
	@echo "  CI      lib"
	$(Q)$(MAKE) --no-print-directory lib TOOLCHAIN=$(TOOLCHAIN) HOSTCC=$(HOSTCC)
	@echo "  CI      js2300-check"
	$(Q)$(MAKE) --no-print-directory js2300-check TOOLCHAIN=$(TOOLCHAIN) HOSTCC=$(HOSTCC)

ci-sd-zip: ci-deps ci-toolchain
	$(Q)$(MAKE) doctor TOOLCHAIN=$(TOOLCHAIN)
	$(Q)$(MAKE) sd-zip TOOLCHAIN=$(TOOLCHAIN) \
		SDZIP=$(CI_SDZIP) \
		ZIP_COMPRESSION=$(CI_ZIP_COMPRESSION)
clean:
	$(Q)rm -rf $(BUILD) $(OUT) $(ASD)

distclean: clean
	$(Q)find . -type f \( -name '*~' -o -name '*.tmp' -o -name '.DS_Store' \) -exec rm -f {} +

DEPFILES := $(shell test ! -d $(BUILD) || find $(BUILD) -type f -name '*.d')
-include $(DEPFILES)
