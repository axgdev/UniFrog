# User-facing path defaults. Override these in untracked config.mk.
TOOLCHAIN ?= /opt/mipsel-mti-elf
TOOLCHAIN_UNAME_M := $(shell uname -m)
TOOLCHAIN_HOST_ARCH := $(if $(filter aarch64 arm64,$(TOOLCHAIN_UNAME_M)),arm64,$(if $(filter x86_64 amd64,$(TOOLCHAIN_UNAME_M)),x86_64,$(TOOLCHAIN_UNAME_M)))
TOOLCHAIN_URL ?= https://github.com/axgdev/frog-toolchain/releases/download/v1.1.1/toolchain-stable-static-$(TOOLCHAIN_HOST_ARCH)-gcc15.2.0-binutils2.45-newlib4.5.0.20241231.tar.xz
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
DEPS ?= .deps
SDK ?= unifrog-hcrtos-sdk
CORES ?= cores
CORE_SOURCE_ROOT ?= $(DEPS)/cores
CORE_SUPPORT_ROOT ?= $(DEPS)/support
JS2300 ?= js2300
FRONTEND ?= frontend
MQUICKJS_DIR ?= $(DEPS)/mquickjs
MQUICKJS_URL ?= https://github.com/bellard/mquickjs.git
MQUICKJS_REF ?= ee50431eac9b14b99f722b537ec4cac0c8dd75ab
JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
PIN_MODE ?= $(if $(MODE),$(MODE),head)

-include config.mk

ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif
ifeq ($(filter --output-sync% -O%,$(MAKEFLAGS)),)
MAKEFLAGS += --output-sync=target
endif

.SUFFIXES:

# Output names and build directories.
APP := unifrog
BUILD := build
OUT := output
TARGET := sf2000
ASD := bisrv.asd
FASTBOOT_ASD := fastboot.asd
FASTBOOT_PAYLOAD ?= full
FRONTEND_PACKAGE := $(OUT)/sdcard/unifrog
CORE_PACKAGE := $(OUT)/sdcard/unifrog/cores
FRONTEND_MANIFEST := $(FRONTEND_PACKAGE)/manifest.ini
SDCARD_PACKAGE_DIR ?= $(OUT)/sdcard-package
SDZIP ?= $(OUT)/UniFrog-sdcard.zip
ZIP_COMPRESSION ?= -9
CI_SDCARD_PACKAGE_DIR ?= $(abspath sdcard-package)
CI_SDZIP ?= $(abspath UniFrog-local-sdcard.zip)
CI_ZIP_COMPRESSION ?= -1
CI_SUBMODULE_JOBS ?= 8
SDCARD_BIOS_DIR := $(SDCARD)/bios
SDCARD_FIRMWARE_DIR := $(SDCARD)/firmware
ASDPACK := $(BUILD)/asdpack
SDCARD ?= /media/mmcblk0
DTS ?= board/hc15xx/common/dts/sf2000_min.dts
DTS_NAME := $(basename $(notdir $(DTS)))
DTS_PRE := $(BUILD)/$(DTS_NAME).dtb.dts.tmp
DTB := $(BUILD)/$(DTS_NAME).dtb
DTB_ASM := $(BUILD)/$(DTS_NAME)_dtb.S
DTB_OBJ := $(BUILD)/$(DTS_NAME)_dtb.o
DTC ?= dtc
EMBED_DTB ?= 1
CCACHE ?=
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
HOSTCFLAGS ?= -O0 -Wall -Wextra
MIPS_ARCH ?= mips32
MIPS_TUNE ?= mips32
ARCH_CFLAGS ?= -march=$(MIPS_ARCH) -mtune=$(MIPS_TUNE)
OPT_SIZE ?= -Os
OPT_FAST ?= -O2
OPT_AUDIO ?= -Os
OPT_FLAGS := -O0 -O1 -O2 -O3 -Os -Og -Ofast

GCC_LIBDIR ?= $(firstword $(wildcard $(TOOLCHAIN)/lib/gcc/mipsel-mti-elf/*))
SYS_LIBDIR := $(TOOLCHAIN)/mipsel-mti-elf/lib
Q := $(if $(V),,@)
UNIFROG_GIT_COMMIT := $(shell git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_GIT_DIRTY := $(shell test -z "$$(git status --porcelain --untracked-files=no 2>/dev/null)" && echo 0 || echo 1)
UNIFROG_SDK_GIT_COMMIT := $(shell git -C $(SDK) rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_CORES_GIT_COMMIT := $(shell git -C $(CORE_SOURCE_ROOT)/libretro-common rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_JS2300_GIT_COMMIT := $(shell git -C $(JS2300) rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_FRONTEND_GIT_COMMIT := $(shell git -C $(FRONTEND) rev-parse --short=12 HEAD 2>/dev/null || echo unknown)

# Treat SDK headers as system headers so third-party/newlib warnings do not
# obscure warnings from the UniFrog source itself.
PROJECT_INCLUDES := -Iinclude -Isrc -I$(CORE_SOURCE_ROOT)/libretro-common/include -I$(JS2300)/include
SDK_INCLUDES := \
	-isystem $(SDK)/include \
	-isystem $(SDK)/include/hcrtos \
	-isystem $(SDK)/include/newlib \
	-isystem $(SDK)/include/vendor

DTS_CPPFLAGS := \
	-Idts/include \
	-I$(SDK)/include/hcrtos \
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

# Defines mirror the original HCRTOS app build environment.
DEFINES := \
	-D_FORTIFY_SOURCE=0 \
	-D__HCRTOS__ \
	-D__DEBUG__ \
	-DNOT_SUPPORT_4K \
	-DSOC_HC15XX \
	-DSF2000 \
	-DSUPPORT_FFPLAYER \
	-DUNIFROG_GIT_COMMIT=\"$(UNIFROG_GIT_COMMIT)\" \
	-DUNIFROG_GIT_DIRTY=$(UNIFROG_GIT_DIRTY) \
	-DUNIFROG_SDK_GIT_COMMIT=\"$(UNIFROG_SDK_GIT_COMMIT)\" \
	-DUNIFROG_CORES_GIT_COMMIT=\"$(UNIFROG_CORES_GIT_COMMIT)\" \
	-DUNIFROG_JS2300_GIT_COMMIT=\"$(UNIFROG_JS2300_GIT_COMMIT)\" \
	-DUNIFROG_FRONTEND_GIT_COMMIT=\"$(UNIFROG_FRONTEND_GIT_COMMIT)\"

CFLAGS := -EL $(ARCH_CFLAGS) $(OPT_SIZE) -pipe -msoft-float -fsigned-char -W \
	-ffunction-sections -fdata-sections -G0 \
	-Wformat=2 \
	-Wno-error=cast-function-type \
	-Wno-error=builtin-declaration-mismatch \
	-Wno-error=format-truncation= \
	-Wno-error=int-conversion \
	$(DEFINES) \
	$(PROJECT_INCLUDES) \
	$(SDK_INCLUDES)
CFLAGS_NOOPT = $(filter-out $(OPT_FLAGS),$(CFLAGS))
CFLAGS_FAST = $(CFLAGS_NOOPT) $(OPT_FAST)
CFLAGS_AUDIO = $(CFLAGS_NOOPT) $(OPT_AUDIO)
CFLAGS_VIDEO = $(CFLAGS_FAST)

ifeq ($(MIPS_ARCH),mips32)
CFLAGS += -DSF2000_HAVE_MIPS_WAIT=1
endif

LIBDIRS := \
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
	--wrap rename

LDFLAGS := -EL $(OPT_SIZE) --static $(LIBDIRS) $(LDSCRIPTS) --gc-sections -n $(WRAPS) --allow-multiple-definition

# Normal archives are pulled as needed by the linker.
LDLIBS := \
	-lavformat \
	-lavcodec \
	-lavutil \
	-lswscale \
	-lge \
	-lviddrv \
	-lz \
	-lkernel \
	-llnx \
	-lpthread \
	-lm \
	-lstdc++ \
	-lsupc++ \
	-lc \
	-lgcc

# Media plugins are registered through archive-level constructors/tables, so
# they must be kept even when there is no direct symbol reference from UniFrog code.
WHOLE_LIBS := \
	-lffplayer \
	-ldsc \
	-lmp3nddec \
	-lmp3 \
	-laacdec \
	-lpcmdec \
	-lflacdec \
	-lflac \
	-lvorbisdec \
	-ltremor \
	-lwmaprodec \
	-lwmapro \
	-lopusdec \
	-lopus \
	-lradec \
	-lra \
	-lauddrv \
	-lauddsp \
	-lviddrv_h264dec \
	-lviddrv_mpeg2dec \
	-lviddrv_vc1dec \
	-lviddrv_vp8dec \
	-lviddrv_mpeg4dec \
	-lviddrv_imagedec \
	-lusbdrv \
	-lusbdrv_hid \
	-lntfs \
	-lmmc \
	-lmmchosthc15 \
	-lefuse

APP_OBJECTS := \
	$(BUILD)/main.o \
	$(BUILD)/frontend/frontend_core.o \
	$(BUILD)/frontend/js2300_frontend.o

SHELL_APP_OBJECTS := \
	$(BUILD)/main_shell.o \
	$(BUILD)/test_frontend/boot_shell.o

BUILD_IDENTITY_STAMP := $(BUILD)/build-identity.stamp
BUILD_IDENTITY_OBJECTS := \
	$(BUILD)/main.o \
	$(BUILD)/main_shell.o \
	$(BUILD)/unifrog_libretro_host.o \
	$(BUILD)/test_frontend/boot_shell.o \
	$(BUILD)/test_frontend/test_frontend.o

# These objects print build identity in device logs. Rebuild them only when the
# embedded identity changes, and let dependent core modules relink from the
# updated libunifrog archive.
$(BUILD_IDENTITY_OBJECTS): $(BUILD_IDENTITY_STAMP)

UNIFROG_OBJECTS := \
	$(BUILD)/unifrog_abi.o \
	$(BUILD)/unifrog_abi_tramp.o \
	$(BUILD)/unifrog_audio.o \
	$(BUILD)/unifrog_av.o \
	$(BUILD)/unifrog_backlight.o \
	$(BUILD)/unifrog_battery.o \
	$(BUILD)/unifrog_boot.o \
	$(BUILD)/unifrog_core_module_loader.o \
	$(BUILD)/unifrog_fb.o \
	$(BUILD)/unifrog_ge.o \
	$(BUILD)/unifrog_gfx.o \
	$(BUILD)/unifrog_input.o \
	$(BUILD)/unifrog_input_wireless.o \
	$(BUILD)/unifrog_libretro_host.o \
	$(BUILD)/unifrog_libretro_tramp.o \
	$(BUILD)/unifrog_log.o \
	$(BUILD)/unifrog_media.o \
	$(BUILD)/unifrog_mips_call.o \
	$(BUILD)/unifrog_panic.o \
	$(BUILD)/unifrog_path.o \
	$(BUILD)/unifrog_platform.o \
	$(BUILD)/unifrog_perf.o \
	$(BUILD)/unifrog_png.o \
	$(BUILD)/unifrog_presenter.o \
	$(BUILD)/unifrog_runtime.o \
	$(BUILD)/unifrog_scpu.o \
	$(BUILD)/unifrog_text.o

LZ4_SRCS := lz4.c lz4frame.c lz4hc.c xxhash.c
LZ4_OBJS := $(addprefix $(BUILD)/third_party/lz4/,$(LZ4_SRCS:.c=.o))
LZ4_CFLAGS := $(CFLAGS_FAST) -w -Isrc/third_party/lz4
ZSTD_DIR := $(CORE_SUPPORT_ROOT)/zstd/lib
ZSTD_DECODER_SRC := $(CORE_SUPPORT_ROOT)/zstd/build/single_file_libs/zstddeclib-in.c
ZSTD_DECODER_OBJ := $(BUILD)/third_party/zstddeclib.o
ZSTD_CFLAGS := $(CFLAGS_FAST) -w \
	-DZSTD_DISABLE_ASM=1 \
	-I$(ZSTD_DIR) \
	-I$(ZSTD_DIR)/common \
	-I$(ZSTD_DIR)/decompress

UNIFROG_OBJECTS += $(LZ4_OBJS) $(ZSTD_DECODER_OBJ)

LIBUNIFROG := $(OUT)/libunifrog.a
LIBJS2300 := $(JS2300)/output/libjs2300.a
LIBRETRO_COMMON_LIB := $(CORES)/output/libretro-common-sf2000.a
SHELL_OUT := $(OUT)/unifrog-shell.out
SHELL_BIN := $(OUT)/unifrog-shell.bin
FASTBOOT_STAGE_OUT := $(OUT)/fastboot-stage1.out
FASTBOOT_STAGE_BIN := $(BUILD)/fastboot/stage1.bin
FASTBOOT_STUB_OUT := $(OUT)/fastboot-stub.out
FASTBOOT_STUB_BIN := $(BUILD)/fastboot/stub.bin
FASTBOOT_STAGE_OBJ := $(BUILD)/fastboot/stage1.o
FASTBOOT_STAGE_ENTRY_OBJ := $(BUILD)/fastboot/stage_entry.o
FASTBOOT_STUB_OBJ := $(BUILD)/fastboot/stub.o
GAMBATTE_CORE_LIB := $(CORES)/output/gambatte_libretro_sf2000.a
GPSP_CORE_LIB := $(CORES)/output/gpsp_libretro_sf2000.a
PICODRIVE_CORE_LIB := $(CORES)/output/picodrive_libretro_sf2000.a
CHD_SUPPORT_CORE_LIB := $(CORES)/output/libchdr-support-sf2000.a
SNES9X2005_CORE_LIB := $(CORES)/output/snes9x2005_libretro_sf2000.a
SNES9X2002_CORE_LIB := $(CORES)/output/snes9x2002_libretro_sf2000.a
QUICKNES_CORE_LIB := $(CORES)/output/quicknes_libretro_sf2000.a
FCEUMM_CORE_LIB := $(CORES)/output/fceumm_libretro_sf2000.a
GEARBOY_CORE_LIB := $(CORES)/output/gearboy_libretro_sf2000.a
PCE_FAST_CORE_LIB := $(CORES)/output/pce_fast_libretro_sf2000.a
QPSX_CORE_LIB := $(CORES)/output/pcsx4all_libretro_sf2000.a
PMP_VIDEO_CORE_LIB := $(CORES)/output/pmp_libretro_sf2000.a
FIRMWARE_LIBRETRO_CORE_LIBS ?=
CORE_REV_STAMP := $(BUILD)/core-sources.rev
CORE_BUILD_DEPS := $(CORES)/Makefile $(CORES)/manifest.mk $(CORE_REV_STAMP)
CORE_MAKE_ARGS := \
	TOOLCHAIN=$(TOOLCHAIN) \
	CROSS_COMPILE=$(CROSS_COMPILE) \
	SDK=$(abspath $(SDK)) \
	CORE_SOURCE_ROOT=$(abspath $(CORE_SOURCE_ROOT)) \
	CORE_SUPPORT_ROOT=$(abspath $(CORE_SUPPORT_ROOT)) \
	CCACHE=$(CCACHE) \
	JOBS=$(JOBS)
CORE_SMOKE_MAKE_FLAGS := --no-print-directory
ifeq ($(V),)
CORE_SMOKE_MAKE_FLAGS += --silent
endif
LIBRETRO_COMMON_BUILD_DEPS := $(CORE_BUILD_DEPS)
CHD_SUPPORT_BUILD_DEPS := $(CORE_BUILD_DEPS)
GAMBATTE_BUILD_DEPS := $(CORE_BUILD_DEPS)
GPSP_BUILD_DEPS := $(CORE_BUILD_DEPS)
PICODRIVE_BUILD_DEPS := $(CORE_BUILD_DEPS)
SNES9X2005_BUILD_DEPS := $(CORE_BUILD_DEPS)
SNES9X2002_BUILD_DEPS := $(CORE_BUILD_DEPS)
QUICKNES_BUILD_DEPS := $(CORE_BUILD_DEPS)
FCEUMM_BUILD_DEPS := $(CORE_BUILD_DEPS)
GEARBOY_BUILD_DEPS := $(CORE_BUILD_DEPS)
PCE_FAST_BUILD_DEPS := $(CORE_BUILD_DEPS)
QPSX_BUILD_DEPS := $(CORE_BUILD_DEPS)
PMP_VIDEO_BUILD_DEPS := $(CORE_BUILD_DEPS)
PACKAGE_LIBRETRO_CORE_LIBS := \
	$(GAMBATTE_CORE_LIB) \
	$(GPSP_CORE_LIB) \
	$(PICODRIVE_CORE_LIB) \
	$(SNES9X2005_CORE_LIB) \
	$(SNES9X2002_CORE_LIB) \
	$(QUICKNES_CORE_LIB) \
	$(FCEUMM_CORE_LIB) \
	$(GEARBOY_CORE_LIB) \
	$(PCE_FAST_CORE_LIB) \
	$(QPSX_CORE_LIB) \
	$(PMP_VIDEO_CORE_LIB)
JS2300_CORE_BIN := $(CORE_PACKAGE)/js2300.bin
GAMBATTE_CORE_BIN := $(CORE_PACKAGE)/gambatte.bin
GPSP_CORE_BIN := $(CORE_PACKAGE)/gpsp.bin
PICODRIVE_CORE_BIN := $(CORE_PACKAGE)/picodrive.bin
SNES9X2005_CORE_BIN := $(CORE_PACKAGE)/snes9x2005.bin
SNES9X2002_CORE_BIN := $(CORE_PACKAGE)/snes9x2002.bin
QUICKNES_CORE_BIN := $(CORE_PACKAGE)/quicknes.bin
FCEUMM_CORE_BIN := $(CORE_PACKAGE)/fceumm.bin
GEARBOY_CORE_BIN := $(CORE_PACKAGE)/gearboy.bin
PCE_FAST_CORE_BIN := $(CORE_PACKAGE)/pce-fast.bin
QPSX_CORE_BIN := $(CORE_PACKAGE)/qpsx.bin
PMP_VIDEO_CORE_BIN := $(CORE_PACKAGE)/pmp-video.bin
LIBRETRO_CORE_BINS := \
	$(GAMBATTE_CORE_BIN) \
	$(GPSP_CORE_BIN) \
	$(PICODRIVE_CORE_BIN) \
	$(SNES9X2005_CORE_BIN) \
	$(SNES9X2002_CORE_BIN) \
	$(QUICKNES_CORE_BIN) \
	$(FCEUMM_CORE_BIN) \
	$(GEARBOY_CORE_BIN) \
	$(PCE_FAST_CORE_BIN) \
	$(QPSX_CORE_BIN) \
	$(PMP_VIDEO_CORE_BIN)
LIBRETRO_CORE_MODULE_OUTS := \
	$(BUILD)/core_modules/gambatte.out \
	$(BUILD)/core_modules/gpsp.out \
	$(BUILD)/core_modules/picodrive.out \
	$(BUILD)/core_modules/snes9x2005.out \
	$(BUILD)/core_modules/snes9x2002.out \
	$(BUILD)/core_modules/quicknes.out \
	$(BUILD)/core_modules/fceumm.out \
	$(BUILD)/core_modules/gearboy.out \
	$(BUILD)/core_modules/pce_fast.out \
	$(BUILD)/core_modules/qpsx.out \
	$(BUILD)/core_modules/pmp_video.out
UNIFROG_CORE_MODULE_BASE ?= 0x83000000
CORE_MODULE_CFLAGS := $(CFLAGS) -mno-abicalls -fno-pic
CORE_MODULE_LDFLAGS := -EL $(OPT_SIZE) --static $(LIBDIRS) \
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
PICODRIVE_LZMA_DIR := $(CORE_SUPPORT_ROOT)/libchdr/deps/lzma-25.01
PICODRIVE_LZMA_SRCS := LzmaDec.c
PICODRIVE_LZMA_OBJS := $(addprefix $(BUILD)/core_modules/picodrive_lzma_,$(PICODRIVE_LZMA_SRCS:.c=.o))
PICODRIVE_LZMA_CFLAGS := \
	-DZ7_ST \
	-Wno-unused \
	-I$(PICODRIVE_LZMA_DIR)/include \
	-I$(PICODRIVE_LZMA_DIR)/src
FASTBOOT_CFLAGS := -EL $(ARCH_CFLAGS) -Os -pipe -msoft-float -fsigned-char -W \
	-ffunction-sections -fdata-sections -G0 \
	-ffreestanding -fno-builtin -fno-pic -mno-abicalls \
	-nostdinc -I$(GCC_LIBDIR)/include

SHELL_LDLIBS := \
	-lge \
	-lviddrv \
	-lviddrv_imagedec \
	-lviddrv_mpeg2dec \
	-lauddrv \
	-lauddsp \
	-ldsc \
	-lusbdrv \
	-lusbdrv_hid \
	-lkernel \
	-llnx \
	-lpthread \
	-lm \
	-lc \
	-lgcc

SHELL_WHOLE_LIBS := \
	-lntfs \
	-lmmc \
	-lmmchosthc15 \
	-lefuse

ifeq ($(EMBED_DTB),1)
APP_OBJECTS += $(DTB_OBJ)
SHELL_APP_OBJECTS += $(DTB_OBJ)
endif

.DELETE_ON_ERROR:
COMMON_TARGETS := all help setup doctor deps deps-status upgrade-pins upgrade-deps repo-check quick-check check verify clean distclean rebuild
SETUP_TARGETS := deps-alpine deps-ubuntu deps-sdk deps-mquickjs deps-support deps-cores
PACKAGE_TARGETS := frontend-package core-package sd-zip install refresh-sd refresh-sd-clean
VERIFY_TARGETS := asdcheck fastboot-check layout-check js2300-check frontend-check core-smoke-check
ADVANCED_TARGETS := sdk dtb lib fastboot size ci-deps ci-toolchain ci-commit-check ci-sd-zip print-config
.PHONY: $(COMMON_TARGETS) $(SETUP_TARGETS) $(PACKAGE_TARGETS) $(VERIFY_TARGETS) $(ADVANCED_TARGETS) FORCE

all: $(ASD) $(OUT)/unifrog.bin core-package
setup: deps
verify: check

help:
	@echo "$(APP) common workflow:"
	@echo "  make setup         Fetch SDK submodule and external source inputs"
	@echo "  make doctor        Check toolchain, SDK, and fetched inputs"
	@echo "  make quick-check   Fast hygiene, core smoke, JS2300, and frontend checks"
	@echo "  make               Build $(ASD), $(OUT)/unifrog.bin, and SD files"
	@echo "  make verify        Build and verify firmware, fastboot, JS, and layout"
	@echo "  make deps          Same as make setup"
	@echo "  make deps-status   Show pinned deps vs latest MODE=head or MODE=tag"
	@echo "  make upgrade-deps  Bump pins and fetch deps, MODE=head or MODE=tag"
	@echo "  make check         Same as make verify"
	@echo ""
	@echo "Setup:"
	@echo "  make deps-alpine   Install Alpine host packages"
	@echo "  make deps-ubuntu   Print Ubuntu host package command"
	@echo "  make deps-sdk      Initialize only the HCRTOS SDK submodule"
	@echo "  make deps-cores    Fetch only libretro core sources"
	@echo ""
	@echo "Packaging and device:"
	@echo "  make sd-zip        Build $(SDZIP)"
	@echo "  make install       Copy firmware and SD files to SDCARD=$(SDCARD)"
	@echo "  make refresh-sd    Build, install, and sync SD files"
	@echo ""
	@echo "Focused checks:"
	@echo "  make repo-check core-smoke-check js2300-check frontend-check"
	@echo "  make layout-check asdcheck"
	@echo "  make -C cores help"
	@echo "  make -C frontend help"
	@echo "  make -C js2300 help"
	@echo ""
	@echo "Cleanup:"
	@echo "  make clean         Remove generated files"
	@echo "  make distclean     Also remove sub-build outputs"
	@echo ""
	@echo "Config:"
	@echo "  make print-config  Show current paths and tools"
	@echo "  make V=1           Show full compiler/linker commands"
	@echo "  Override paths in untracked config.mk, or on the command line."

print-config:
	@echo "TOOLCHAIN=$(TOOLCHAIN)"
	@echo "SDK=$(SDK)"
	@echo "DEPS=$(DEPS)"
	@echo "CORES=$(CORES)"
	@echo "CORE_SOURCE_ROOT=$(CORE_SOURCE_ROOT)"
	@echo "CORE_SUPPORT_ROOT=$(CORE_SUPPORT_ROOT)"
	@echo "JS2300=$(JS2300)"
	@echo "FRONTEND=$(FRONTEND)"
	@echo "MQUICKJS_DIR=$(MQUICKJS_DIR)"
	@echo "HOSTCC=$(HOSTCC)"
	@echo "DTC=$(DTC)"
	@echo "CCACHE=$(if $(CCACHE),$(CCACHE),disabled)"
	@echo "ARCH_CFLAGS=$(ARCH_CFLAGS)"
	@echo "OPT_SIZE=$(OPT_SIZE)"
	@echo "OPT_FAST=$(OPT_FAST)"
	@echo "OPT_AUDIO=$(OPT_AUDIO)"

deps: deps-sdk deps-mquickjs deps-cores

deps-alpine:
	apk add git make dtc tcc tcc-libs-static musl-dev ccache curl tar xz zip patch

deps-ubuntu:
	@echo "sudo apt-get update && sudo apt-get install -y git make device-tree-compiler tcc ccache curl xz-utils zip patch"

deps-sdk:
	git config --global --add safe.directory "$(abspath .)" 2>/dev/null || true
	git submodule sync unifrog-hcrtos-sdk
	git submodule update --init --depth 1 unifrog-hcrtos-sdk

deps-mquickjs:
	@mkdir -p $(DEPS)
	@if test -d "$(MQUICKJS_DIR)/.git"; then \
		echo "  FETCH   $(MQUICKJS_DIR)"; \
		git -C "$(MQUICKJS_DIR)" remote set-url origin "$(MQUICKJS_URL)"; \
	else \
		echo "  CLONE   $(MQUICKJS_URL)"; \
		rm -rf "$(MQUICKJS_DIR)"; \
		git init -q "$(MQUICKJS_DIR)"; \
		git -C "$(MQUICKJS_DIR)" remote add origin "$(MQUICKJS_URL)"; \
	fi; \
	if ! git -C "$(MQUICKJS_DIR)" cat-file -e "$(MQUICKJS_REF)^{commit}" 2>/dev/null; then \
		git -C "$(MQUICKJS_DIR)" fetch --depth 1 --filter=blob:none origin "$(MQUICKJS_REF)"; \
	fi; \
	git -C "$(MQUICKJS_DIR)" checkout -q "$(MQUICKJS_REF)"; \
	git -C "$(MQUICKJS_DIR)" reset --hard -q "$(MQUICKJS_REF)"; \
	git -C "$(MQUICKJS_DIR)" clean -fdx -q

deps-status:
	@set -e; \
	mode="$(PIN_MODE)"; \
	case "$$mode" in head|tag) ;; *) echo "MODE must be head or tag"; exit 1;; esac; \
	resolve_ref() { \
		url="$$1"; mode="$$2"; \
		if test "$$mode" = tag; then \
			tag=$$(git ls-remote --tags --sort='version:refname' "$$url" 'refs/tags/v[0-9]*' 'refs/tags/[0-9]*' 2>/dev/null | \
				awk '$$2 !~ /\^\{\}$$/ && $$2 ~ /^refs\/tags\/v?[0-9]+([.][0-9]+)*$$/ { sub("refs/tags/", "", $$2); tag=$$2 } END { print tag }'); \
			if test -n "$$tag"; then \
				ref=$$(git ls-remote --tags "$$url" "refs/tags/$$tag" "refs/tags/$$tag^{}" | \
					awk '$$2 ~ /\^\{\}$$/ { peeled=$$1 } $$2 !~ /\^\{\}$$/ { direct=$$1 } END { print peeled ? peeled : direct }'); \
				printf '%s tag %s\n' "$$ref" "$$tag"; \
				return; \
			fi; \
		fi; \
		branch=$$(git ls-remote --symref "$$url" HEAD | awk '/^ref:/ { sub("refs/heads/", "", $$2); print $$2; exit }'); \
		ref=$$(git ls-remote "$$url" HEAD | awk '/^[0-9a-f]/ { print $$1; exit }'); \
		printf '%s head %s\n' "$$ref" "$${branch:-HEAD}"; \
	}; \
	set -- $$(resolve_ref "$(MQUICKJS_URL)" "$$mode"); \
	printf '%-16s pinned=%s latest=%s source=%s:%s\n' mquickjs "$(MQUICKJS_REF)" "$$1" "$$2" "$$3"
	$(Q)$(MAKE) -C $(CORES) pin-status PIN_MODE=$(PIN_MODE) \
		CORE_SOURCE_ROOT=$(abspath $(CORE_SOURCE_ROOT)) \
		CORE_SUPPORT_ROOT=$(abspath $(CORE_SUPPORT_ROOT))

upgrade-pins:
	@set -e; \
	mode="$(PIN_MODE)"; \
	case "$$mode" in head|tag) ;; *) echo "MODE must be head or tag"; exit 1;; esac; \
	resolve_ref() { \
		url="$$1"; mode="$$2"; \
		if test "$$mode" = tag; then \
			tag=$$(git ls-remote --tags --sort='version:refname' "$$url" 'refs/tags/v[0-9]*' 'refs/tags/[0-9]*' 2>/dev/null | \
				awk '$$2 !~ /\^\{\}$$/ && $$2 ~ /^refs\/tags\/v?[0-9]+([.][0-9]+)*$$/ { sub("refs/tags/", "", $$2); tag=$$2 } END { print tag }'); \
			if test -n "$$tag"; then \
				ref=$$(git ls-remote --tags "$$url" "refs/tags/$$tag" "refs/tags/$$tag^{}" | \
					awk '$$2 ~ /\^\{\}$$/ { peeled=$$1 } $$2 !~ /\^\{\}$$/ { direct=$$1 } END { print peeled ? peeled : direct }'); \
				printf '%s tag %s\n' "$$ref" "$$tag"; \
				return; \
			fi; \
		fi; \
		branch=$$(git ls-remote --symref "$$url" HEAD | awk '/^ref:/ { sub("refs/heads/", "", $$2); print $$2; exit }'); \
		ref=$$(git ls-remote "$$url" HEAD | awk '/^[0-9a-f]/ { print $$1; exit }'); \
		printf '%s head %s\n' "$$ref" "$${branch:-HEAD}"; \
	}; \
	set -- $$(resolve_ref "$(MQUICKJS_URL)" "$$mode"); \
	new=$$1; kind=$$2; label=$$3; old="$(MQUICKJS_REF)"; \
	if test -z "$$new"; then echo "mquickjs: unable to resolve latest $$mode"; exit 1; fi; \
	if test "$$new" != "$$old"; then \
		sed -i.bak "s|^MQUICKJS_REF ?= .*|MQUICKJS_REF ?= $$new|" Makefile; \
		rm -f Makefile.bak; \
		echo "  PIN     mquickjs $$old -> $$new ($$kind $$label)"; \
	else \
		echo "  PIN     mquickjs already $$old ($$kind $$label)"; \
	fi
	$(Q)$(MAKE) -C $(CORES) upgrade-pins PIN_MODE=$(PIN_MODE) \
		CORE_SOURCE_ROOT=$(abspath $(CORE_SOURCE_ROOT)) \
		CORE_SUPPORT_ROOT=$(abspath $(CORE_SUPPORT_ROOT))

upgrade-deps: upgrade-pins
	$(Q)$(MAKE) --no-print-directory deps PIN_MODE=$(PIN_MODE)

deps-support:
	$(Q)$(MAKE) -C $(CORES) support-init $(CORE_MAKE_ARGS)

deps-cores:
	$(Q)$(MAKE) -C $(CORES) init $(CORE_MAKE_ARGS)

doctor:
	@echo "Toolchain: $(TOOLCHAIN)"
	@command -v $(CC) >/dev/null || { echo "missing: $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null || { echo "missing: $(LD)"; exit 1; }
	@command -v $(AR) >/dev/null || { echo "missing: $(AR)"; exit 1; }
	@command -v $(NM) >/dev/null || { echo "missing: $(NM)"; exit 1; }
	@command -v $(READELF) >/dev/null || { echo "missing: $(READELF)"; exit 1; }
	@command -v $(OBJCOPY) >/dev/null || { echo "missing: $(OBJCOPY)"; exit 1; }
	@command -v $(HOSTCC) >/dev/null || { echo "missing: $(HOSTCC)"; exit 1; }
	@command -v $(DTC) >/dev/null || { echo "missing: $(DTC)"; exit 1; }
	@test -n "$(GCC_LIBDIR)" || { echo "missing: GCC libdir under $(TOOLCHAIN)/lib/gcc/mipsel-mti-elf"; exit 1; }
	@test -d "$(SYS_LIBDIR)" || { echo "missing: $(SYS_LIBDIR)"; exit 1; }
	@test -d "$(SDK)/include" || { echo "missing: $(SDK)/include"; exit 1; }
	@test -d "$(SDK)/lib" || { echo "missing: $(SDK)/lib"; exit 1; }
	@test -f "$(SDK)/Makefile" || { echo "missing SDK checkout: $(SDK)"; exit 1; }
	@test -f "$(SDK)/lib/core/libm.a" || { echo "missing: $(SDK)/lib/core/libm.a"; exit 1; }
	@test -f "$(DTS)" || { echo "missing: $(DTS)"; exit 1; }
	@test -f "$(CORES)/Makefile" || { echo "missing: $(CORES)/Makefile"; exit 1; }
	@test -e "$(CORE_SOURCE_ROOT)/libretro-common/.git" || { echo "missing core checkout; run: make deps-cores"; exit 1; }
	@test -f "$(CORE_SUPPORT_ROOT)/zstd/build/single_file_libs/zstddeclib-in.c" || { echo "missing core support checkout; run: make deps"; exit 1; }
	@test -f "$(CORE_SUPPORT_ROOT)/zlib/inflate.c" || { echo "missing core support checkout; run: make deps"; exit 1; }
	@test -f "$(CORE_SUPPORT_ROOT)/libchdr/src/libchdr_chd.c" || { echo "missing core support checkout; run: make deps"; exit 1; }
	@test -f "$(JS2300)/Makefile" || { echo "missing JS2300 source: $(JS2300)"; exit 1; }
	@test -f "$(FRONTEND)/Makefile" || { echo "missing frontend source: $(FRONTEND)"; exit 1; }
	@test -f "$(MQUICKJS_DIR)/mquickjs.c" || { echo "missing MQuickJS checkout: $(MQUICKJS_DIR)"; exit 1; }
	@echo "OK"
	@echo "Run 'make print-config' to show resolved paths and tools."

repo-check:
	@echo "  CHECK   repository hygiene"
	$(Q)git diff --check
	$(Q)git diff --cached --check
	$(Q)! git ls-files | grep -E '(^build/|^output/|^\.deps/|^cores/build/|^cores/output/|^frontend/output/|^js2300/build/|^js2300/output/|~$$|\.tmp$$|\.DS_Store$$|\.o$$|\.d$$|\.out$$|\.map$$|\.bin$$|\.dtb$$|\.dts\.tmp$$|\.pre\.tmp$$)' >/dev/null || { \
		echo "tracked generated file found"; \
		git ls-files | grep -E '(^build/|^output/|^\.deps/|^cores/build/|^cores/output/|^frontend/output/|^js2300/build/|^js2300/output/|~$$|\.tmp$$|\.DS_Store$$|\.o$$|\.d$$|\.out$$|\.map$$|\.bin$$|\.dtb$$|\.dts\.tmp$$|\.pre\.tmp$$)'; \
		exit 1; \
	}
	@echo "OK"

quick-check:
	$(Q)$(MAKE) --no-print-directory repo-check
	$(Q)$(MAKE) --no-print-directory doctor
	@echo "  CHECK   core smoke"
	$(Q)$(MAKE) --no-print-directory core-smoke-check
	@echo "  CHECK   js2300"
	$(Q)$(MAKE) --no-print-directory js2300-check
	@echo "  CHECK   frontend"
	$(Q)$(MAKE) --no-print-directory frontend-check
	@echo "OK"

core-smoke-check:
	$(Q)$(MAKE) $(CORE_SMOKE_MAKE_FLAGS) -C $(CORES) smoke-check $(CORE_MAKE_ARGS)

sdk:
	$(Q)$(MAKE) -C $(SDK) check TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS)

js2300-check:
	$(Q)$(MAKE) -C $(JS2300) check TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) MQUICKJS_DIR=$(abspath $(MQUICKJS_DIR)) \
		HOSTCC=$(HOSTCC)

frontend-check:
	$(Q)$(MAKE) -C $(FRONTEND) check MQUICKJS_DIR=$(abspath $(MQUICKJS_DIR)) \
		HOSTCC=$(HOSTCC)

frontend-package:
	$(Q)$(MAKE) -C $(FRONTEND) package OUT=$(abspath $(OUT))/frontend \
		MQUICKJS_DIR=$(abspath $(MQUICKJS_DIR)) HOSTCC=$(HOSTCC)
	$(Q)mkdir -p $(FRONTEND_PACKAGE)
	$(Q)rm -rf $(FRONTEND_PACKAGE)/app $(FRONTEND_PACKAGE)/themes \
		$(FRONTEND_PACKAGE)/scripts \
		$(FRONTEND_PACKAGE)/main.js \
		$(FRONTEND_MANIFEST)
	$(Q)cp -R $(OUT)/frontend/unifrog-frontend/. $(FRONTEND_PACKAGE)/
	$(Q){ \
		printf '%s\n' 'manifest_version=1'; \
		printf '%s\n' 'layout=sf2000-sd-v1'; \
		printf '%s\n' 'firmware_commit=$(UNIFROG_GIT_COMMIT)'; \
		printf '%s\n' 'firmware_dirty=$(UNIFROG_GIT_DIRTY)'; \
		printf '%s\n' 'sdk_commit=$(UNIFROG_SDK_GIT_COMMIT)'; \
		printf '%s\n' 'cores_commit=$(UNIFROG_CORES_GIT_COMMIT)'; \
		printf '%s\n' 'js2300_commit=$(UNIFROG_JS2300_GIT_COMMIT)'; \
		printf '%s\n' 'frontend_commit=$(UNIFROG_FRONTEND_GIT_COMMIT)'; \
		printf '%s\n' "generated_utc=$$(date -u +%Y-%m-%dT%H:%M:%SZ)"; \
	} > $(FRONTEND_MANIFEST)

core-package: $(JS2300_CORE_BIN) $(LIBRETRO_CORE_BINS)
	@echo "  COREBIN $(CORE_PACKAGE)"

$(CORE_PACKAGE): frontend-package
	$(Q)mkdir -p $@

$(JS2300_CORE_BIN): $(LIBJS2300) | $(CORE_PACKAGE)
	@echo "  COREBIN $@"
	$(Q)cp $(LIBJS2300) $(JS2300_CORE_BIN)

$(BUILD)/core_modules/gambatte_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"gambatte\" -DUNIFROG_MODULE_EXTENSIONS=\"gb\|gbc\"
$(BUILD)/core_modules/gpsp_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"gpsp\" -DUNIFROG_MODULE_EXTENSIONS=\"gba\" -DUNIFROG_MODULE_SYMBOL_PREFIX=gpsp
PICODRIVE_CORE_SUPPORT_LIBS := $(CHD_SUPPORT_CORE_LIB)
PCE_FAST_CORE_SUPPORT_LIBS := $(CHD_SUPPORT_CORE_LIB)

$(BUILD)/core_modules/picodrive_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"picodrive\" -DUNIFROG_MODULE_EXTENSIONS=\"md\|gen\|smd\|sms\|gg\|sg\|32x\|cue\|chd\|iso\" -DUNIFROG_MODULE_SYMBOL_PREFIX=picodrive
$(BUILD)/core_modules/snes9x2005_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"snes9x2005\" -DUNIFROG_MODULE_EXTENSIONS=\"sfc\|smc\" -DUNIFROG_MODULE_SYMBOL_PREFIX=snes9x2005
$(BUILD)/core_modules/snes9x2002_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"snes9x2002\" -DUNIFROG_MODULE_EXTENSIONS=\"sfc\|smc\" -DUNIFROG_MODULE_SYMBOL_PREFIX=snes9x2002
$(BUILD)/core_modules/quicknes_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"quicknes\" -DUNIFROG_MODULE_EXTENSIONS=\"nes\" -DUNIFROG_MODULE_SYMBOL_PREFIX=quicknes
$(BUILD)/core_modules/fceumm_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"fceumm\" -DUNIFROG_MODULE_EXTENSIONS=\"nes\|fds\" -DUNIFROG_MODULE_SYMBOL_PREFIX=fceumm
$(BUILD)/core_modules/gearboy_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"gearboy\" -DUNIFROG_MODULE_EXTENSIONS=\"gb\|gbc\" -DUNIFROG_MODULE_SYMBOL_PREFIX=gearboy
$(BUILD)/core_modules/pce_fast_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"pce-fast\" -DUNIFROG_MODULE_EXTENSIONS=\"pce\|sgx\|cue\|chd\" -DUNIFROG_MODULE_SYMBOL_PREFIX=pce_fast
$(BUILD)/core_modules/qpsx_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"qpsx\" -DUNIFROG_MODULE_EXTENSIONS=\"bin\|iso\|img\|cue\|pbp\" -DUNIFROG_MODULE_SYMBOL_PREFIX=qpsx
$(BUILD)/core_modules/pmp_video_entry.o: CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"pmp-video\" -DUNIFROG_MODULE_EXTENSIONS=\"avi\" -DUNIFROG_MODULE_SYMBOL_PREFIX=pmp_video

$(BUILD)/core_modules/%_entry.o: src/unifrog_core_module_entry.c include/unifrog/core_module.h | $(BUILD)
	@echo "  CC      $< ($*)"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) $(CORE_MODULE_DEFINES) -MD -MP -c $< -o $@

$(BUILD)/core_modules/support.o: src/unifrog_core_module_support.c include/unifrog/abi.h | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/core_modules/picodrive_lzma_%.o: $(PICODRIVE_LZMA_DIR)/src/%.c | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) $(PICODRIVE_LZMA_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/third_party/lz4/%.o: src/third_party/lz4/%.c | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(LZ4_CFLAGS) -MD -MP -c $< -o $@

$(ZSTD_DECODER_OBJ): $(ZSTD_DECODER_SRC) | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(ZSTD_CFLAGS) -MD -MP -c $< -o $@

define CORE_MODULE_RULES
$(BUILD)/core_modules/$(1).out: $(BUILD)/core_modules/$(1)_entry.o $(BUILD)/core_modules/support.o $$($(2)_CORE_LIB) $$($(2)_CORE_SUPPORT_LIBS) $(LIBRETRO_COMMON_LIB) $(LIBUNIFROG) linker/core-module.ld linker/hc15xx/peripherals.ld | $(OUT)
	@echo "  LD      $$@"
	$(Q)$(LD) $(CORE_MODULE_LDFLAGS) -o $$@ -Map $$@.map -u unifrog_core_module_entry --start-group $$(filter %.o %.a,$$^) $(CORE_MODULE_LDLIBS) --end-group

$(3): $(BUILD)/core_modules/$(1).out | $(CORE_PACKAGE)
	@echo "  OBJCOPY $$@"
	$(Q)$(OBJCOPY) -O binary $$< $$@
endef

$(eval $(call CORE_MODULE_RULES,gambatte,GAMBATTE,$(GAMBATTE_CORE_BIN)))
$(eval $(call CORE_MODULE_RULES,gpsp,GPSP,$(GPSP_CORE_BIN)))
$(eval $(call CORE_MODULE_RULES,picodrive,PICODRIVE,$(PICODRIVE_CORE_BIN)))
$(eval $(call CORE_MODULE_RULES,snes9x2005,SNES9X2005,$(SNES9X2005_CORE_BIN)))
$(eval $(call CORE_MODULE_RULES,snes9x2002,SNES9X2002,$(SNES9X2002_CORE_BIN)))
$(eval $(call CORE_MODULE_RULES,quicknes,QUICKNES,$(QUICKNES_CORE_BIN)))
$(eval $(call CORE_MODULE_RULES,fceumm,FCEUMM,$(FCEUMM_CORE_BIN)))
$(eval $(call CORE_MODULE_RULES,gearboy,GEARBOY,$(GEARBOY_CORE_BIN)))
$(eval $(call CORE_MODULE_RULES,pce_fast,PCE_FAST,$(PCE_FAST_CORE_BIN)))
$(eval $(call CORE_MODULE_RULES,qpsx,QPSX,$(QPSX_CORE_BIN)))
$(eval $(call CORE_MODULE_RULES,pmp_video,PMP_VIDEO,$(PMP_VIDEO_CORE_BIN)))

$(BUILD) $(OUT):
	$(Q)mkdir -p $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/unifrog_fb.o $(BUILD)/unifrog_ge.o $(BUILD)/unifrog_presenter.o: CFLAGS := $(CFLAGS_VIDEO)
$(BUILD)/unifrog_gfx.o $(BUILD)/unifrog_perf.o $(BUILD)/unifrog_scpu.o: CFLAGS := $(CFLAGS_FAST)
$(BUILD)/unifrog_audio.o: CFLAGS := $(CFLAGS_AUDIO)
$(BUILD)/unifrog_libretro_host.o: CFLAGS := $(CFLAGS_AUDIO) -I$(ZSTD_DIR)

$(BUILD)/%.o: src/%.S | $(BUILD)
	@echo "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -D__ASSEMBLY__ -MD -MP -c $< -o $@

$(BUILD)/main_shell.o: src/main.c | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -DUNIFROG_BOOT_SHELL=1 -MD -MP -c $< -o $@

$(BUILD)/test_frontend/test_frontend.o: src/test_frontend/test_frontend.c $(BUILD_IDENTITY_STAMP) | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(FASTBOOT_STAGE_OBJ): src/fastboot/stage1.c | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(FASTBOOT_CFLAGS) -MD -MP -c $< -o $@

$(FASTBOOT_STAGE_ENTRY_OBJ): src/fastboot/stage_entry.S | $(BUILD)
	@echo "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(FASTBOOT_CFLAGS) -D__ASSEMBLY__ -MD -MP -c $< -o $@

$(FASTBOOT_STAGE_OUT): $(FASTBOOT_STAGE_ENTRY_OBJ) $(FASTBOOT_STAGE_OBJ) linker/fastboot/stage.ld | $(OUT)
	@echo "  LD      $@"
	$(Q)$(LD) -EL --static -n --gc-sections -T linker/fastboot/stage.ld -Map $@.map -o $@ $(filter %.o,$^)

$(FASTBOOT_STAGE_BIN): $(FASTBOOT_STAGE_OUT) | $(BUILD)
	@echo "  OBJCOPY $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(OBJCOPY) -O binary $< $@

$(FASTBOOT_STUB_OBJ): src/fastboot/stub.S $(FASTBOOT_STAGE_BIN) | $(BUILD)
	@echo "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(FASTBOOT_CFLAGS) -D__ASSEMBLY__ -MD -MP -c $< -o $@

$(FASTBOOT_STUB_OUT): $(FASTBOOT_STUB_OBJ) linker/fastboot/stub.ld | $(OUT)
	@echo "  LD      $@"
	$(Q)$(LD) -EL --static -n --gc-sections -T linker/fastboot/stub.ld -Map $@.map -o $@ $(filter %.o,$^)

$(FASTBOOT_STUB_BIN): $(FASTBOOT_STUB_OUT)
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(DTS_PRE): $(DTS) | $(BUILD)
	@echo "  CPP     $<"
	$(Q)$(CC) $(DTS_CPPFLAGS) -Wp,-MD,$@.d -E -o $@ $<

$(DTB): $(DTS_PRE)
	@echo "  DTC     $@"
	$(Q)$(DTC) -O dtb -o $@ -b 0 $(DTCFLAGS) -d $@.d $<

$(DTB_ASM): $(DTB) | $(BUILD)
	@echo "  GEN     $@"
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
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(LIBUNIFROG): $(UNIFROG_OBJECTS) | $(OUT)
	@echo "  AR      $@"
	$(Q)$(AR) rcs $@ $^

$(OUT)/$(TARGET).out: $(APP_OBJECTS) $(LIBUNIFROG) $(LIBJS2300) $(LIBRETRO_COMMON_LIB) $(FIRMWARE_LIBRETRO_CORE_LIBS) | $(OUT) sdk
	@echo "  LD      $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ -Map $@.map --start-group $^ $(LDLIBS) --whole-archive $(WHOLE_LIBS) --no-whole-archive --end-group

$(LIBJS2300): FORCE
	@echo "  JS2300  runtime"
	$(Q)$(MAKE) -C $(JS2300) TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) MQUICKJS_DIR=$(abspath $(MQUICKJS_DIR))

$(CORE_REV_STAMP): FORCE | $(BUILD)
	$(Q)value=$$(for dir in $(CORE_SOURCE_ROOT)/*; do \
		test -d "$$dir/.git" || continue; \
		name=$$(basename "$$dir"); \
		rev=$$(git -C "$$dir" rev-parse --short=12 HEAD 2>/dev/null || echo unknown); \
		dirty=$$(git -C "$$dir" status --porcelain --untracked-files=no 2>/dev/null | cksum | awk '{print $$1}'); \
		printf '%s:%s:%s ' "$$name" "$$rev" "$$dirty"; \
	done); \
	old=$$(cat $@ 2>/dev/null || true); \
	if test "$$value" != "$$old"; then printf '%s\n' "$$value" > $@; fi

$(BUILD_IDENTITY_STAMP): FORCE | $(BUILD)
	$(Q)value="$(UNIFROG_GIT_COMMIT) $(UNIFROG_GIT_DIRTY) $(UNIFROG_SDK_GIT_COMMIT) $(UNIFROG_CORES_GIT_COMMIT) $(UNIFROG_JS2300_GIT_COMMIT) $(UNIFROG_FRONTEND_GIT_COMMIT)"; \
	old=$$(cat $@ 2>/dev/null || true); \
	if test "$$value" != "$$old"; then printf '%s\n' "$$value" > $@; fi

$(GAMBATTE_CORE_LIB): $(GAMBATTE_BUILD_DEPS)
	@echo "  CORE    gambatte"
	$(Q)$(MAKE) -C $(CORES) gambatte $(CORE_MAKE_ARGS)

$(GPSP_CORE_LIB): $(GPSP_BUILD_DEPS)
	@echo "  CORE    gpsp"
	$(Q)$(MAKE) -C $(CORES) gpsp $(CORE_MAKE_ARGS)

$(PICODRIVE_CORE_LIB): $(PICODRIVE_BUILD_DEPS) $(CHD_SUPPORT_CORE_LIB)
	@echo "  CORE    picodrive"
	$(Q)$(MAKE) -C $(CORES) picodrive $(CORE_MAKE_ARGS)

$(SNES9X2005_CORE_LIB): $(SNES9X2005_BUILD_DEPS)
	@echo "  CORE    snes9x2005"
	$(Q)$(MAKE) -C $(CORES) snes9x2005 $(CORE_MAKE_ARGS)

$(SNES9X2002_CORE_LIB): $(SNES9X2002_BUILD_DEPS)
	@echo "  CORE    snes9x2002"
	$(Q)$(MAKE) -C $(CORES) snes9x2002 $(CORE_MAKE_ARGS)

$(QUICKNES_CORE_LIB): $(QUICKNES_BUILD_DEPS)
	@echo "  CORE    quicknes"
	$(Q)$(MAKE) -C $(CORES) quicknes $(CORE_MAKE_ARGS)

$(FCEUMM_CORE_LIB): $(FCEUMM_BUILD_DEPS)
	@echo "  CORE    fceumm"
	$(Q)$(MAKE) -C $(CORES) fceumm $(CORE_MAKE_ARGS)

$(GEARBOY_CORE_LIB): $(GEARBOY_BUILD_DEPS)
	@echo "  CORE    gearboy"
	$(Q)$(MAKE) -C $(CORES) gearboy $(CORE_MAKE_ARGS)

$(PCE_FAST_CORE_LIB): $(PCE_FAST_BUILD_DEPS) $(CHD_SUPPORT_CORE_LIB)
	@echo "  CORE    pce-fast"
	$(Q)$(MAKE) -C $(CORES) pce-fast $(CORE_MAKE_ARGS)

$(QPSX_CORE_LIB): $(QPSX_BUILD_DEPS)
	@echo "  CORE    qpsx"
	$(Q)$(MAKE) -C $(CORES) qpsx $(CORE_MAKE_ARGS)

$(PMP_VIDEO_CORE_LIB): $(PMP_VIDEO_BUILD_DEPS)
	@echo "  CORE    pmp-video"
	$(Q)$(MAKE) -C $(CORES) pmp-video $(CORE_MAKE_ARGS)

$(LIBRETRO_COMMON_LIB): $(LIBRETRO_COMMON_BUILD_DEPS)
	@echo "  CORELIB libretro-common"
	$(Q)$(MAKE) -C $(CORES) libretro-common $(CORE_MAKE_ARGS)

$(CHD_SUPPORT_CORE_LIB): $(CHD_SUPPORT_BUILD_DEPS)
	@echo "  CORELIB libchdr-support"
	$(Q)$(MAKE) -C $(CORES) chd-support $(CORE_MAKE_ARGS)

$(OUT)/$(TARGET).bin: $(OUT)/$(TARGET).out
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(SHELL_OUT): $(SHELL_APP_OBJECTS) $(LIBUNIFROG) | $(OUT) sdk
	@echo "  LD      $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ -Map $@.map --start-group $^ $(SHELL_LDLIBS) --whole-archive $(SHELL_WHOLE_LIBS) --no-whole-archive --end-group

$(SHELL_BIN): $(SHELL_OUT)
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(ASDPACK): tools/asdpack.c | $(BUILD)
	@echo "  HOSTCC  $<"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) $< -o $@

$(ASD): $(OUT)/$(TARGET).bin $(ASDPACK)
	@echo "  PACK    $@"
	$(Q)$(ASDPACK) $< $@

ifeq ($(FASTBOOT_PAYLOAD),full)
$(OUT)/unifrog.bin: $(OUT)/$(TARGET).bin FORCE | $(OUT)
	@echo "  COPY    $@"
	$(Q)cp $< $@
else ifeq ($(FASTBOOT_PAYLOAD),shell)
$(OUT)/unifrog.bin: $(SHELL_BIN) FORCE | $(OUT)
	@echo "  COPY    $@"
	$(Q)cp $< $@
else
$(error FASTBOOT_PAYLOAD must be shell or full)
endif

FORCE:

$(FASTBOOT_ASD): $(FASTBOOT_STUB_BIN) $(ASDPACK)
	@echo "  PACK    $@"
	$(Q)$(ASDPACK) $< $@

dtb: $(DTB)

lib: $(LIBUNIFROG)

asdcheck: $(ASD)
	@echo "  CHECK   $(ASD)"
	$(Q)$(ASDPACK) --check $(ASD)

fastboot: $(FASTBOOT_ASD) $(OUT)/unifrog.bin

fastboot-check: fastboot core-package
	@test -s $(FASTBOOT_ASD)
	@test -s $(OUT)/unifrog.bin
	@test -s $(FRONTEND_PACKAGE)/main.js
	@test -s $(FRONTEND_MANIFEST)
	@test -s $(JS2300_CORE_BIN)
	@for bin in $(LIBRETRO_CORE_BINS); do test -s $$bin || exit 1; done
	@echo "  CHECK   $(FASTBOOT_ASD)"
	$(Q)$(ASDPACK) --check $(FASTBOOT_ASD)
	@echo "  OK      $(FASTBOOT_ASD)"

ifeq ($(FASTBOOT_PAYLOAD),full)
fastboot-check: layout-check
endif

layout-check: $(OUT)/$(TARGET).out $(LIBRETRO_CORE_MODULE_OUTS)
	@echo "  CHECK   link layout"
	$(Q)for image in $^; do \
		ebss_hex=$$($(NM) -n $$image | awk '$$3 == "_ebss" { print $$1; exit }'); \
		if [ -z "$$ebss_hex" ]; then echo "$$image: missing _ebss symbol"; exit 1; fi; \
		$(READELF) -SW $$image | awk -v image="$$image" -v ebss_hex="$$ebss_hex" ' \
	function h2d(s, i, c, n, v) { \
		sub(/^0x/, "", s); \
		n = 0; \
		for (i = 1; i <= length(s); i++) { \
			c = tolower(substr(s, i, 1)); \
			v = index("0123456789abcdef", c) - 1; \
			if (v < 0) { return -1 } \
			n = n * 16 + v; \
		} \
		return n; \
	} \
	BEGIN { ebss = h2d(ebss_hex); bad = 0 } \
	$$2 ~ /^\./ && $$3 == "NOBITS" && index($$8, "A") { \
		start = h2d($$4); \
		size = h2d($$6); \
		end = start + size; \
		printf("  LAYOUT  %-18s 0x%08x..0x%08x flags=%s\n", $$2, start, end, $$8); \
		if (end > ebss) { \
			printf("%s: %s ends at 0x%08x after _ebss=0x%08x\n", image, $$2, end, ebss) > "/dev/stderr"; \
			bad = 1; \
		} \
	} \
	END { \
		if (bad) { exit 1 } \
		printf("  OK      %s _ebss=0x%08x covers alloc NOBITS reservations\n", image, ebss); \
	}' || exit 1; \
	done

check: $(ASD) $(OUT)/unifrog.bin core-package layout-check
	@test -s $(ASD)
	@test -s $(OUT)/unifrog.bin
	@test -s $(LIBUNIFROG)
	@test -s $(FRONTEND_PACKAGE)/main.js
	@test -s $(FRONTEND_MANIFEST)
	@test -s $(JS2300_CORE_BIN)
	@for bin in $(LIBRETRO_CORE_BINS); do test -s $$bin || exit 1; done
	@echo "  CHECK   $(ASD)"
	$(Q)$(ASDPACK) --check $(ASD)
	@echo "  CHECK   $(OUT)/unifrog.bin"
	$(Q)test -s $(OUT)/unifrog.bin
	@echo "  OK      $(ASD)"

size: $(ASD)
	@ls -lh $(ASD) $(OUT)/$(TARGET).bin $(OUT)/$(TARGET).out $(LIBUNIFROG) $(DTB)

install: fastboot-check layout-check
	@echo "  CLEAN   stale root firmware files"
	$(Q)rm -f $(SDCARD)/$(ASD) $(SDCARD)/$(FASTBOOT_ASD) $(SDCARD)/unifrog.bin
	@echo "  INSTALL $(SDCARD_BIOS_DIR)/bisrv.asd"
	$(Q)mkdir -p $(SDCARD_BIOS_DIR)
	$(Q)cp $(FASTBOOT_ASD) $(SDCARD_BIOS_DIR)/bisrv.asd
	@echo "  INSTALL $(SDCARD_FIRMWARE_DIR)/unifrog.bin"
	$(Q)mkdir -p $(SDCARD_FIRMWARE_DIR)
	$(Q)cp $(OUT)/unifrog.bin $(SDCARD_FIRMWARE_DIR)/unifrog.bin
	@echo "  INSTALL $(SDCARD)/unifrog"
	$(Q)rm -rf $(SDCARD)/unifrog
	$(Q)cp -R $(FRONTEND_PACKAGE) $(SDCARD)/unifrog
	$(Q)sync

rebuild:
	$(Q)$(MAKE) clean
	$(Q)$(MAKE)

refresh-sd:
	@echo "  REFRESH incremental SDCARD=$(SDCARD)"
	$(Q)$(MAKE) install SDCARD=$(SDCARD)

refresh-sd-clean:
	$(Q)$(MAKE) clean
	$(Q)$(MAKE) -C $(SDK) clean TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS)
	$(Q)$(MAKE) -C $(CORES) clean $(CORE_MAKE_ARGS)
	$(Q)$(MAKE) -C $(JS2300) clean
	$(Q)$(MAKE) -C $(FRONTEND) clean
	$(Q)$(MAKE) install SDCARD=$(SDCARD)

sd-zip:
	@command -v zip >/dev/null || { echo "missing: zip"; exit 1; }
	$(Q)rm -rf $(SDCARD_PACKAGE_DIR)
	$(Q)$(MAKE) install SDCARD=$(abspath $(SDCARD_PACKAGE_DIR))
	@echo "  ZIP     $(SDZIP)"
	$(Q)mkdir -p $(dir $(SDZIP))
	$(Q)rm -f $(SDZIP)
	$(Q)cd $(SDCARD_PACKAGE_DIR) && zip $(ZIP_COMPRESSION) -r $(abspath $(SDZIP)) . >/dev/null

ci-deps: deps

ci-toolchain:
	@if test ! -x "$(TOOLCHAIN)/bin/mipsel-mti-elf-gcc"; then \
		command -v curl >/dev/null || { echo "missing: curl"; exit 1; }; \
		echo "  FETCH   $(TOOLCHAIN_URL)"; \
		rm -rf toolchain /tmp/frog-toolchain; \
		mkdir -p /tmp/frog-toolchain; \
		curl -L "$(TOOLCHAIN_URL)" -o /tmp/frog-toolchain.tar.xz; \
		tar -C /tmp/frog-toolchain -xf /tmp/frog-toolchain.tar.xz; \
		found=$$(find /tmp/frog-toolchain -path '*/bin/mipsel-mti-elf-gcc' -type f -print -quit); \
		test -n "$$found"; \
		root=$$(dirname "$$(dirname "$$found")"); \
		mkdir -p "$(TOOLCHAIN)"; \
		cp -a "$$root"/. "$(TOOLCHAIN)"/; \
		rm -f /tmp/frog-toolchain.tar.xz; \
	fi
	@test -x "$(TOOLCHAIN)/bin/mipsel-mti-elf-gcc" || { echo "missing: $(TOOLCHAIN)/bin/mipsel-mti-elf-gcc"; exit 1; }

ci-commit-check: ci-deps ci-toolchain
	$(Q)$(MAKE) quick-check TOOLCHAIN=$(TOOLCHAIN)
	@echo "  CI      sdk"
	$(Q)$(MAKE) --no-print-directory sdk TOOLCHAIN=$(TOOLCHAIN) HOSTCC=$(HOSTCC)
	@echo "  CI      lib"
	$(Q)$(MAKE) --no-print-directory lib TOOLCHAIN=$(TOOLCHAIN) HOSTCC=$(HOSTCC)
	@echo "  CI      js2300-check"
	$(Q)$(MAKE) --no-print-directory js2300-check TOOLCHAIN=$(TOOLCHAIN) HOSTCC=$(HOSTCC)
	@echo "  CI      frontend-check"
	$(Q)$(MAKE) --no-print-directory frontend-check TOOLCHAIN=$(TOOLCHAIN) HOSTCC=$(HOSTCC)

ci-sd-zip: ci-deps ci-toolchain
	$(Q)$(MAKE) doctor TOOLCHAIN=$(TOOLCHAIN)
	$(Q)$(MAKE) sd-zip TOOLCHAIN=$(TOOLCHAIN) \
		SDCARD_PACKAGE_DIR=$(CI_SDCARD_PACKAGE_DIR) \
		SDZIP=$(CI_SDZIP) \
		ZIP_COMPRESSION=$(CI_ZIP_COMPRESSION)

clean:
	$(Q)rm -rf $(BUILD) $(OUT) $(ASD)

distclean: clean
	$(Q)find . -type f \( -name '*~' -o -name '*.tmp' -o -name '.DS_Store' \) -exec rm -f {} +

DEPFILES := $(wildcard $(BUILD)/*.d $(BUILD)/*/*.d)
-include $(DEPFILES)
