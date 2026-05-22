# User-facing path defaults. Override these in untracked config.mk.
TOOLCHAIN ?= /opt/mipsel-mti-elf
TOOLCHAIN_UNAME_M := $(shell uname -m)
TOOLCHAIN_HOST_ARCH := $(if $(filter aarch64 arm64,$(TOOLCHAIN_UNAME_M)),arm64,$(if $(filter x86_64 amd64,$(TOOLCHAIN_UNAME_M)),x86_64,$(TOOLCHAIN_UNAME_M)))
TOOLCHAIN_URL ?= https://github.com/axgdev/frog-toolchain/releases/download/v1.1.1/toolchain-stable-static-$(TOOLCHAIN_HOST_ARCH)-gcc15.2.0-binutils2.45-newlib4.5.0.20241231.tar.xz
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
DEPS ?= .deps
SDK ?= unifrog-hcrtos-sdk
HCRTOS_FFMPEG_INCLUDE ?= $(firstword \
	$(patsubst %/libavformat/avformat.h,%,$(wildcard $(SDK)/include/newlib/libavformat/avformat.h)) \
	$(patsubst %/libavformat/avformat.h,%,$(wildcard /root/host-frogdev/universal/sf2000_hcrtos/output/staging/usr/include/libavformat/avformat.h)) \
	$(patsubst %/libavformat/avformat.h,%,$(wildcard /root/host-frogdev/universal/sf2000_hcrtos/components/ffmpeg/source/libavformat/avformat.h)))
CORES ?= cores
CORE_SOURCE_ROOT ?= $(DEPS)/cores
CORE_SUPPORT_ROOT ?= $(DEPS)/support
JS2300 ?= js2300
MQUICKJS_DIR ?= $(DEPS)/mquickjs
MQUICKJS_URL ?= https://github.com/bellard/mquickjs.git
MQUICKJS_POLICY ?= head
MQUICKJS_REF ?= ee50431eac9b14b99f722b537ec4cac0c8dd75ab
LVGL_DIR ?= $(DEPS)/support/lvgl
LVGL_URL ?= https://github.com/lvgl/lvgl.git
LVGL_REF ?= 0019fc541f759b3323add63034502b0248afc58f
JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
PIN_MODE ?= $(if $(MODE),$(MODE),policy)
SD_MODE ?= wide20
SD_READ_MODE ?= boot
SD_FORCE_PIO ?= 0
SD_DMA_MODE ?= wrap
LOG_AUTO_FLUSH_BYTES ?= 16384
LOG_FLUSH_EVERY ?= 0
LOG_DISK_WRITES ?= 1
STORAGE_BOOT_MOUNT ?= 0
HCRTOS_MEDIA ?= firmware
FRONTEND_IMPL ?= native

-include config.mk

ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif
ifeq ($(filter --output-sync% -O%,$(MAKEFLAGS)),)
MAKEFLAGS += --output-sync=target
endif

.SUFFIXES:
.DEFAULT_GOAL := all

# Output names and build directories.
APP := unifrog
BUILD := build
OUT := output
TARGET := sf2000
ASD := bisrv.asd
FASTBOOT_ASD := fastboot.asd
FRONTEND_PACKAGE := $(OUT)/sdcard/unifrog
CORE_PACKAGE := $(OUT)/sdcard/unifrog/cores
MODULE_PACKAGE := $(OUT)/sdcard/unifrog/modules
USER_PACKAGE := $(OUT)/sdcard/unifrog_data
FRONTEND_MANIFEST := $(FRONTEND_PACKAGE)/manifest.ini
LANGUAGE_FILES := $(wildcard languages/*.ini)
SCRIPT_FILES := $(shell find scripts -type f 2>/dev/null | sort)
SDCARD_BIOS_PACKAGE := $(OUT)/sdcard/bios/bisrv.asd
SDCARD_FIRMWARE_PACKAGE := $(OUT)/sdcard/unifrog/firmware/unifrog.bin
SDCARD_PACKAGE_DIR ?= $(OUT)/sdcard-package
SDZIP ?= $(OUT)/UniFrog-sdcard.zip
ZIP_COMPRESSION ?= -1
CI_SDCARD_PACKAGE_DIR ?= $(abspath sdcard-package)
CI_SDZIP ?= $(abspath UniFrog-local-sdcard.zip)
CI_ZIP_COMPRESSION ?= -1
CI_SUBMODULE_JOBS ?= 8
THIRD_PARTY_NOTICE ?= THIRD_PARTY.md
SDCARD_BIOS_DIR := $(SDCARD)/bios
SDCARD_FIRMWARE_DIR := $(SDCARD)/unifrog/firmware
SDCARD_USER_DIR := $(SDCARD)/unifrog_data
ASDPACK := $(BUILD)/asdpack
THEME_ARCHIVE_CHECK := $(BUILD)/theme_archive_check
THEME_VISUAL_CHECK := $(BUILD)/theme_visual_check
SDCARD ?= /media/mmcblk0
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
HOSTCFLAGS ?= -O0 -Wall -Wextra
MIPS_ARCH ?= mips32
MIPS_TUNE ?= mips32
ARCH_CFLAGS ?= -march=$(MIPS_ARCH) -mtune=$(MIPS_TUNE)
OPT_SIZE ?= -Os
OPT_FAST ?= -O2
OPT_AUDIO ?= -Os
OPT_FLAGS := -O0 -O1 -O2 -O3 -Os -Og -Ofast
SD_MODES := safe wide1 wide2 wide4 wide8 wide10 wide12 wide14 wide16 wide18 wide20 wide22 wide24 wide25 wide37 hs1 wide50 wide uhs12 uhs25 uhs
SD_READ_MODES := boot off none safe wide1 wide2 wide4 wide8 wide10 wide12 wide14 wide16 wide18 wide20 wide22 wide24 wide25 wide37 hs1 wide50 wide uhs12 uhs25 uhs
HCRTOS_MEDIA_MODES := module firmware
FRONTEND_IMPLS := native

ifneq ($(filter $(SD_MODE),$(SD_MODES)),$(SD_MODE))
$(error SD_MODE must be one of: $(SD_MODES))
endif
ifneq ($(filter $(SD_READ_MODE),$(SD_READ_MODES)),$(SD_READ_MODE))
$(error SD_READ_MODE must be one of: $(SD_READ_MODES))
endif
ifneq ($(filter $(HCRTOS_MEDIA),$(HCRTOS_MEDIA_MODES)),$(HCRTOS_MEDIA))
$(error HCRTOS_MEDIA must be one of: $(HCRTOS_MEDIA_MODES))
endif
ifneq ($(filter $(FRONTEND_IMPL),$(FRONTEND_IMPLS)),$(FRONTEND_IMPL))
$(error FRONTEND_IMPL must be one of: $(FRONTEND_IMPLS))
endif

SD_CLOCK_FREQUENCY := 198000000
SD_UHS_SDR12 := 0
SD_UHS_SDR25 := 0
SD_UHS_SDR50 := 0

ifeq ($(SD_MODE),safe)
SD_BUS_WIDTH := 1
SD_CLOCK_FREQUENCY := 25000000
SD_CAP_HIGHSPEED := 0
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),hs1)
SD_BUS_WIDTH := 1
SD_CAP_HIGHSPEED := 1
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 1
else ifeq ($(SD_MODE),wide1)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 1000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide2)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 2000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide4)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 4000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide8)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 8000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide10)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 10000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide12)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 12000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide14)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 14000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide16)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 16000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide18)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 18000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide20)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 20000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide22)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 22000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide24)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 24000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide25)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 0
SD_CLOCK_FREQUENCY := 25000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide37)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 1
SD_CLOCK_FREQUENCY := 37000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 0
else ifeq ($(SD_MODE),wide50)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 1
SD_CLOCK_FREQUENCY := 50000000
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 1
else ifeq ($(SD_MODE),wide)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 1
SD_NO_1V8 := 1
SD_EXPERIMENTAL := 1
else ifeq ($(SD_MODE),uhs12)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 1
SD_UHS_SDR12 := 1
SD_CLOCK_FREQUENCY := 50000000
SD_NO_1V8 := 0
SD_EXPERIMENTAL := 1
else ifeq ($(SD_MODE),uhs25)
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 1
SD_UHS_SDR12 := 1
SD_UHS_SDR25 := 1
SD_CLOCK_FREQUENCY := 99000000
SD_NO_1V8 := 0
SD_EXPERIMENTAL := 1
else
SD_BUS_WIDTH := 4
SD_CAP_HIGHSPEED := 1
SD_UHS_SDR12 := 1
SD_UHS_SDR25 := 1
SD_UHS_SDR50 := 1
SD_NO_1V8 := 0
SD_EXPERIMENTAL := 1
endif

SD_CAP_UHS := $(if $(filter 1,$(SD_UHS_SDR12) $(SD_UHS_SDR25) $(SD_UHS_SDR50)),1,0)

GCC_LIBDIR ?= $(firstword $(wildcard $(TOOLCHAIN)/lib/gcc/mipsel-mti-elf/*))
SYS_LIBDIR := $(TOOLCHAIN)/mipsel-mti-elf/lib
Q := $(if $(V),,@)
UNIFROG_GIT_COMMIT := $(shell git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_GIT_TAG := $(shell git describe --tags --exact-match 2>/dev/null || true)
UNIFROG_BOOT_VERSION := $(shell tag=$$(git describe --tags --exact-match 2>/dev/null); if test -n "$$tag"; then printf '%s\n' "$$tag"; else commit=$$(git rev-parse --short=12 HEAD 2>/dev/null || echo unknown); printf 'revision %s\n' "$$commit"; fi)
UNIFROG_GIT_DIRTY := $(shell git diff-index --quiet --ignore-submodules=dirty HEAD -- 2>/dev/null && echo 0 || echo 1)
UNIFROG_SDK_GIT_COMMIT := $(shell git -C $(SDK) rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_CORES_GIT_COMMIT := $(shell git -C $(CORE_SOURCE_ROOT)/libretro-common rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_JS2300_GIT_COMMIT := $(shell git -C $(JS2300) rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_FRONTEND_GIT_COMMIT := $(shell git -C $(FRONTEND) rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
UNIFROG_NATIVE_FRONTEND_GIT_COMMIT := native

# Treat SDK headers as system headers so third-party/newlib warnings do not
# obscure warnings from the UniFrog source itself.
PROJECT_INCLUDES := -Iinclude -Isrc -I$(BUILD) -I$(CORE_SOURCE_ROOT)/libretro-common/include -I$(JS2300)/include -I$(LVGL_DIR) -I$(LVGL_DIR)/src
SDK_INCLUDES := \
	-isystem $(SDK)/include \
	-isystem $(SDK)/include/hcrtos \
	-isystem $(SDK)/include/newlib \
	-isystem $(SDK)/include/vendor

DTS_CPPFLAGS := \
	-Idts/include \
	-I$(SDK)/include/hcrtos \
	-DUNIFROG_SD_CLOCK_FREQUENCY=$(SD_CLOCK_FREQUENCY) \
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
	-DSUPPORT_FFPLAYER \
	-DUNIFROG_HCRTOS_MEDIA=\"$(HCRTOS_MEDIA)\" \
	-DUNIFROG_HCRTOS_MEDIA_MODULE=$(if $(filter module,$(HCRTOS_MEDIA)),1,0) \
	-DUNIFROG_HCRTOS_MEDIA_FIRMWARE=$(if $(filter firmware,$(HCRTOS_MEDIA)),1,0) \
	-DUNIFROG_FRONTEND_IMPL=\"$(FRONTEND_IMPL)\" \
	-DUNIFROG_FRONTEND_NATIVE=$(if $(filter native,$(FRONTEND_IMPL)),1,0) \
	-DUNIFROG_FRONTEND_MUOS=0 \
	-DUNIFROG_FRONTEND_JS2300=$(if $(filter js2300,$(FRONTEND_IMPL)),1,0) \
	-DUNIFROG_SD_MODE=\"$(SD_MODE)\" \
	-DUNIFROG_SD_READ_MODE=\"$(SD_READ_MODE)\" \
	-DUNIFROG_SD_FORCE_PIO=$(SD_FORCE_PIO) \
	-DUNIFROG_SD_DMA_MODE=\"$(SD_DMA_MODE)\" \
	-DUNIFROG_SD_DMA_MODE_QUIRKS=$(if $(filter quirks,$(SD_DMA_MODE)),1,0) \
	-DUNIFROG_SD_DMA_MODE_WRAP=$(if $(filter wrap,$(SD_DMA_MODE)),1,0) \
	-DUNIFROG_STORAGE_BOOT_MOUNT=$(STORAGE_BOOT_MOUNT) \
	-DUNIFROG_LOG_AUTO_FLUSH_BYTES=$(LOG_AUTO_FLUSH_BYTES) \
	-DUNIFROG_LOG_FLUSH_EVERY=$(LOG_FLUSH_EVERY) \
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
	-DUNIFROG_NATIVE_FRONTEND_GIT_COMMIT=\"$(UNIFROG_NATIVE_FRONTEND_GIT_COMMIT)\"

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
	$(SDK_INCLUDES) \
	$(if $(HCRTOS_FFMPEG_INCLUDE),-isystem $(HCRTOS_FFMPEG_INCLUDE))
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

HCRTOS_DISPLAY_LDLIBS := \
	-lviddrv

HCRTOS_MEDIA_LDLIBS := \
	-lavformat \
	-lavcodec \
	-lavutil \
	-lswscale \
	-lntfs

# Normal archives are pulled as needed by the linker.
LDLIBS := \
	$(if $(filter firmware,$(HCRTOS_MEDIA)),$(HCRTOS_MEDIA_LDLIBS)) \
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
	-lmmchosthc15 \
	-lefuse

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

WHOLE_LIBS := $(if $(filter firmware,$(HCRTOS_MEDIA)),$(HCRTOS_MEDIA_WHOLE_LIBS)) $(CORE_WHOLE_LIBS)

JS2300_HOST_SOURCES := \
	src/frontend/js2300_frontend.c \
	src/frontend/js2300_frontend_actions.c \
	src/frontend/js2300_frontend_bindings.c \
	src/frontend/js2300_frontend_catalog.c \
	src/frontend/js2300_frontend_storage.c
FRONTEND_HOST_SOURCES :=
NATIVE_FRONTEND_SOURCES := \
	src/native_frontend.c \
	src/frontend_lvgl.c
ifeq ($(FRONTEND_IMPL),native)
FRONTEND_HOST_SOURCES := $(NATIVE_FRONTEND_SOURCES) $(JS2300_HOST_SOURCES)
endif
FRONTEND_HOST_OBJECTS := $(patsubst src/%.c,$(BUILD)/%.o,$(FRONTEND_HOST_SOURCES))

APP_OBJECTS := \
	$(BUILD)/main.o \
	$(FRONTEND_HOST_OBJECTS)

BUILD_IDENTITY_OBJECTS := \
	$(BUILD)/main.o \
	$(FRONTEND_HOST_OBJECTS) \
	$(BUILD)/unifrog_libretro_host.o \
	$(BUILD)/unifrog_platform.o

UNIFROG_OBJECTS := \
	$(BUILD)/unifrog_abi.o \
	$(BUILD)/unifrog_abi_tramp.o \
	$(BUILD)/unifrog_audio.o \
	$(BUILD)/unifrog_av.o \
	$(BUILD)/unifrog_backlight.o \
	$(BUILD)/unifrog_battery.o \
	$(BUILD)/unifrog_boot.o \
	$(BUILD)/unifrog_boot_logo.o \
	$(BUILD)/unifrog_boot_trace.o \
	$(BUILD)/unifrog_core_module_loader.o \
	$(BUILD)/unifrog_diag.o \
	$(BUILD)/unifrog_display_benchmark.o \
	$(BUILD)/unifrog_exception_record.o \
	$(BUILD)/unifrog_fb.o \
	$(BUILD)/unifrog_ge.o \
	$(BUILD)/unifrog_gfx.o \
	$(BUILD)/unifrog_image.o \
	$(BUILD)/unifrog_input.o \
	$(BUILD)/unifrog_input_wireless.o \
	$(BUILD)/unifrog_libretro_host.o \
	$(BUILD)/unifrog_libretro_tramp.o \
	$(BUILD)/unifrog_log.o \
	$(BUILD)/unifrog_mips_call.o \
	$(BUILD)/unifrog_panic.o \
	$(BUILD)/unifrog_path.o \
	$(BUILD)/unifrog_platform.o \
	$(BUILD)/unifrog_perf.o \
	$(BUILD)/unifrog_png.o \
	$(BUILD)/unifrog_presenter.o \
	$(BUILD)/unifrog_runtime.o \
	$(BUILD)/unifrog_scpu.o \
	$(BUILD)/unifrog_storage_probe.o \
	$(BUILD)/unifrog_surface_alloc.o \
	$(BUILD)/unifrog_text.o
UNIFROG_OBJECTS += \
	$(BUILD)/unifrog_ui.o

ifeq ($(HCRTOS_MEDIA),firmware)
UNIFROG_OBJECTS += \
	$(BUILD)/unifrog_media.o
else
UNIFROG_OBJECTS += $(BUILD)/unifrog_sdk_optional_stubs.o
endif

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
LIBJS2300 := $(JS2300)/output/libjs2300.a
LIBJS2300_IF := $(if $(filter native,$(FRONTEND_IMPL)),$(LIBJS2300))
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
BOOT_LOGO_PPM := $(BUILD)/boot/unifrog-logo.ppm
BOOT_LOGO_STAMPED_PPM := $(BUILD)/boot/unifrog-logo-stamped.ppm
BOOT_LOGO_STAMPED_PNG := $(BUILD)/boot/unifrog-logo-stamped.png
BOOT_LOGO_RGB565_INC := $(BUILD)/boot/unifrog-logo-rgb565.inc
BOOT_LOGO_STAMP := $(BUILD)/boot/unifrog-logo.stamp
CHD_SUPPORT_CORE_LIB := $(CORES)/output/libchdr-support-sf2000.a
FIRMWARE_LIBRETRO_CORE_LIBS ?=
CORE_BUILD_DEPS = $(CORES)/Makefile $(CORES)/manifest.mk $(CORE_REV_STAMP)
CORE_MAKE_ARGS := \
	TOOLCHAIN=$(TOOLCHAIN) \
	CROSS_COMPILE=$(CROSS_COMPILE) \
	SDK=$(abspath $(SDK)) \
	CORE_SOURCE_ROOT=$(abspath $(CORE_SOURCE_ROOT)) \
	CORE_SUPPORT_ROOT=$(abspath $(CORE_SUPPORT_ROOT)) \
	CCACHE=$(CCACHE) \
	JOBS=$(JOBS)
CORE_BATCH_GOALS := all check verify sdcard-package sd-zip install refresh-sd refresh-sd-clean
CORE_BATCH_BUILD := $(if $(filter $(CORE_BATCH_GOALS),$(MAKECMDGOALS)),1,0)
CORE_SMOKE_MAKE_FLAGS := --no-print-directory
ifeq ($(V),)
CORE_SMOKE_MAKE_FLAGS += --silent
endif
LIBRETRO_COMMON_BUILD_DEPS = $(CORE_BUILD_DEPS)
CHD_SUPPORT_BUILD_DEPS = $(CORE_BUILD_DEPS)
LIBRETRO_MODULES := \
	gambatte:GAMBATTE:gambatte:gambatte:gb\|gbc:-:- \
	gpsp:GPSP:gpsp:gpsp:gba:gpsp:- \
	gpsp-gbac-prosty:GPSP_MULTICORE:gpsp-gbac-prosty:gpsp_multicore:gba:gpsp_multicore:- \
	picodrive:PICODRIVE:picodrive:picodrive:md\|gen\|smd\|sms\|gg\|sg\|32x\|cue\|chd\|iso:picodrive:$(CHD_SUPPORT_CORE_LIB) \
	snes9x2005:SNES9X2005:snes9x2005:snes9x2005:sfc\|smc:snes9x2005:- \
	snes9x2002:SNES9X2002:snes9x2002:snes9x2002:sfc\|smc:snes9x2002:- \
	quicknes:QUICKNES:quicknes:quicknes:nes:quicknes:- \
	fceumm:FCEUMM:fceumm:fceumm:nes\|fds:fceumm:- \
	gearboy:GEARBOY:gearboy:gearboy:gb\|gbc:gearboy:- \
	pce-fast:PCE_FAST:pce_fast:pce-fast:pce\|sgx\|cue\|chd:pce_fast:$(CHD_SUPPORT_CORE_LIB) \
	qpsx:QPSX:qpsx:qpsx:bin\|iso\|img\|cue\|pbp:qpsx:- \
	pmp-video:PMP_VIDEO:pmp_video:pmp-video:avi:pmp_video:-

define LIBRETRO_MODULE_REGISTER
$(2)_CORE_ID := $(1)
$(2)_CORE_LIB := $(CORES)/output/$(4)_libretro_sf2000.a
$(2)_BUILD_DEPS := $(CORE_BUILD_DEPS)
$(2)_CORE_BIN := $(CORE_PACKAGE)/$(3).bin
$(2)_CORE_OUT := $(BUILD)/core_modules/$(3).out
$(2)_CORE_ENTRY := $(BUILD)/core_modules/$(3)_entry.o
$(2)_CORE_MODULE := $(3)
$(2)_CORE_TARGET := $(4)
$(2)_CORE_EXTENSIONS := $(5)
$(2)_CORE_SYMBOL_PREFIX := $(if $(filter -,$(6)),,$(6))
$(2)_CORE_SUPPORT_LIBS := $(if $(filter -,$(7)),,$(7))
CORE_VAR_$(1) := $(2)
endef

$(foreach module,$(LIBRETRO_MODULES),$(eval $(call LIBRETRO_MODULE_REGISTER,$(word 1,$(subst :, ,$(module))),$(word 2,$(subst :, ,$(module))),$(word 3,$(subst :, ,$(module))),$(word 4,$(subst :, ,$(module))),$(word 5,$(subst :, ,$(module))),$(word 6,$(subst :, ,$(module))),$(word 7,$(subst :, ,$(module))))))

PCE_FAST_CORE_LIB := $(CORES)/output/pce_fast_libretro_sf2000.a
PCE_FAST_CORE_BIN := $(CORE_PACKAGE)/pce-fast.bin
QPSX_CORE_LIB := $(CORES)/output/pcsx4all_libretro_sf2000.a
PMP_VIDEO_CORE_LIB := $(CORES)/output/pmp_libretro_sf2000.a
PMP_VIDEO_CORE_BIN := $(CORE_PACKAGE)/pmp-video.bin
LIBRETRO_CORE_VARS := $(foreach module,$(LIBRETRO_MODULES),$(word 2,$(subst :, ,$(module))))
LIBRETRO_CORE_IDS := $(foreach var,$(LIBRETRO_CORE_VARS),$($(var)_CORE_ID))
PACKAGE_LIBRETRO_CORE_LIBS := $(foreach var,$(LIBRETRO_CORE_VARS),$($(var)_CORE_LIB))
LIBRETRO_CORE_BINS := $(foreach var,$(LIBRETRO_CORE_VARS),$($(var)_CORE_BIN))
LIBRETRO_CORE_MODULE_OUTS := $(foreach var,$(LIBRETRO_CORE_VARS),$($(var)_CORE_OUT))
LIBRETRO_CORE_ENTRY_OBJECTS := $(foreach var,$(LIBRETRO_CORE_VARS),$($(var)_CORE_ENTRY))
CORE_MODULE_IDS := $(foreach var,$(LIBRETRO_CORE_VARS),$($(var)_CORE_MODULE))
ifneq ($(CORE),)
SELECTED_CORE_VAR := $(CORE_VAR_$(CORE))
ifeq ($(SELECTED_CORE_VAR),)
$(error unknown CORE='$(CORE)'; supported cores: $(LIBRETRO_CORE_IDS))
endif
SELECTED_CORE_LIB := $($(SELECTED_CORE_VAR)_CORE_LIB)
SELECTED_CORE_BIN := $($(SELECTED_CORE_VAR)_CORE_BIN)
SELECTED_CORE_OUT := $($(SELECTED_CORE_VAR)_CORE_OUT)
else
SELECTED_CORE_LIB :=
SELECTED_CORE_BIN :=
SELECTED_CORE_OUT :=
endif
HCRTOS_MEDIA_MODULE_BIN := $(MODULE_PACKAGE)/hcrtos-media.bin
HCRTOS_MEDIA_MODULE_OUT := $(BUILD)/native_modules/hcrtos_media.out
HCRTOS_MEDIA_MODULE_ARCHIVE := $(BUILD)/native_modules/hcrtos_media.a
HCRTOS_MEDIA_MODULE_BINS := $(if $(filter module,$(HCRTOS_MEDIA)),$(HCRTOS_MEDIA_MODULE_BIN))
HCRTOS_MEDIA_MODULE_OBJECTS := \
	$(BUILD)/native_modules/unifrog_media_module_entry.o \
	$(BUILD)/native_modules/unifrog_media_module_support.o \
	$(BUILD)/native_modules/unifrog_media.o
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
NATIVE_MODULE_OUTS := $(if $(filter module,$(HCRTOS_MEDIA)),$(HCRTOS_MEDIA_MODULE_OUT))
UNIFROG_CORE_MODULE_BASE ?= 0x83000000
CORE_MODULE_CFLAGS := $(CFLAGS) -mno-abicalls -fno-pic
CORE_MODULE_CFLAGS_NO_IDENTITY := $(filter-out $(IDENTITY_DEFINES),$(CORE_MODULE_CFLAGS))
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
FASTBOOT_CFLAGS := -EL $(ARCH_CFLAGS) -Os -pipe -msoft-float -fsigned-char -W \
	-ffunction-sections -fdata-sections -G0 \
	-ffreestanding -fno-builtin -fno-pic -mno-abicalls \
	-nostdinc -I$(GCC_LIBDIR)/include

CORE_MODULE_ENTRY_OBJECTS := $(LIBRETRO_CORE_ENTRY_OBJECTS)
CORE_MODULE_SUPPORT_OBJECT := $(BUILD)/core_modules/support.o

DTS_INPUTS := $(DTS) $(shell test ! -d dts/include || find dts/include -type f | sort)
SDK_PATCHES := $(shell test ! -d patches/open-source || find patches/open-source -type f -name '*.patch' | sort)
JS2300_INPUTS := $(addprefix $(JS2300)/,$(shell test ! -d "$(JS2300)/.git" || git -C "$(JS2300)" ls-files))
MQUICKJS_INPUTS := $(addprefix $(MQUICKJS_DIR)/,$(shell test ! -d "$(MQUICKJS_DIR)/.git" || git -C "$(MQUICKJS_DIR)" ls-files '*.c' '*.h' Makefile))

BUILD_CONFIG_TOKEN := $(shell printf '%s\n' \
	'CC=$(CC)' 'CXX=$(CXX)' 'LD=$(LD)' 'AR=$(AR)' 'OBJCOPY=$(OBJCOPY)' \
	'HOSTCC=$(HOSTCC)' 'HOSTCFLAGS=$(HOSTCFLAGS)' \
	'ARCH_CFLAGS=$(ARCH_CFLAGS)' 'OPT_SIZE=$(OPT_SIZE)' \
	'OPT_FAST=$(OPT_FAST)' 'OPT_AUDIO=$(OPT_AUDIO)' \
	'CONFIG_DEFINES=$(CONFIG_DEFINES)' 'PROJECT_INCLUDES=$(PROJECT_INCLUDES)' \
	'SDK_INCLUDES=$(SDK_INCLUDES)' 'CFLAGS=$(CFLAGS_NO_IDENTITY)' \
	'CFLAGS_FAST=$(CFLAGS_FAST_NO_IDENTITY)' \
	'CFLAGS_AUDIO=$(CFLAGS_AUDIO_NO_IDENTITY)' \
	'CFLAGS_VIDEO=$(CFLAGS_VIDEO_NO_IDENTITY)' \
	'LDFLAGS=$(LDFLAGS)' 'LDLIBS=$(LDLIBS)' 'WHOLE_LIBS=$(WHOLE_LIBS)' \
	'CORE_MODULE_CFLAGS=$(CORE_MODULE_CFLAGS_NO_IDENTITY)' \
	'CORE_MODULE_LDFLAGS=$(CORE_MODULE_LDFLAGS)' \
	'CORE_MODULE_LDLIBS=$(CORE_MODULE_LDLIBS)' \
	'DTS_CPPFLAGS=$(DTS_CPPFLAGS)' 'DTCFLAGS=$(DTCFLAGS)' \
	'EMBED_DTB=$(EMBED_DTB)' 'FRONTEND_IMPL=$(FRONTEND_IMPL)' | cksum | awk '{print $$1}')
BUILD_IDENTITY_TOKEN := $(shell printf '%s\n' \
	'UNIFROG_GIT_COMMIT=$(UNIFROG_GIT_COMMIT)' \
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
	for f in $(CORES)/manifest.mk $(SDK_PATCHES) $(DTS_INPUTS); do test -f "$$f" && cksum "$$f"; done; \
} | cksum | awk '{print $$1}')
SDK_CONFIG_TOKEN := $(shell { \
	printf '%s\n' 'TOOLCHAIN=$(TOOLCHAIN)' 'CROSS_COMPILE=$(CROSS_COMPILE)' \
		'CCACHE=$(CCACHE)' 'JOBS=$(JOBS)' 'SD_MODE=$(SD_MODE)' \
		'SDK=$(abspath $(SDK))'; \
	git -C "$(SDK)" rev-parse --short=12 HEAD 2>/dev/null || echo unknown; \
	for f in $(SDK_PATCHES) $(DTS_INPUTS); do test -f "$$f" && cksum "$$f"; done; \
} | cksum | awk '{print $$1}')
CORE_ARCHIVE_STAMP := $(BUILD)/core-archives.$(CORE_CONFIG_TOKEN).stamp
JS2300_CONFIG_TOKEN := $(shell { \
	printf '%s\n' 'TOOLCHAIN=$(TOOLCHAIN)' 'CROSS_COMPILE=$(CROSS_COMPILE)' \
		'MQUICKJS_DIR=$(abspath $(MQUICKJS_DIR))' 'HOSTCC=$(HOSTCC)'; \
	for f in $(JS2300_INPUTS); do test -f "$$f" && cksum "$$f"; done; \
	for f in $(MQUICKJS_INPUTS); do test -f "$$f" && cksum "$$f"; done; \
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
JS2300_CONFIG_STAMP := $(BUILD)/js2300-config.stamp
FRONTEND_CONFIG_STAMP := $(BUILD)/frontend-config.stamp
FRONTEND_PACKAGE_STAMP := $(FRONTEND_PACKAGE)/.package.$(FRONTEND_CONFIG_TOKEN).stamp
CONFIG_STAMPS := $(BUILD_CONFIG_STAMP) $(BUILD_IDENTITY_STAMP) \
	$(FASTBOOT_CONFIG_STAMP) $(DTS_MODE_STAMP) $(CORE_REV_STAMP) \
	$(JS2300_CONFIG_STAMP) $(FRONTEND_CONFIG_STAMP)

ifeq ($(EMBED_DTB),1)
APP_OBJECTS += $(DTB_OBJ)
endif

DEVICE_OBJECTS := $(APP_OBJECTS) $(UNIFROG_OBJECTS) \
	$(CORE_MODULE_ENTRY_OBJECTS) $(CORE_MODULE_SUPPORT_OBJECT) \
	$(HCRTOS_MEDIA_MODULE_OBJECTS)
FASTBOOT_OBJECTS := $(FASTBOOT_STAGE_OBJ) $(FASTBOOT_STAGE_ENTRY_OBJ) \
	$(FASTBOOT_STUB_OBJ)

$(CONFIG_STAMPS): | $(BUILD)
	$(Q)touch $@

$(DEVICE_OBJECTS): $(BUILD_CONFIG_STAMP)
$(FASTBOOT_OBJECTS): $(FASTBOOT_CONFIG_STAMP)

# These objects print build identity in device logs. Rebuild them only when the
# embedded identity changes, and let dependent core modules relink from the
# updated libunifrog archive.
$(BUILD_IDENTITY_OBJECTS): $(BUILD_IDENTITY_STAMP)

.DELETE_ON_ERROR:
COMMON_TARGETS := all help setup doctor deps deps-status upgrade-pins upgrade-deps repo-check quick-check check verify clean distclean rebuild list-cores core core-archive core-out
SETUP_TARGETS := deps-alpine deps-ubuntu deps-sdk deps-mquickjs deps-support deps-cores deps-lvgl
PACKAGE_TARGETS := frontend-package core-package module-package sdcard-package sd-zip install refresh-sd refresh-sd-clean
VERIFY_TARGETS := asdcheck fastboot-check fastboot-only-check layout-check boot-logo-check js2300-check frontend-check native-frontend-check core-smoke-check
ADVANCED_TARGETS := sdk dtb lib fastboot fastboot-only size ci-deps ci-toolchain ci-commit-check ci-sd-zip print-config
.PHONY: $(COMMON_TARGETS) $(SETUP_TARGETS) $(PACKAGE_TARGETS) $(VERIFY_TARGETS) $(ADVANCED_TARGETS)

all: $(ASD) sdcard-package
setup: deps

verify:
	$(Q)$(MAKE) --no-print-directory quick-check
	$(Q)$(MAKE) --no-print-directory check
	$(Q)$(MAKE) --no-print-directory fastboot-check

help:
	@echo "$(APP) common workflow:"
	@echo "  make setup         Fetch SDK submodule and external source inputs"
	@echo "  make doctor        Check toolchain, SDK, and fetched inputs"
	@echo "  make quick-check   Fast hygiene, core smoke, JS2300, and frontend checks"
	@echo "  make               Build $(ASD), $(OUT)/unifrog.bin, and SD files"
	@echo "  make verify        Build and verify firmware, fastboot, JS, and layout"
	@echo "  make deps          Same as make setup"
	@echo "  make deps-status   Show pins vs policy, or override MODE=head|tag"
	@echo "  make upgrade-deps  Bump pins by policy, or override MODE=head|tag"
	@echo "  make check         Build firmware, SD files, and link layout check"
	@echo "  make list-cores    List libretro CORE= ids"
	@echo "  make core CORE=picodrive"
	@echo "                     Build and package one libretro core module"
	@echo "  make core-archive CORE=picodrive"
	@echo "                     Build only that core's upstream static archive"
	@echo ""
	@echo "Setup:"
	@echo "  make deps-alpine   Install Alpine host packages"
	@echo "  make deps-ubuntu   Print Ubuntu host package command"
	@echo "  make deps-sdk      Initialize only the HCRTOS SDK submodule"
	@echo "  make deps-cores    Fetch only libretro core sources"
	@echo "  make deps-lvgl     Fetch only the LVGL checkout"
	@echo ""
	@echo "Packaging and device:"
	@echo "  make sdcard-package Build the complete $(OUT)/sdcard tree"
	@echo "  make sd-zip        Build $(SDZIP)"
	@echo "  make install       Copy firmware and SD files to SDCARD=$(SDCARD)"
	@echo "  make refresh-sd    Build, install, and sync SD files"
	@echo ""
	@echo "Focused checks:"
	@echo "  make repo-check core-smoke-check native-frontend-check js2300-check"
	@echo "  make boot-logo-check"
	@echo "  make fastboot-only-check"
	@echo "  make layout-check asdcheck fastboot-check"
	@echo "  make -C cores help"
	@echo "  make -C js2300 help"
	@echo ""
	@echo "Cleanup:"
	@echo "  make clean         Remove generated files"
	@echo "  make distclean     Also remove sub-build outputs"
	@echo ""
	@echo "Config:"
	@echo "  make print-config  Show current paths and tools"
	@echo "  make V=1           Show full compiler/linker commands"
	@echo "  make SD_MODE=wide20  Use the default resilient 4-bit 20 MHz SD profile"
	@echo "  make SD_MODE=safe    Use 1-bit 25 MHz SD with DMA safety workarounds"
	@echo "  make SD_FORCE_PIO=1  Enable the slow vendor PIO diagnostic path"
	@echo "  make SD_DMA_MODE=wrap Observe stock DMA through linker wrappers"
	@echo "  make SD_DMA_MODE=quirks Enable UniFrog DMA bounce/cache overrides"
	@echo "  make STORAGE_BOOT_MOUNT=1 Try the risky pre-menu SD mount path"
	@echo "                     Developer -> Storage test quick-sweeps SD profiles"
	@echo "  make SD_READ_MODE=wide25  Re-enable a runtime read window"
	@echo "  make LOG_FLUSH_EVERY=1  Flush each log line for diagnostics"
	@echo "  make LOG_AUTO_FLUSH_BYTES=4096  Set buffered log flush threshold"
	@echo "  make LOG_DISK_WRITES=0  Keep logs/reports in retained RAM only"
	@echo "  make HCRTOS_MEDIA=module  Keep native media in an SD-loaded module"
	@echo "  make HCRTOS_MEDIA=firmware  Link native media into unifrog.bin"
	@echo "  make FRONTEND_IMPL=native   Build the native frontend (default)"
	@echo "  make FRONTEND_IMPL=native   Build the native C frontend fallback"
	@echo "  make SD_MODE=wide10 Diagnostic 4-bit 10 MHz SD build"
	@echo "  make SD_MODE=wide18 Diagnostic 4-bit 18 MHz SD build"
	@echo "  make SD_MODE=wide20 Default resilient 4-bit 20 MHz SD build"
	@echo "  make SD_MODE=wide25 Diagnostic 4-bit 25 MHz SD build"
	@echo "  make SD_MODE=wide37 Diagnostic 4-bit 37 MHz SD build"
	@echo "  make SD_MODE=hs1   Diagnostic 1-bit high-speed SD build"
	@echo "  make SD_MODE=wide50 Diagnostic 4-bit high-speed, lower-clock SD build"
	@echo "  make SD_MODE=wide  Diagnostic 4-bit high-speed SD build"
	@echo "  make SD_MODE=uhs12 Diagnostic UHS SDR12-only SD build"
	@echo "  make SD_MODE=uhs25 Diagnostic UHS SDR25 SD build"
	@echo "  make SD_MODE=uhs   Diagnostic UHS SDR50 SD build"
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
	@echo "LVGL_DIR=$(LVGL_DIR)"
	@echo "HOSTCC=$(HOSTCC)"
	@echo "DTC=$(DTC)"
	@echo "SD_MODE=$(SD_MODE)"
	@echo "SD_READ_MODE=$(SD_READ_MODE)"
	@echo "SD_FORCE_PIO=$(SD_FORCE_PIO)"
	@echo "SD_DMA_MODE=$(SD_DMA_MODE)"
	@echo "STORAGE_BOOT_MOUNT=$(STORAGE_BOOT_MOUNT)"
	@echo "LOG_AUTO_FLUSH_BYTES=$(LOG_AUTO_FLUSH_BYTES)"
	@echo "LOG_FLUSH_EVERY=$(LOG_FLUSH_EVERY)"
	@echo "LOG_DISK_WRITES=$(LOG_DISK_WRITES)"
	@echo "HCRTOS_MEDIA=$(HCRTOS_MEDIA)"
	@echo "FRONTEND_IMPL=$(FRONTEND_IMPL)"
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

list-cores:
	@printf '%s\n' $(LIBRETRO_CORE_IDS)

ifeq ($(CORE),)
core core-archive core-out:
	@echo "usage: make $@ CORE=<id>"
	@echo "supported cores: $(LIBRETRO_CORE_IDS)"
	@exit 2
else
core: $(FRONTEND_PACKAGE_STAMP) $(SELECTED_CORE_BIN)

core-archive: $(SELECTED_CORE_LIB)

core-out: $(SELECTED_CORE_OUT)
endif

deps: deps-sdk deps-mquickjs deps-lvgl deps-cores

deps-alpine:
	apk add git make dtc tcc tcc-libs-static musl-dev ccache curl tar xz zip patch

deps-ubuntu:
	@echo "sudo apt-get update && sudo apt-get install -y git make device-tree-compiler tcc ccache curl xz-utils zip patch"

deps-sdk:
	git config --global --add safe.directory "$(abspath .)" 2>/dev/null || true
	git submodule sync unifrog-hcrtos-sdk
	git submodule update --init --depth 1 --filter=blob:none --jobs "$(JOBS)" \
		unifrog-hcrtos-sdk

deps-mquickjs:
	@mkdir -p $(DEPS)
	@fresh=0; \
	if test -d "$(MQUICKJS_DIR)/.git"; then \
		echo "  FETCH   $(MQUICKJS_DIR)"; \
		git -C "$(MQUICKJS_DIR)" remote set-url origin "$(MQUICKJS_URL)"; \
	else \
		echo "  CLONE   $(MQUICKJS_URL)"; \
		rm -rf "$(MQUICKJS_DIR)"; \
		git init -q "$(MQUICKJS_DIR)"; \
		git -C "$(MQUICKJS_DIR)" remote add origin "$(MQUICKJS_URL)"; \
		fresh=1; \
	fi; \
	if ! git -C "$(MQUICKJS_DIR)" cat-file -e "$(MQUICKJS_REF)^{commit}" 2>/dev/null; then \
		git -C "$(MQUICKJS_DIR)" fetch --depth 1 origin "$(MQUICKJS_REF)"; \
	fi; \
	git -C "$(MQUICKJS_DIR)" checkout -q "$(MQUICKJS_REF)"; \
	git -C "$(MQUICKJS_DIR)" reset --hard -q "$(MQUICKJS_REF)"; \
	if test "$$fresh" -eq 0; then \
		git -C "$(MQUICKJS_DIR)" clean -fdx -q; \
	fi

deps-lvgl:
	@mkdir -p "$(dir $(LVGL_DIR))"
	@fresh=0; \
	if test -d "$(LVGL_DIR)/.git"; then \
		echo "  FETCH   $(LVGL_DIR)"; \
		git -C "$(LVGL_DIR)" remote set-url origin "$(LVGL_URL)"; \
	else \
		echo "  CLONE   $(LVGL_URL)"; \
		rm -rf "$(LVGL_DIR)"; \
		git init -q "$(LVGL_DIR)"; \
		git -C "$(LVGL_DIR)" remote add origin "$(LVGL_URL)"; \
		fresh=1; \
	fi; \
	if ! git -C "$(LVGL_DIR)" cat-file -e "$(LVGL_REF)^{commit}" 2>/dev/null; then \
		git -C "$(LVGL_DIR)" fetch --depth 1 origin "$(LVGL_REF)"; \
	fi; \
	git -C "$(LVGL_DIR)" checkout -q "$(LVGL_REF)"; \
	git -C "$(LVGL_DIR)" reset --hard -q "$(LVGL_REF)"; \
	if test "$$fresh" -eq 0; then \
		git -C "$(LVGL_DIR)" clean -fdx -q; \
	fi

deps-status:
	@set -e; \
	mode="$(PIN_MODE)"; \
	case "$$mode" in policy|head|tag) ;; *) echo "MODE must be policy, head, or tag"; exit 1;; esac; \
	if test "$$mode" = policy; then mode="$(MQUICKJS_POLICY)"; fi; \
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
	printf '%-16s policy=%s mode=%s pinned=%s latest=%s source=%s:%s\n' mquickjs "$(MQUICKJS_POLICY)" "$$mode" "$(MQUICKJS_REF)" "$$1" "$$2" "$$3"
	@printf '%-16s pinned=%s url=%s\n' lvgl "$(LVGL_REF)" "$(LVGL_URL)"
	$(Q)$(MAKE) --no-print-directory -C $(CORES) pin-status PIN_MODE=$(PIN_MODE) \
		CORE_SOURCE_ROOT=$(abspath $(CORE_SOURCE_ROOT)) \
		CORE_SUPPORT_ROOT=$(abspath $(CORE_SUPPORT_ROOT))

upgrade-pins:
	@set -e; \
	mode="$(PIN_MODE)"; \
	case "$$mode" in policy|head|tag) ;; *) echo "MODE must be policy, head, or tag"; exit 1;; esac; \
	if test "$$mode" = policy; then mode="$(MQUICKJS_POLICY)"; fi; \
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
		echo "  PIN     mquickjs $$old -> $$new ($$kind $$label, policy $(MQUICKJS_POLICY), mode $$mode)"; \
	else \
		echo "  PIN     mquickjs already $$old ($$kind $$label, policy $(MQUICKJS_POLICY), mode $$mode)"; \
	fi
	$(Q)$(MAKE) --no-print-directory -C $(CORES) upgrade-pins PIN_MODE=$(PIN_MODE) \
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
	@if test "$(FRONTEND_IMPL)" = native; then \
		test -f "$(JS2300)/Makefile" || { echo "missing JS2300 source: $(JS2300)"; exit 1; }; \
		test -f "$(MQUICKJS_DIR)/mquickjs.c" || { echo "missing MQuickJS checkout: $(MQUICKJS_DIR)"; exit 1; }; \
	fi
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
	@echo "  CHECK   $(FRONTEND_IMPL) frontend"
	$(Q)$(MAKE) --no-print-directory native-frontend-check
	@echo "  CHECK   js2300 script runtime"
	$(Q)$(MAKE) --no-print-directory js2300-check
	@echo "  CHECK   boot logo"
	$(Q)$(MAKE) --no-print-directory boot-logo-check
	@echo "OK"

core-smoke-check:
	$(Q)$(MAKE) $(CORE_SMOKE_MAKE_FLAGS) -C $(CORES) smoke-check $(CORE_MAKE_ARGS)

sdk: $(SDK_BUILD_STAMP)

$(SDK_BUILD_STAMP): $(SDK_KERNEL_LIB) | $(BUILD)
	$(Q)touch $@

$(SDK_KERNEL_LIB): $(DTS_INPUTS) $(SDK_PATCHES) | $(BUILD)
	$(Q)$(MAKE) -C $(SDK) check TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) CCACHE=$(CCACHE) JOBS=$(JOBS) \
		SD_MODE=$(SD_MODE) BOARD_DTS=$(abspath $(DTS)) \
		DTS_INCLUDE=$(abspath dts/include)
	$(Q)test -s $@

js2300-check:
	$(Q)$(MAKE) -C $(JS2300) check TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) MQUICKJS_DIR=$(abspath $(MQUICKJS_DIR)) \
		HOSTCC=$(HOSTCC)

frontend-check:
	@echo "  OK      JavaScript frontend removed"

native-frontend-check:
	$(Q)$(MAKE) --no-print-directory frontend-theme-check
	@echo "  OK      $(FRONTEND_IMPL) frontend sources"

frontend-theme-check: $(THEME_ARCHIVE_CHECK) $(THEME_VISUAL_CHECK)
	@echo "  CHECK   muxthm theme archive"
	$(Q)rm -rf "$(BUILD)/theme-check"
	$(Q)mkdir -p "$(BUILD)/theme-check/theme/640x480/scheme" \
		"$(BUILD)/theme-check/theme/640x480/image/wall"
	$(Q)printf '%s\n' '[background]' 'BACKGROUND = 101820' \
		'BACKGROUND_ALPHA = 255' '[list]' \
		'LIST_DEFAULT_BACKGROUND = 202830' \
		'LIST_DEFAULT_BACKGROUND_ALPHA = 128' \
		'LIST_FOCUS_BACKGROUND = 405060' \
		'LIST_FOCUS_BACKGROUND_ALPHA = 220' \
		'LIST_DEFAULT_TEXT = EEF1E8' \
		> "$(BUILD)/theme-check/theme/640x480/scheme/default.ini"
	$(Q)printf 'png placeholder\n' \
		> "$(BUILD)/theme-check/theme/640x480/image/wall/default.png"
	$(Q)(cd "$(BUILD)/theme-check/theme" && zip -q -r ../sample.muxthm .)
	$(Q)$(THEME_ARCHIVE_CHECK) "$(BUILD)/theme-check/sample.muxthm"
	@echo "  CHECK   theme visual"
	$(Q)$(THEME_VISUAL_CHECK) \
		"$(BUILD)/theme-check/theme/640x480/scheme/default.ini" \
		"$(BUILD)/theme-check/preview.ppm"
	$(Q)if test -f /tmp/unifrog-theme-test/Analogue.muxthm; then \
		$(THEME_ARCHIVE_CHECK) /tmp/unifrog-theme-test/Analogue.muxthm; \
		unzip -p /tmp/unifrog-theme-test/Analogue.muxthm 640x480/scheme/default.ini > "$(BUILD)/theme-check/Analogue-default.ini"; \
		$(THEME_VISUAL_CHECK) "$(BUILD)/theme-check/Analogue-default.ini" "$(BUILD)/theme-check/Analogue-preview.ppm"; \
	fi
	$(Q)if test -d /tmp/unifrog-themes; then \
		for theme in /tmp/unifrog-themes/*.muxthm; do \
			test -f "$$theme" || continue; \
			$(THEME_ARCHIVE_CHECK) "$$theme"; \
		done; \
	fi

frontend-package: $(FRONTEND_PACKAGE_STAMP)

$(FRONTEND_PACKAGE_STAMP): \
	Makefile $(FRONTEND_CONFIG_STAMP) $(BUILD_IDENTITY_STAMP) LICENSE \
	$(THIRD_PARTY_NOTICE) $(LANGUAGE_FILES) $(SCRIPT_FILES) | $(OUT)
	$(Q)rm -rf $(FRONTEND_PACKAGE)/app $(FRONTEND_PACKAGE)/user \
		$(FRONTEND_PACKAGE)/saves $(FRONTEND_PACKAGE)/cache \
		$(FRONTEND_PACKAGE)/logs $(FRONTEND_PACKAGE)/updates \
		$(FRONTEND_PACKAGE)/versions $(FRONTEND_PACKAGE)/themes \
		$(FRONTEND_PACKAGE)/languages $(FRONTEND_PACKAGE)/archive \
		$(FRONTEND_PACKAGE)/scripts \
		$(FRONTEND_PACKAGE)/main.js $(FRONTEND_PACKAGE)/main.js.mqbc \
		$(FRONTEND_PACKAGE)/bytecode-manifest.txt
	$(Q)rm -rf $(USER_PACKAGE)/probes $(USER_PACKAGE)/languages \
		$(USER_PACKAGE)/scripts
	$(Q)mkdir -p $(FRONTEND_PACKAGE)/firmware \
		$(USER_PACKAGE)/saves $(USER_PACKAGE)/cache \
		$(USER_PACKAGE)/logs $(USER_PACKAGE)/logs/crashlogs \
		$(USER_PACKAGE)/logs/rotatedlogs $(USER_PACKAGE)/logs/reports \
		$(USER_PACKAGE)/updates $(USER_PACKAGE)/versions \
		$(USER_PACKAGE)/themes $(USER_PACKAGE)/languages \
		$(USER_PACKAGE)/archive $(USER_PACKAGE)/scripts \
		$(USER_PACKAGE)/firmware
	$(Q)if test -n "$(LANGUAGE_FILES)"; then cp $(LANGUAGE_FILES) $(USER_PACKAGE)/languages/; fi
	$(Q)if test -n "$(SCRIPT_FILES)"; then \
		for script in $(SCRIPT_FILES); do \
			target="$(USER_PACKAGE)/$$script"; \
			mkdir -p "$$(dirname "$$target")"; \
			cp "$$script" "$$target"; \
		done; \
	fi
	$(Q){ \
		printf '%s\n' 'manifest_version=1'; \
		printf '%s\n' 'firmware_commit=$(UNIFROG_GIT_COMMIT)'; \
		printf '%s\n' 'firmware_dirty=$(UNIFROG_GIT_DIRTY)'; \
		printf '%s\n' 'sdk_commit=$(UNIFROG_SDK_GIT_COMMIT)'; \
		printf '%s\n' 'cores_commit=$(UNIFROG_CORES_GIT_COMMIT)'; \
		printf '%s\n' 'js2300_commit=$(if $(filter native,$(FRONTEND_IMPL)),$(UNIFROG_JS2300_GIT_COMMIT),disabled)'; \
		printf '%s\n' 'frontend_commit=$(UNIFROG_NATIVE_FRONTEND_GIT_COMMIT)'; \
		printf '%s\n' 'hcrtos_media=$(HCRTOS_MEDIA)'; \
		printf '%s\n' 'frontend_impl=$(FRONTEND_IMPL)'; \
		printf '%s\n' "generated_utc=$$(date -u +%Y-%m-%dT%H:%M:%SZ)"; \
	} > $(FRONTEND_MANIFEST)
	$(Q)cp LICENSE $(FRONTEND_PACKAGE)/LICENSE.txt
	$(Q)cp $(THIRD_PARTY_NOTICE) $(FRONTEND_PACKAGE)/THIRD_PARTY.md
	$(Q)rm -f $(FRONTEND_PACKAGE)/.package.*.stamp
	$(Q)touch $@

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
	@echo "  SDCARD  $@"
	$(Q)if test -f $@ && cmp -s $< $@; then touch $@; else cp $< $@; fi

$(SDCARD_FIRMWARE_PACKAGE): $(OUT)/unifrog.bin | $(dir $(SDCARD_FIRMWARE_PACKAGE))
	@echo "  SDCARD  $@"
	$(Q)rm -f $(OUT)/sdcard/firmware/unifrog.bin
	$(Q)rmdir $(OUT)/sdcard/firmware 2>/dev/null || true
	$(Q)if test -f $@ && cmp -s $< $@; then touch $@; else cp $< $@; fi

define CORE_ENTRY_DEFINES_RULE
$(1): CORE_MODULE_DEFINES := -DUNIFROG_MODULE_CORE_ID=\"$(2)\" -DUNIFROG_MODULE_EXTENSIONS=\"$(3)\" $(if $(4),-DUNIFROG_MODULE_SYMBOL_PREFIX=$(4))
endef

$(foreach var,$(LIBRETRO_CORE_VARS),$(eval $(call CORE_ENTRY_DEFINES_RULE,$($(var)_CORE_ENTRY),$($(var)_CORE_ID),$($(var)_CORE_EXTENSIONS),$($(var)_CORE_SYMBOL_PREFIX))))

$(BUILD)/core_modules/%_entry.o: src/unifrog_core_module_entry.c include/unifrog/core_module.h | $(BUILD)
	@echo "  CC      $< ($*)"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) $(CORE_MODULE_DEFINES) -MD -MP -c $< -o $@

$(BUILD)/core_modules/support.o: src/unifrog_core_module_support.c include/unifrog/abi.h | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/native_modules/%.o: src/%.c | $(BUILD)
	@echo "  CC      $< (native module)"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CORE_MODULE_CFLAGS) -MD -MP -c $< -o $@

$(HCRTOS_MEDIA_MODULE_ARCHIVE): $(HCRTOS_MEDIA_MODULE_OBJECTS) | $(OUT)
	@echo "  AR      $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(AR) rcs $@ $^

$(HCRTOS_MEDIA_MODULE_OUT): $(HCRTOS_MEDIA_MODULE_ARCHIVE) \
	linker/core-module.ld linker/hc15xx/peripherals.ld \
	$(SDK_BUILD_STAMP) $(BUILD_CONFIG_STAMP) | $(OUT)
	@echo "  LD      $@"
	$(Q)$(LD) $(CORE_MODULE_LDFLAGS) -o $@ -Map $@.map -u unifrog_core_module_entry --start-group $(HCRTOS_MEDIA_MODULE_ARCHIVE) $(HCRTOS_MEDIA_MODULE_LDLIBS) --whole-archive $(HCRTOS_MEDIA_WHOLE_LIBS) --no-whole-archive --end-group

$(HCRTOS_MEDIA_MODULE_BIN): $(HCRTOS_MEDIA_MODULE_OUT) | $(MODULE_PACKAGE)
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(BUILD)/third_party/lz4/%.o: src/third_party/lz4/%.c | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(LZ4_CFLAGS) -MD -MP -c $< -o $@

$(ZSTD_DECODER_OBJ): $(ZSTD_DECODER_SRC) | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(ZSTD_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/lvgl/%.o: $(LVGL_DIR)/src/%.c | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS_FAST) -w -MD -MP -c $< -o $@

define CORE_MODULE_RULES
$(BUILD)/core_modules/$(1).out: $(BUILD)/core_modules/$(1)_entry.o $(BUILD)/core_modules/support.o $$($(2)_CORE_LIB) $$($(2)_CORE_SUPPORT_LIBS) $(LIBRETRO_COMMON_LIB) $(LIBUNIFROG) linker/core-module.ld linker/hc15xx/peripherals.ld $(SDK_BUILD_STAMP) $(BUILD_CONFIG_STAMP) | $(OUT)
	@echo "  LD      $$@"
	$(Q)$(LD) $(CORE_MODULE_LDFLAGS) -o $$@ -Map $$@.map -u unifrog_core_module_entry --start-group $$(filter %.o %.a,$$^) $(CORE_MODULE_LDLIBS) --end-group

$(3): $(BUILD)/core_modules/$(1).out | $(CORE_PACKAGE)
	@echo "  OBJCOPY $$@"
	$(Q)$(OBJCOPY) -O binary $$< $$@
endef

$(foreach var,$(LIBRETRO_CORE_VARS),$(eval $(call CORE_MODULE_RULES,$($(var)_CORE_MODULE),$(var),$($(var)_CORE_BIN))))

$(BUILD) $(OUT):
	$(Q)mkdir -p $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/unifrog_boot_logo.o: $(BOOT_LOGO_RGB565_INC)
$(BUILD)/unifrog_fb.o $(BUILD)/unifrog_ge.o $(BUILD)/unifrog_presenter.o $(BUILD)/unifrog_surface_alloc.o: CFLAGS := $(CFLAGS_VIDEO)
$(BUILD)/unifrog_gfx.o $(BUILD)/unifrog_perf.o $(BUILD)/unifrog_scpu.o: CFLAGS := $(CFLAGS_FAST)
$(BUILD)/unifrog_audio.o: CFLAGS := $(CFLAGS_AUDIO)
$(BUILD)/unifrog_libretro_host.o: CFLAGS := $(CFLAGS_AUDIO) -I$(ZSTD_DIR)

$(BUILD)/%.o: src/%.S | $(BUILD)
	@echo "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -D__ASSEMBLY__ -MD -MP -c $< -o $@

$(FASTBOOT_STAGE_OBJ): src/fastboot/stage1.c | $(BUILD)
	@echo "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(FASTBOOT_CFLAGS) -MD -MP -c $< -o $@

$(FASTBOOT_STAGE_ENTRY_OBJ): src/fastboot/stage_entry.S | $(BUILD)
	@echo "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(FASTBOOT_CFLAGS) -D__ASSEMBLY__ -MD -MP -c $< -o $@

$(FASTBOOT_STAGE_OUT): $(FASTBOOT_STAGE_ENTRY_OBJ) $(FASTBOOT_STAGE_OBJ) \
	linker/fastboot/stage.ld $(FASTBOOT_CONFIG_STAMP) | $(OUT)
	@echo "  LD      $@"
	$(Q)$(LD) -EL --static -n --gc-sections -T linker/fastboot/stage.ld -Map $@.map -o $@ $(filter %.o,$^)

$(FASTBOOT_STAGE_BIN): $(FASTBOOT_STAGE_OUT) | $(BUILD)
	@echo "  OBJCOPY $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(OBJCOPY) -O binary $< $@

$(FASTBOOT_STUB_OBJ): src/fastboot/stub.S $(FASTBOOT_STAGE_BIN) | $(BUILD)
	@echo "  AS      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(FASTBOOT_CFLAGS) -D__ASSEMBLY__ \
		-DFASTBOOT_STAGE_BIN_PATH=\"$(FASTBOOT_STAGE_BIN)\" \
		-MD -MP -c $< -o $@

$(FASTBOOT_STUB_OUT): $(FASTBOOT_STUB_OBJ) linker/fastboot/stub.ld \
	$(FASTBOOT_CONFIG_STAMP) | $(OUT)
	@echo "  LD      $@"
	$(Q)$(LD) -EL --static -n --gc-sections -T linker/fastboot/stub.ld -Map $@.map -o $@ $(filter %.o,$^)

$(FASTBOOT_STUB_BIN): $(FASTBOOT_STUB_OUT)
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(BOOT_LOGO_TOOL): tools/bootlogo.c $(CORE_SUPPORT_ROOT)/zlib/inflate.c $(BUILD_CONFIG_STAMP) | $(BUILD)
	@echo "  HOSTCC  $@"
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

$(THEME_ARCHIVE_CHECK): tools/theme_archive_check.c $(BUILD_CONFIG_STAMP) | $(BUILD)
	@echo "  HOSTCC  $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(HOSTCC) $(HOSTCFLAGS) $< -lz -o $@

$(THEME_VISUAL_CHECK): tools/theme_visual_check.c $(BUILD_CONFIG_STAMP) | $(BUILD)
	@echo "  HOSTCC  $@"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(HOSTCC) $(HOSTCFLAGS) $< -o $@

$(BOOT_LOGO_STAMP): $(BOOT_LOGO_SRC) $(BOOT_LOGO_TOOL) $(BUILD_IDENTITY_STAMP)
	@echo "  BOOTLOGO $(UNIFROG_BOOT_VERSION)"
	$(Q)mkdir -p $(dir $(BOOT_LOGO_STAMP))
	$(Q)$(BOOT_LOGO_TOOL) $(BOOT_LOGO_SRC) "$(UNIFROG_BOOT_VERSION)" $(BOOT_LOGO_STAMPED_PPM) $(BOOT_LOGO_RGB565_INC)
	$(Q)touch $@

$(BOOT_LOGO_STAMPED_PPM) $(BOOT_LOGO_RGB565_INC): $(BOOT_LOGO_STAMP)

boot-logo-check: $(BOOT_LOGO_STAMPED_PPM) $(BOOT_LOGO_RGB565_INC)
	@test -s $(BOOT_LOGO_STAMPED_PPM)
	@test -s $(BOOT_LOGO_RGB565_INC)
	@echo "  OK      boot logo"

$(DTS_PRE): $(DTS_INPUTS) $(DTS_MODE_STAMP) $(BUILD_CONFIG_STAMP) | $(BUILD)
	@echo "  CPP     $<"
	$(Q)$(CC) $(DTS_CPPFLAGS) -Wp,-MD,$@.d -E -o $@ $<

$(DTB): $(DTS_PRE) $(BUILD_CONFIG_STAMP)
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

$(LIBUNIFROG): $(UNIFROG_OBJECTS) $(BUILD_CONFIG_STAMP) | $(OUT)
	@echo "  AR      $@"
	$(Q)$(AR) rcs $@ $(filter %.o,$^)

$(OUT)/$(TARGET).out: $(APP_OBJECTS) $(LIBUNIFROG) $(LIBJS2300_IF) \
	$(LIBRETRO_COMMON_LIB) $(FIRMWARE_LIBRETRO_CORE_LIBS) \
	$(SDK_BUILD_STAMP) $(BUILD_CONFIG_STAMP) | $(OUT)
	@echo "  LD      $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ -Map $@.map --start-group $(filter %.o %.a,$^) $(LDLIBS) --whole-archive $(WHOLE_LIBS) --no-whole-archive --end-group

$(LIBJS2300): $(JS2300_INPUTS) $(MQUICKJS_INPUTS) $(JS2300_CONFIG_STAMP)
	@echo "  JS2300  runtime"
	$(Q)$(MAKE) -C $(JS2300) TOOLCHAIN=$(TOOLCHAIN) \
		CROSS_COMPILE=$(CROSS_COMPILE) MQUICKJS_DIR=$(abspath $(MQUICKJS_DIR))
	$(Q)test -s $@ && touch $@

define CORE_ARCHIVE_RULE
$(if $(filter 1,$(CORE_BATCH_BUILD)),$(1): $(CORE_ARCHIVE_STAMP),$(1): $(2) $(3))
	@echo "  CORE    $(4)"
ifeq ($(CORE_BATCH_BUILD),1)
	$(Q)test -s $$@ && touch $$@
else
	$(Q)$(MAKE) -C $(CORES) $(4) $(CORE_MAKE_ARGS)
	$(Q)test -s $$@ && touch $$@
endif
endef

$(CORE_ARCHIVE_STAMP): $(CORE_BUILD_DEPS) | $(BUILD)
	@echo "  CORE    all"
	$(Q)$(MAKE) -C $(CORES) all $(CORE_MAKE_ARGS)
	$(Q)touch $@

$(foreach var,$(LIBRETRO_CORE_VARS),$(eval $(call CORE_ARCHIVE_RULE,$($(var)_CORE_LIB),$($(var)_BUILD_DEPS),$($(var)_CORE_SUPPORT_LIBS),$($(var)_CORE_TARGET))))

$(LIBRETRO_COMMON_LIB): $(LIBRETRO_COMMON_BUILD_DEPS)
	@echo "  CORELIB libretro-common"
	$(Q)$(MAKE) -C $(CORES) libretro-common $(CORE_MAKE_ARGS)
	$(Q)test -s $@ && touch $@

$(CHD_SUPPORT_CORE_LIB): $(CHD_SUPPORT_BUILD_DEPS)
	@echo "  CORELIB libchdr-support"
	$(Q)$(MAKE) -C $(CORES) chd-support $(CORE_MAKE_ARGS)
	$(Q)test -s $@ && touch $@

$(OUT)/$(TARGET).bin: $(OUT)/$(TARGET).out
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(ASDPACK): tools/asdpack.c $(BUILD_CONFIG_STAMP) | $(BUILD)
	@echo "  HOSTCC  $<"
	$(Q)$(HOSTCC) $(HOSTCFLAGS) $< -o $@

$(ASD): $(OUT)/$(TARGET).bin $(ASDPACK)
	@echo "  PACK    $@"
	$(Q)$(ASDPACK) $< $@

$(OUT)/unifrog.bin: $(OUT)/$(TARGET).bin | $(OUT)
	@echo "  COPY    $@"
	$(Q)if test -f $@ && cmp -s $< $@; then touch $@; else cp $< $@; fi

$(FASTBOOT_ASD): $(FASTBOOT_STUB_BIN) $(ASDPACK)
	@echo "  PACK    $@"
	$(Q)$(ASDPACK) $< $@

dtb: $(DTB)

lib: $(LIBUNIFROG)

asdcheck: $(ASD)
	@echo "  CHECK   $(ASD)"
	$(Q)$(ASDPACK) --check $(ASD)

fastboot: $(FASTBOOT_ASD) $(OUT)/unifrog.bin

fastboot-only: $(FASTBOOT_ASD)

fastboot-only-check: fastboot-only
	@test -s $(FASTBOOT_ASD)
	@echo "  CHECK   $(FASTBOOT_ASD)"
	$(Q)$(ASDPACK) --check $(FASTBOOT_ASD)
	@echo "  OK      $(FASTBOOT_ASD)"

fastboot-check: sdcard-package
	@test -s $(FASTBOOT_ASD)
	@test -s $(OUT)/unifrog.bin
	@test -s $(SDCARD_BIOS_PACKAGE)
	@test -s $(SDCARD_FIRMWARE_PACKAGE)
	@test -s $(FRONTEND_MANIFEST)
	@test -s $(FRONTEND_PACKAGE)/LICENSE.txt
	@test -s $(FRONTEND_PACKAGE)/THIRD_PARTY.md
	@for bin in $(LIBRETRO_CORE_BINS); do test -s $$bin || exit 1; done
	@test -z "$(HCRTOS_MEDIA_MODULE_BINS)" || for bin in $(HCRTOS_MEDIA_MODULE_BINS); do test -s $$bin || exit 1; done
	@echo "  CHECK   $(FASTBOOT_ASD)"
	$(Q)$(ASDPACK) --check $(FASTBOOT_ASD)
	@echo "  OK      $(FASTBOOT_ASD)"

fastboot-check: layout-check

layout-check: $(OUT)/$(TARGET).out $(LIBRETRO_CORE_MODULE_OUTS) $(NATIVE_MODULE_OUTS)
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

check: sdk $(ASD) sdcard-package layout-check
	@test -s $(ASD)
	@test -s $(OUT)/unifrog.bin
	@test -s $(SDCARD_BIOS_PACKAGE)
	@test -s $(SDCARD_FIRMWARE_PACKAGE)
	@test -s $(LIBUNIFROG)
	@test -s $(FRONTEND_MANIFEST)
	@test -s $(FRONTEND_PACKAGE)/LICENSE.txt
	@test -s $(FRONTEND_PACKAGE)/THIRD_PARTY.md
	@for bin in $(LIBRETRO_CORE_BINS); do test -s $$bin || exit 1; done
	@test -z "$(HCRTOS_MEDIA_MODULE_BINS)" || for bin in $(HCRTOS_MEDIA_MODULE_BINS); do test -s $$bin || exit 1; done
	@echo "  CHECK   $(ASD)"
	$(Q)$(ASDPACK) --check $(ASD)
	@echo "  CHECK   $(OUT)/unifrog.bin"
	$(Q)test -s $(OUT)/unifrog.bin
	@echo "  OK      $(ASD)"

size: $(ASD) module-package
	@ls -lh $(ASD) $(OUT)/$(TARGET).bin $(OUT)/$(TARGET).out $(LIBUNIFROG) $(DTB) $(HCRTOS_MEDIA_MODULE_BINS)

install: fastboot-check layout-check
	@echo "  CLEAN   stale root firmware files"
	$(Q)rm -f $(SDCARD)/$(ASD) $(SDCARD)/$(FASTBOOT_ASD) $(SDCARD)/unifrog.bin \
		$(SDCARD)/firmware/unifrog.bin
	$(Q)rmdir $(SDCARD)/firmware 2>/dev/null || true
	@echo "  INSTALL $(SDCARD_BIOS_DIR)/bisrv.asd"
	$(Q)mkdir -p $(SDCARD_BIOS_DIR)
	$(Q)cp $(FASTBOOT_ASD) $(SDCARD_BIOS_DIR)/bisrv.asd
	@echo "  INSTALL $(SDCARD_FIRMWARE_DIR)/unifrog.bin"
	$(Q)mkdir -p $(SDCARD_FIRMWARE_DIR)
	$(Q)cp $(OUT)/unifrog.bin $(SDCARD_FIRMWARE_DIR)/unifrog.bin
	@echo "  INSTALL $(SDCARD)/unifrog"
	$(Q)mkdir -p $(SDCARD)/unifrog
	$(Q)rm -rf $(SDCARD)/unifrog/cores $(SDCARD)/unifrog/modules
	$(Q)rm -rf $(SDCARD)/unifrog/app $(SDCARD)/unifrog/main.js \
		$(SDCARD)/unifrog/main.js.mqbc \
		$(SDCARD)/unifrog/bytecode-manifest.txt
	$(Q)cp -R $(FRONTEND_PACKAGE)/. $(SDCARD)/unifrog/
	$(Q)cp LICENSE $(SDCARD)/unifrog/LICENSE.txt
	$(Q)cp $(THIRD_PARTY_NOTICE) $(SDCARD)/unifrog/THIRD_PARTY.md
	@echo "  INSTALL $(SDCARD_USER_DIR)"
	$(Q)mkdir -p $(SDCARD_USER_DIR)/saves $(SDCARD_USER_DIR)/cache \
		$(SDCARD_USER_DIR)/logs $(SDCARD_USER_DIR)/logs/crashlogs \
		$(SDCARD_USER_DIR)/logs/rotatedlogs $(SDCARD_USER_DIR)/logs/reports \
		$(SDCARD_USER_DIR)/updates $(SDCARD_USER_DIR)/versions \
		$(SDCARD_USER_DIR)/themes $(SDCARD_USER_DIR)/languages \
		$(SDCARD_USER_DIR)/archive $(SDCARD_USER_DIR)/scripts \
		$(SDCARD_USER_DIR)/firmware
	$(Q)if test -n "$(LANGUAGE_FILES)"; then cp $(LANGUAGE_FILES) $(SDCARD_USER_DIR)/languages/; fi
	$(Q)if test -n "$(SCRIPT_FILES)"; then \
		for script in $(SCRIPT_FILES); do \
			target="$(SDCARD_USER_DIR)/$$script"; \
			mkdir -p "$$(dirname "$$target")"; \
			cp "$$script" "$$target"; \
		done; \
	fi
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

sd-zip: sdcard-package
	@command -v zip >/dev/null || { echo "missing: zip"; exit 1; }
	@echo "  ZIP     $(SDZIP)"
	$(Q)mkdir -p $(dir $(SDZIP))
	$(Q)rm -f $(SDZIP)
	$(Q)cd $(OUT)/sdcard && zip $(ZIP_COMPRESSION) -r $(abspath $(SDZIP)) . >/dev/null

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

DEPFILES := $(shell test ! -d $(BUILD) || find $(BUILD) -type f -name '*.d')
-include $(DEPFILES)
