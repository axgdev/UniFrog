TOOLCHAIN_VERSION ?= 1.3.2
TOOLCHAIN_UNAME_M := $(shell uname -m)
TOOLCHAIN_HOST_ARCH := $(if $(filter aarch64 arm64,$(TOOLCHAIN_UNAME_M)),arm64,$(if $(filter x86_64 amd64,$(TOOLCHAIN_UNAME_M)),x86_64,$(TOOLCHAIN_UNAME_M)))
TOOLCHAIN_ROOT ?= $(abspath ../.deps/frog-toolchain-v$(TOOLCHAIN_VERSION)-$(TOOLCHAIN_HOST_ARCH))
TOOLCHAIN ?= $(TOOLCHAIN_ROOT)/mipsel-mti-elf
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
SDK ?= ../unifrog-hcrtos-sdk
JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
CCACHE ?=
CCACHE_PREFIX := $(if $(CCACHE),$(CCACHE) )
Q := $(if $(V),,@)
BUILD_PROGRESS ?= $(if $(V),1,0)
LOG_ECHO := $(if $(filter 1,$(BUILD_PROGRESS)),@echo,@:)
ifeq ($(V),)
MAKEFLAGS += --silent
endif
PIN_MODE ?= $(if $(MODE),$(MODE),policy)
DEP_CHECKOUT ?= sparse
DEP_DEPTH ?= 1
DEP_GIT_ENV ?= GIT_TERMINAL_PROMPT=0 GIT_SSH_COMMAND='ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new'
CORE_IDS ?=
DEP ?=
DEV_CORE_MAKEFILE ?= Makefile.libretro
DEV_CORE_TARGET ?= $(CORE)_libretro_sf2000.a
DEV_CORE_ARGS ?= platform=unix CC="$(CC)" CXX="$(CXX)" AR=$(AR) STATIC_LINKING=1 fpic= TARGET=$(DEV_CORE_TARGET)

include manifest.mk

.SUFFIXES:

OUT := output
CORE ?=
DEP_EFFECTIVE := $(strip $(DEP))
dep_id_to_target = $(if $(filter gpsp-gbac-prosty,$(1)),gpsp_multicore,$(1))
DEP_TARGET := $(call dep_id_to_target,$(DEP_EFFECTIVE))
LIBRETRO_COMMON_DIR := $(CORE_SOURCE_ROOT)/libretro-common
GAMBATTE_DIR := $(CORE_SOURCE_ROOT)/gambatte-libretro
GAMBATTE_LIB := $(GAMBATTE_DIR)/gambatte_libretro_sf2000.a
GPSP_DIR := $(CORE_SOURCE_ROOT)/gpsp-libretro
GPSP_LIB := $(GPSP_DIR)/gpsp_libretro_sf2000.a
GPSP_MULTICORE_DIR := $(CORE_SOURCE_ROOT)/gpsp_multicore
GPSP_MULTICORE_LIB := $(GPSP_MULTICORE_DIR)/gpsp_multicore_libretro_sf2000.a
PICODRIVE_DIR := $(CORE_SOURCE_ROOT)/picodrive
PICODRIVE_LIB := $(PICODRIVE_DIR)/picodrive_libretro_sf2000.a
LIBCHDR_DIR := $(CORE_SUPPORT_ROOT)/libchdr
ZSTD_DIR := $(CORE_SUPPORT_ROOT)/zstd/lib
ZLIB_DIR := $(CORE_SUPPORT_ROOT)/zlib
LZMA_DIR := $(LIBCHDR_DIR)/deps/lzma-25.01
CHD_SUPPORT_LIB := $(OUT)/libchdr-support-sf2000.a
SNES9X2005_DIR := $(CORE_SOURCE_ROOT)/snes9x2005
SNES9X2005_LIB := $(SNES9X2005_DIR)/snes9x2005_libretro_sf2000.a
SNES9X2002_DIR := $(CORE_SOURCE_ROOT)/snes9x2002
SNES9X2002_LIB := $(SNES9X2002_DIR)/snes9x2002_libretro_sf2000.a
QUICKNES_DIR := $(CORE_SOURCE_ROOT)/QuickNES_Core
QUICKNES_LIB := $(QUICKNES_DIR)/quicknes_libretro_sf2000.a
FCEUMM_DIR := $(CORE_SOURCE_ROOT)/libretro-fceumm
FCEUMM_LIB := $(FCEUMM_DIR)/fceumm_libretro_sf2000.a
GEARBOY_ROOT := $(CORE_SOURCE_ROOT)/Gearboy
GEARBOY_DIR := $(GEARBOY_ROOT)/platforms/libretro
GEARBOY_LIB := $(GEARBOY_DIR)/gearboy_libretro_sf2000.a
PCE_FAST_DIR := $(CORE_SOURCE_ROOT)/beetle-pce-fast-libretro
PCE_FAST_LIB := $(PCE_FAST_DIR)/pce_fast_libretro_sf2000.a
QPSX_DIR := $(CORE_SOURCE_ROOT)/sf2000-qpsx-playstation-emulator
QPSX_LIB := $(QPSX_DIR)/pcsx4all_libretro_sf2000.a
QPSX_PLATFORM ?= unifrog
ifneq ($(filter unifrog linux,$(QPSX_PLATFORM)),$(QPSX_PLATFORM))
$(error QPSX_PLATFORM must be unifrog or linux (got $(QPSX_PLATFORM)))
endif
FROGUI_DIR := $(CORE_SOURCE_ROOT)/FrogUI
FROGUI_LIB := $(OUT)/frogui_libretro_sf2000.a
LIBRETRO_COMMON := $(OUT)/libretro-common-sf2000.a
BUILD := build
CC := $(CCACHE_PREFIX)$(CROSS_COMPILE)gcc
CXX := $(CCACHE_PREFIX)$(CROSS_COMPILE)g++
AR := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm

copy_core_lib = cp $(1) $(OUT)/ && rm -f $(1)

MIPS_CORE_CFLAGS := -EL -march=mips32 -mtune=mips32 -Os -pipe \
	-msoft-float -fsigned-char -G0 -mno-abicalls -fno-pic \
	-ffast-math -fomit-frame-pointer \
	-ffunction-sections -fdata-sections \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-DSF2000

COMMON_CFLAGS := $(MIPS_CORE_CFLAGS) \
	-I$(LIBRETRO_COMMON_DIR)/include \
	-I$(LIBRETRO_COMMON_DIR)/include/compat/zlib \
	-isystem $(SDK)/include \
	-isystem $(SDK)/include/hcrtos \
	-isystem $(SDK)/include/newlib

LIBRETRO_API_SYMBOLS := \
	retro_set_environment \
	retro_set_video_refresh \
	retro_set_audio_sample \
	retro_set_audio_sample_batch \
	retro_set_input_poll \
	retro_set_input_state \
	retro_init \
	retro_deinit \
	retro_api_version \
	retro_get_system_info \
	retro_get_system_av_info \
	retro_set_controller_port_device \
	retro_reset \
	retro_run \
	retro_unload_game \
	retro_load_game \
	retro_get_region \
	retro_load_game_special \
	retro_serialize_size \
	retro_serialize \
	retro_unserialize \
	retro_get_memory_data \
	retro_get_memory_size \
	retro_cheat_reset \
	retro_cheat_set

libretro_static_rename_cflags = \
	$(foreach symbol,$(LIBRETRO_API_SYMBOLS),-D$(symbol)=$(1)_$(symbol))
libretro_static_rename_objcopy = \
	$(foreach symbol,$(LIBRETRO_API_SYMBOLS),--redefine-sym $(symbol)=$(1)_$(symbol))

GPSP_RENAME_CFLAGS := $(call libretro_static_rename_cflags,gpsp)
GPSP_MULTICORE_RENAME_CFLAGS := $(call libretro_static_rename_cflags,gpsp_multicore)
PICODRIVE_RENAME_CFLAGS := $(call libretro_static_rename_cflags,picodrive)
SNES9X2005_RENAME_CFLAGS := $(call libretro_static_rename_cflags,snes9x2005)
SNES9X2002_RENAME_CFLAGS := $(call libretro_static_rename_cflags,snes9x2002)
QUICKNES_RENAME_CFLAGS := $(call libretro_static_rename_cflags,quicknes)
FCEUMM_RENAME_CFLAGS := $(call libretro_static_rename_cflags,fceumm)
GEARBOY_RENAME_CFLAGS := $(call libretro_static_rename_cflags,gearboy)
PCE_FAST_RENAME_CFLAGS := $(call libretro_static_rename_cflags,pce_fast)
QPSX_RENAME_CFLAGS := $(call libretro_static_rename_cflags,qpsx)
FROGUI_RENAME_CFLAGS := $(call libretro_static_rename_cflags,frogui)

CORE_BASE_CFLAGS := $(MIPS_CORE_CFLAGS) \
	-isystem $(abspath $(SDK))/include \
	-isystem $(abspath $(SDK))/include/hcrtos \
	-isystem $(abspath $(SDK))/include/newlib

PICODRIVE_CFLAGS := $(CORE_BASE_CFLAGS) $(PICODRIVE_RENAME_CFLAGS) \
	-Wno-stringop-overflow
SNES9X2005_CFLAGS := $(CORE_BASE_CFLAGS) $(SNES9X2005_RENAME_CFLAGS) \
	-Wno-array-bounds -Wno-char-subscripts -Wno-format
SNES9X2002_CFLAGS := $(CORE_BASE_CFLAGS) $(SNES9X2002_RENAME_CFLAGS) \
	-Wno-array-bounds -Wno-char-subscripts -Wno-format \
	-Wno-implicit-fallthrough -Wno-maybe-uninitialized -Wno-restrict \
	-Wno-shift-negative-value -Wno-unused-value
QUICKNES_CFLAGS := $(CORE_BASE_CFLAGS) $(QUICKNES_RENAME_CFLAGS)
FCEUMM_CFLAGS := $(CORE_BASE_CFLAGS) $(FCEUMM_RENAME_CFLAGS)
GEARBOY_CFLAGS := $(CORE_BASE_CFLAGS) $(GEARBOY_RENAME_CFLAGS) -DMINIZ_NO_TIME \
	-Wno-misleading-indentation -Wno-unused-function
PCE_FAST_CFLAGS := $(CORE_BASE_CFLAGS) $(PCE_FAST_RENAME_CFLAGS)
QPSX_CFLAGS := $(CORE_BASE_CFLAGS) $(QPSX_RENAME_CFLAGS) \
	-Wno-error=incompatible-pointer-types \
	-Wno-error=implicit-function-declaration \
	-Wno-error=int-conversion \
	-Wno-cpp \
	-Wno-incompatible-pointer-types \
	-Wno-implicit-function-declaration \
	-Wno-int-conversion
QPSX_CXXFLAGS := $(CORE_BASE_CFLAGS) $(QPSX_RENAME_CFLAGS) -Wno-cpp
FROGUI_CFLAGS := $(CORE_BASE_CFLAGS) $(FROGUI_RENAME_CFLAGS) \
	-DSCREEN_WIDTH=320 -DSCREEN_HEIGHT=240 -DUI_SCALE=75 \
	-I$(abspath ../include) -I$(FROGUI_DIR) \
	-Wno-unused-function -Wno-unused-variable
GPSP_CFLAGS := $(MIPS_CORE_CFLAGS) -DROM_BUFFER_SIZE=32 \
	-DSMALL_TRANSLATION_CACHE \
	$(GPSP_RENAME_CFLAGS)
GPSP_MULTICORE_CFLAGS := $(MIPS_CORE_CFLAGS) -DROM_BUFFER_SIZE=32 \
	-DSMALL_TRANSLATION_CACHE \
	$(GPSP_MULTICORE_RENAME_CFLAGS)

# Use gpSP's stock platform=unix path and override only the cross-toolchain,
# source list, and static-link flags here. This keeps platform plumbing out of
# the upstream core branch; the bundled VFS source is excluded because HCRTOS
# newlib does not provide host directory APIs.
GPSP_SOURCES_C := \
	./main.c \
	./gba_memory.c \
	./savestate.c \
	./input.c \
	./sound.c \
	./cheats.c \
	./memmap.c \
	./serial.c \
	./gbp.c \
	./rfu.c \
	./serial_proto.c \
	./libretro/libretro.c \
	./gba_cc_lut.c \
	./cpu_threaded.c
GPSP_MULTICORE_SOURCES_C := $(filter-out ./serial_proto.c,$(GPSP_SOURCES_C))

GPSP_SOURCES_ASM := ./bios_data.S ./mips/mips_stub.S
GPSP_SOURCES_CC := ./video.cc ./cpu.cc

GPSP_MAKE_ARGS := platform=unix \
	MIPS=$(CROSS_COMPILE) \
	CC="$(CC)" \
	CXX="$(CXX)" \
	AR=$(CROSS_COMPILE)ar \
	STATIC_LINKING=1 \
	HAVE_DYNAREC=1 \
	CPU_ARCH=mips \
	MMAP_JIT_CACHE=0 \
	FORCE_32BIT_ARCH=0 \
	FORCE_32BIT= \
	fpic= \
	TARGET=gpsp_libretro_sf2000.a \
	OPTIMIZE="-Os -DNDEBUG" \
	CPU_THREADED_OPTIMIZE="-Os -DNDEBUG -fno-expensive-optimizations" \
	SOURCES_C="$(GPSP_SOURCES_C)" \
	SOURCES_ASM="$(GPSP_SOURCES_ASM)" \
	SOURCES_CC="$(GPSP_SOURCES_CC)"
GPSP_MULTICORE_MAKE_ARGS := $(filter-out TARGET=gpsp_libretro_sf2000.a,$(GPSP_MAKE_ARGS)) \
	SOURCES_C="$(GPSP_MULTICORE_SOURCES_C)" \
	TARGET=gpsp_multicore_libretro_sf2000.a

LIBRETRO_COMMON_OBJECTS := \
	$(BUILD)/libretro-common/compat/compat_posix_string.o \
	$(BUILD)/libretro-common/compat/compat_snprintf.o \
	$(BUILD)/libretro-common/compat/compat_strcasestr.o \
	$(BUILD)/libretro-common/compat/compat_strl.o \
	$(BUILD)/libretro-common/encodings/encoding_crc32.o \
	$(BUILD)/libretro-common/compat/fopen_utf8.o \
	$(BUILD)/libretro-common/encodings/encoding_utf.o \
	$(BUILD)/libretro-common/file/file_path.o \
	$(BUILD)/libretro-common/file/file_path_io.o \
	$(BUILD)/libretro-common/formats/png/rpng.o \
	$(BUILD)/libretro-common/libco/libco.o \
	$(BUILD)/libretro-common/streams/file_stream.o \
	$(BUILD)/libretro-common/streams/file_stream_transforms.o \
	$(BUILD)/libretro-common/streams/memory_stream.o \
	$(BUILD)/libretro-common/streams/trans_stream.o \
	$(BUILD)/libretro-common/streams/trans_stream_pipe.o \
	$(BUILD)/libretro-common/string/stdstring.o \
	$(BUILD)/libretro-common/time/rtime.o \
	$(BUILD)/libretro-common/vfs/vfs_implementation.o

CHD_SUPPORT_INCFLAGS := \
	-I$(abspath $(LIBCHDR_DIR))/include \
	-I$(abspath $(LZMA_DIR))/include \
	-I$(abspath $(ZSTD_DIR)) \
	-I$(abspath $(ZLIB_DIR))

CHD_SUPPORT_CFLAGS := $(CORE_BASE_CFLAGS) $(CHD_SUPPORT_INCFLAGS) \
	-DZ7_ST -DZSTD_DISABLE_ASM -DCHDR_SYSTEM_ZLIB -DCHDR_SYSTEM_ZSTD \
	-Wno-unused -Wno-old-style-definition

CHD_SUPPORT_OBJECTS := \
	$(BUILD)/chd-support/libchdr_bitstream.o \
	$(BUILD)/chd-support/libchdr_cdrom.o \
	$(BUILD)/chd-support/libchdr_chd.o \
	$(BUILD)/chd-support/libchdr_codec_cdfl.o \
	$(BUILD)/chd-support/libchdr_codec_cdlz.o \
	$(BUILD)/chd-support/libchdr_codec_cdzl.o \
	$(BUILD)/chd-support/libchdr_codec_cdzs.o \
	$(BUILD)/chd-support/libchdr_codec_flac.o \
	$(BUILD)/chd-support/libchdr_codec_huff.o \
	$(BUILD)/chd-support/libchdr_codec_lzma.o \
	$(BUILD)/chd-support/libchdr_codec_zlib.o \
	$(BUILD)/chd-support/libchdr_codec_zstd.o \
	$(BUILD)/chd-support/libchdr_flac.o \
	$(BUILD)/chd-support/libchdr_huffman.o \
	$(BUILD)/chd-support/lzma_LzmaDec.o \
	$(BUILD)/chd-support/zstd_entropy_common.o \
	$(BUILD)/chd-support/zstd_error_private.o \
	$(BUILD)/chd-support/zstd_fse_decompress.o \
	$(BUILD)/chd-support/zstd_huf_decompress.o \
	$(BUILD)/chd-support/zstd_xxhash.o \
	$(BUILD)/chd-support/zstd_zstd_common.o \
	$(BUILD)/chd-support/zstd_zstd_ddict.o \
	$(BUILD)/chd-support/zstd_zstd_decompress.o \
	$(BUILD)/chd-support/zstd_zstd_decompress_block.o \
	$(BUILD)/chd-support/sf2000_fcntl.o \
	$(BUILD)/chd-support/zlib_adler32.o \
	$(BUILD)/chd-support/zlib_compress.o \
	$(BUILD)/chd-support/zlib_crc32.o \
	$(BUILD)/chd-support/zlib_deflate.o \
	$(BUILD)/chd-support/zlib_gzclose.o \
	$(BUILD)/chd-support/zlib_gzlib.o \
	$(BUILD)/chd-support/zlib_gzread.o \
	$(BUILD)/chd-support/zlib_gzwrite.o \
	$(BUILD)/chd-support/zlib_inffast.o \
	$(BUILD)/chd-support/zlib_inflate.o \
	$(BUILD)/chd-support/zlib_inftrees.o \
	$(BUILD)/chd-support/zlib_trees.o \
	$(BUILD)/chd-support/zlib_uncompr.o \
	$(BUILD)/chd-support/zlib_zutil.o

FROGUI_OBJECTS := \
	$(BUILD)/frogui/frogui_libretro.o \
	$(BUILD)/frogui/font.o \
	$(BUILD)/frogui/render.o \
	$(BUILD)/frogui/theme.o

# target:source-directory:makefile:upstream-archive:package-archive:symbol-prefix
SF2000_PORT_SPECS := \
	mame2000:$(CORE_SOURCE_ROOT)/libretro-mame2000:Makefile:mame2000_libretro_sf2000.a:mame2000_libretro_sf2000.a:mame2000 \
	fbalpha2012:$(CORE_SOURCE_ROOT)/fbalpha2012/svn-current/trunk:makefile.libretro:fbalpha2012_libretro_sf2000.a:fbalpha2012_libretro_sf2000.a:fbalpha2012 \
	stella2014:$(CORE_SOURCE_ROOT)/libretro-stella2014:Makefile:stella2014_libretro_sf2000.a:stella2014_libretro_sf2000.a:stella2014 \
	a5200:$(CORE_SOURCE_ROOT)/a5200:Makefile:a5200_libretro_sf2000.a:a5200_libretro_sf2000.a:a5200 \
	prosystem:$(CORE_SOURCE_ROOT)/libretro-prosystem:Makefile:prosystem_libretro_sf2000.a:prosystem_libretro_sf2000.a:prosystem \
	atari800lib:$(CORE_SOURCE_ROOT)/libretro-atari800lib:Makefile:libatari800_libretro_sf2000.a:atari800lib_libretro_sf2000.a:atari800lib \
	handy:$(CORE_SOURCE_ROOT)/libretro-handy:Makefile:handy_libretro_sf2000.a:handy_libretro_sf2000.a:handy \
	race:$(CORE_SOURCE_ROOT)/RACE:Makefile:race_libretro_sf2000.a:race_libretro_sf2000.a:race \
	beetle-cygne:$(CORE_SOURCE_ROOT)/libretro-beetle-wswan:Makefile:mednafen_wswan_libretro_sf2000.a:beetle_cygne_libretro_sf2000.a:beetle_cygne \
	gearcoleco:$(CORE_SOURCE_ROOT)/Gearcoleco/platforms/libretro:Makefile:gearcoleco_libretro_sf2000.a:gearcoleco_libretro_sf2000.a:gearcoleco \
	snes9x2005-prosty:$(CORE_SOURCE_ROOT)/snes9x2005-prosty:Makefile:snes9x2005_libretro_sf2000.a:snes9x2005_prosty_libretro_sf2000.a:snes9x2005_prosty \
	snes9x2002-prosty:$(CORE_SOURCE_ROOT)/snes9x2002-prosty:Makefile:snes9x2002_libretro_sf2000.a:snes9x2002_prosty_libretro_sf2000.a:snes9x2002_prosty \
	gambatte-prosty:$(CORE_SOURCE_ROOT)/libretro-gambatte-prosty:Makefile.libretro:gambatte_libretro_sf2000.a:gambatte_prosty_libretro_sf2000.a:gambatte_prosty \
	quicknes-prosty:$(CORE_SOURCE_ROOT)/QuickNES_Core-prosty:Makefile:quicknes_libretro_sf2000.a:quicknes_prosty_libretro_sf2000.a:quicknes_prosty \
	frodo-prosty:$(CORE_SOURCE_ROOT)/libretro-frodo-prosty:Makefile.libretro:frodo_libretro_sf2000.a:frodo_prosty_libretro_sf2000.a:frodo_prosty \
	fake08-prosty:$(CORE_SOURCE_ROOT)/fake-08-prosty/platform/libretro:Makefile:fake08_libretro_sf2000.a:fake08_prosty_libretro_sf2000.a:fake08_prosty \
	bluemsx-prosty:$(CORE_SOURCE_ROOT)/libretro-blueMSX-prosty:Makefile.libretro:bluemsx_libretro_sf2000.a:bluemsx_prosty_libretro_sf2000.a:bluemsx_prosty \
	fceumm-prosty:$(CORE_SOURCE_ROOT)/libretro-fceumm-prosty:Makefile.libretro:fceumm_libretro_sf2000.a:fceumm_prosty_libretro_sf2000.a:fceumm_prosty

SF2000_PORT_TARGETS := $(foreach spec,$(SF2000_PORT_SPECS),$(word 1,$(subst :, ,$(spec))))

define SF2000_PORT_REGISTER
$(1)_PORT_DIR := $(2)
$(1)_PORT_MAKEFILE := $(3)
$(1)_PORT_SOURCE_ARCHIVE := $(2)/$(4)
$(1)_PORT_OUTPUT_ARCHIVE := $(OUT)/$(5)
$(1)_PORT_PREFIX := $(6)
endef

$(foreach spec,$(SF2000_PORT_SPECS),$(eval $(call SF2000_PORT_REGISTER,$(word 1,$(subst :, ,$(spec))),$(word 2,$(subst :, ,$(spec))),$(word 3,$(subst :, ,$(spec))),$(word 4,$(subst :, ,$(spec))),$(word 5,$(subst :, ,$(spec))),$(word 6,$(subst :, ,$(spec))))))

atari800lib_PORT_ARGS := A5200=0
fbalpha2012_PORT_ARGS := PROFILE=performance
frodo-prosty_PORT_ARGS := EMUTYPE=frodo
snes9x2005-prosty_PORT_ARGS := USE_BLARGG_APU=0

CORE_TARGETS := gambatte gpsp gpsp_multicore picodrive snes9x2005 snes9x2002 \
	quicknes fceumm gearboy pce-fast qpsx frogui $(SF2000_PORT_TARGETS)
SUPPORT_TARGETS := libretro-common chd-support
SMOKE_TARGETS := libretro-common quicknes
MAINTAIN_TARGETS := support-init core-init init smoke-init doctor smoke-doctor pin-status \
	upgrade-pins \
	nested-patches-refresh \
	dep-status dep-update dep-edit dep-refresh dep-finalize dep-log dep-diff \
	dep-patches-check dev-core smoke-check check clean help print-config
core_id_to_target = $(if $(filter gpsp-gbac-prosty,$(1)),gpsp_multicore,$(1))
CORE_IDS_EFFECTIVE := $(strip $(if $(CORE_IDS),$(CORE_IDS),$(if $(filter $(CORE_TARGETS),$(MAKECMDGOALS)),$(filter $(CORE_TARGETS),$(MAKECMDGOALS)),$(DEFAULT_CORE_IDS))))
REQUESTED_CORE_TARGETS := $(foreach id,$(CORE_IDS_EFFECTIVE),$(call core_id_to_target,$(id)))
UNKNOWN_CORE_IDS := $(strip $(filter-out $(CORE_TARGETS),$(REQUESTED_CORE_TARGETS)))
ifneq ($(UNKNOWN_CORE_IDS),)
$(error unknown CORE_IDS='$(UNKNOWN_CORE_IDS)'; supported cores: $(DEFAULT_CORE_IDS))
endif
REQUESTED_SUPPORT_TARGETS := libretro-common $(if $(filter picodrive pce-fast,$(REQUESTED_CORE_TARGETS)),chd-support)
REQUESTED_SUPPORT_DEPS := lz4 stb nanosvg zstd zlib \
	$(if $(filter chd-support,$(REQUESTED_SUPPORT_TARGETS)),libchdr)

.PHONY: all $(CORE_TARGETS) $(SUPPORT_TARGETS) $(MAINTAIN_TARGETS)

all: $(REQUESTED_SUPPORT_TARGETS) $(REQUESTED_CORE_TARGETS)

help:
	@echo "UniFrog core build:"
	@echo "  make -C cores init                  Fetch pinned upstream sources and apply patches"
	@echo "  make -C cores core-init             Fetch only core sources and patches"
	@echo "  make -C cores init CORE_IDS=\"gpsp gambatte\""
	@echo "                                      Fetch selected cores plus required support"
	@echo "  make -C cores                       Build all core archives"
	@echo "  make -C cores CORE_IDS=\"gpsp gambatte\""
	@echo "                                      Build selected core archives"
	@echo "  make -C cores smoke-check           Build libretro-common and QuickNES"
	@echo "  make -C cores check                 Verify clean source checkouts"
	@echo "  make -C cores clean                 Remove core build outputs"
	@echo ""
	@echo "Focused builds:"
	@echo "  make -C cores gpsp"
	@echo "  make -C cores gpsp_multicore"
	@echo "  make -C cores pce-fast"
	@echo ""
	@echo "Dependency maintenance:"
	@echo "  make -C cores dep-status DEP=gpsp"
	@echo "  make -C cores dep-edit DEP=gpsp"
	@echo "  make -C cores dep-update DEP=gpsp REF=<new-ref>"
	@echo "  make -C cores dep-refresh DEP=gpsp"
	@echo "  make -C cores nested-patches-refresh CORE=fake08-prosty NESTED=libs/z8lua"
	@echo "  make -C cores dep-finalize DEP=gpsp REF=<new-ref>"
	@echo "  make -C cores dep-log DEP=gpsp"
	@echo "  make -C cores dep-diff DEP=gpsp"
	@echo "  make -C cores dep-patches-check     Reapply patch queues"
	@echo "  make -C cores pin-status            Compare pins using manifest policy"
	@echo "  make -C cores upgrade-pins          Update pins using manifest policy"
	@echo "  make -C cores pin-status MODE=head  Override policy for one check"
	@echo "  make -C cores dev-core CORE=mycore CORE_DIR=/work/mycore"
	@echo ""
	@echo "Config:"
	@echo "  make -C cores print-config"

print-config:
	@echo "TOOLCHAIN=$(TOOLCHAIN)"
	@echo "SDK=$(SDK)"
	@echo "CORE_SOURCE_ROOT=$(CORE_SOURCE_ROOT)"
	@echo "CORE_SUPPORT_ROOT=$(CORE_SUPPORT_ROOT)"
	@echo "DEP_CHECKOUT=$(DEP_CHECKOUT)"
	@echo "DEP_DEPTH=$(DEP_DEPTH)"
	@echo "CORE_IDS=$(CORE_IDS)"
	@echo "CORE_IDS_EFFECTIVE=$(CORE_IDS_EFFECTIVE)"
	@echo "REQUESTED_CORE_TARGETS=$(REQUESTED_CORE_TARGETS)"
	@echo "REQUESTED_SUPPORT_TARGETS=$(REQUESTED_SUPPORT_TARGETS)"
	@echo "CCACHE=$(if $(CCACHE),$(CCACHE),disabled)"
	@echo "JOBS=$(JOBS)"

support-init:
	@set -e; \
	case "$(DEP_CHECKOUT)" in sparse|full) ;; *) echo "DEP_CHECKOUT must be sparse or full"; exit 1;; esac; \
	mkdir -p "$(CORE_SUPPORT_ROOT)"; \
	status=0; \
	pids=""; \
	for spec in $(SUPPORT_SPECS); do \
		( \
			set -e; \
			name=$${spec%%|*}; rest=$${spec#*|}; \
			dir=$${rest%%|*}; rest=$${rest#*|}; \
			url=$${rest%%|*}; rest=$${rest#*|}; \
			policy=$${rest%%|*}; rest=$${rest#*|}; \
			ref=$${rest%%|*}; rest=$${rest#*|}; \
			commit=$${rest%%|*}; rest=$${rest#*|}; \
			patch_dir=$${rest%%|*}; rest=$${rest#*|}; \
			sparse=$$rest; \
			if test "$$name" = libchdr; then \
				case " $(REQUESTED_SUPPORT_TARGETS) " in *" chd-support "*) ;; *) if test "$(BUILD_PROGRESS)" = 1; then echo "  SKIP    support/$$name CORE_IDS=$(CORE_IDS_EFFECTIVE)"; fi; exit 0;; esac; \
			fi; \
			fresh=0; \
			depth_arg=""; \
			filter_arg=""; \
			if test "$(DEP_DEPTH)" != 0; then depth_arg="--depth $(DEP_DEPTH)"; fi; \
			if test "$(DEP_CHECKOUT)" = sparse; then filter_arg="--filter=blob:none"; fi; \
			if test -d "$$dir/.git"; then \
				git -C "$$dir" remote set-url origin "$$url"; \
			else \
				if test "$(BUILD_PROGRESS)" = 1; then echo "  CLONE   support/$$name $$url"; fi; \
				rm -rf "$$dir"; \
				git init -q "$$dir"; \
				git -C "$$dir" remote add origin "$$url"; \
				fresh=1; \
			fi; \
			if git -C "$$dir" cat-file -e "$$commit^{commit}" 2>/dev/null; then \
				if test "$(BUILD_PROGRESS)" = 1; then echo "  REUSE   support/$$name $$commit"; fi; \
			else \
				if test "$(BUILD_PROGRESS)" = 1; then echo "  FETCH   support/$$name $$ref"; fi; \
				case "$$policy:$$ref" in \
				tag:*) $(DEP_GIT_ENV) git -C "$$dir" fetch $$depth_arg $$filter_arg origin "refs/tags/$$ref:refs/tags/$$ref" ;; \
				*) $(DEP_GIT_ENV) git -C "$$dir" fetch $$depth_arg $$filter_arg origin "$$commit" ;; \
				esac; \
			fi; \
			git -C "$$dir" cat-file -e "$$commit^{commit}" 2>/dev/null || { echo "missing fetched commit for support/$$name: $$commit"; exit 1; }; \
			if test "$(DEP_CHECKOUT)" = sparse; then \
				case "$$sparse" in \
				:files\ *) git -C "$$dir" sparse-checkout set --no-cone $${sparse#*:files } ;; \
				:root-files) git -C "$$dir" sparse-checkout init --no-cone; printf '/*\n!/*/\n' >"$$dir/.git/info/sparse-checkout" ;; \
				.) git -C "$$dir" sparse-checkout disable >/dev/null 2>&1 || true ;; \
				*) git -C "$$dir" sparse-checkout init --cone; git -C "$$dir" sparse-checkout set --skip-checks $$sparse ;; \
				esac; \
			else \
				git -C "$$dir" sparse-checkout disable >/dev/null 2>&1 || true; \
			fi; \
			git -C "$$dir" am --abort >/dev/null 2>&1 || true; \
			if test "$$fresh" -eq 0; then \
				git -C "$$dir" submodule deinit -f --all >/dev/null 2>&1 || true; \
				git -C "$$dir" reset --hard -q; \
				git -C "$$dir" clean -ffdx -q; \
			fi; \
			git -C "$$dir" checkout -q -B unifrog-base "$$commit" >/dev/null; \
			git -C "$$dir" reset --hard -q "$$commit"; \
			git -C "$$dir" checkout -q -B unifrog-current unifrog-base >/dev/null; \
			if test "$$patch_dir" != "-"; then \
				patch_abs="$(abspath .)/$$patch_dir"; \
				if ls "$$patch_abs"/*.patch >/dev/null 2>&1; then \
					git -C "$$dir" config user.name "UniFrog CI"; \
					git -C "$$dir" config user.email "ci@unifrog.local"; \
					am_err="$$dir/.git/unifrog-am.err"; \
					if ! git -C "$$dir" am -3 --keep-cr -q "$$patch_abs"/*.patch 2>"$$am_err"; then \
						cat "$$am_err" >&2; \
						exit 1; \
					fi; \
					grep -v '^warning: quoted CRLF detected$$' "$$am_err" >&2 || true; \
					rm -f "$$am_err"; \
				fi; \
			fi; \
		) & \
		pids="$$pids $$!"; \
	done; \
	for pid in $$pids; do \
		wait $$pid || status=$$?; \
	done; \
	exit $$status

init: support-init core-init

smoke-init:
	$(Q)$(MAKE) --no-print-directory support-init SUPPORT_SPECS="$(SMOKE_SUPPORT_SPECS)"
	$(Q)$(MAKE) --no-print-directory core-init CORE_SPECS="$(SMOKE_CORE_SPECS)"

core-init:
	@set -e; \
	case "$(DEP_CHECKOUT)" in sparse|full) ;; *) echo "DEP_CHECKOUT must be sparse or full"; exit 1;; esac; \
	mkdir -p "$(CORE_SOURCE_ROOT)"; \
	status=0; \
	pids=""; \
	for spec in $(CORE_SPECS); do \
		( \
			set -e; \
			name=$${spec%%|*}; rest=$${spec#*|}; \
			dir=$${rest%%|*}; rest=$${rest#*|}; \
			url=$${rest%%|*}; rest=$${rest#*|}; \
			policy=$${rest%%|*}; rest=$${rest#*|}; \
			ref=$${rest%%|*}; rest=$${rest#*|}; \
			commit=$${rest%%|*}; rest=$${rest#*|}; \
			patch_dir=$${rest%%|*}; rest=$${rest#*|}; \
			sparse=$$rest; \
			case " libretro-common $(REQUESTED_CORE_TARGETS) " in *" $$name "*) ;; *) if test "$(BUILD_PROGRESS)" = 1; then echo "  SKIP    $$name CORE_IDS=$(CORE_IDS_EFFECTIVE)"; fi; exit 0;; esac; \
			fresh=0; \
			depth_arg=""; \
			filter_arg=""; \
			if test "$(DEP_DEPTH)" != 0; then depth_arg="--depth $(DEP_DEPTH)"; fi; \
			if test "$(DEP_CHECKOUT)" = sparse; then filter_arg="--filter=blob:none"; fi; \
			if test -d "$$dir/.git"; then \
				git -C "$$dir" remote set-url origin "$$url"; \
			else \
				if test "$(BUILD_PROGRESS)" = 1; then echo "  CLONE   $$name $$url"; fi; \
				rm -rf "$$dir"; \
				git init -q "$$dir"; \
				git -C "$$dir" remote add origin "$$url"; \
				fresh=1; \
			fi; \
			if git -C "$$dir" cat-file -e "$$commit^{commit}" 2>/dev/null; then \
				if test "$(BUILD_PROGRESS)" = 1; then echo "  REUSE   $$name $$commit"; fi; \
			else \
				if test "$(BUILD_PROGRESS)" = 1; then echo "  FETCH   $$name $$ref"; fi; \
				case "$$policy:$$ref" in \
				tag:*) $(DEP_GIT_ENV) git -C "$$dir" fetch $$depth_arg $$filter_arg origin "refs/tags/$$ref:refs/tags/$$ref" ;; \
				*) $(DEP_GIT_ENV) git -C "$$dir" fetch $$depth_arg $$filter_arg origin "$$commit" ;; \
				esac; \
			fi; \
			git -C "$$dir" cat-file -e "$$commit^{commit}" 2>/dev/null || { echo "missing fetched commit for $$name: $$commit"; exit 1; }; \
			if test "$(DEP_CHECKOUT)" = sparse; then \
				case "$$sparse" in \
				:files\ *) git -C "$$dir" sparse-checkout set --no-cone $${sparse#*:files } ;; \
				.) git -C "$$dir" sparse-checkout disable >/dev/null 2>&1 || true ;; \
				*) git -C "$$dir" sparse-checkout init --cone; git -C "$$dir" sparse-checkout set --skip-checks $$sparse ;; \
				esac; \
			else \
				git -C "$$dir" sparse-checkout disable >/dev/null 2>&1 || true; \
			fi; \
			git -C "$$dir" am --abort >/dev/null 2>&1 || true; \
			if test "$$fresh" -eq 0; then \
				git -C "$$dir" submodule deinit -f --all >/dev/null 2>&1 || true; \
				git -C "$$dir" reset --hard -q; \
				git -C "$$dir" clean -ffdx -q; \
			fi; \
			git -C "$$dir" checkout -q -B unifrog-base "$$commit" >/dev/null; \
			git -C "$$dir" reset --hard -q "$$commit"; \
			git -C "$$dir" checkout -q -B unifrog-current unifrog-base >/dev/null; \
			if test "$$patch_dir" != "-"; then \
				patch_abs="$(abspath .)/$$patch_dir"; \
				if ls "$$patch_abs"/*.patch >/dev/null 2>&1; then \
					git -C "$$dir" config user.name "UniFrog CI"; \
					git -C "$$dir" config user.email "ci@unifrog.local"; \
					am_err="$$dir/.git/unifrog-am.err"; \
					if ! git -C "$$dir" am -3 --keep-cr -q "$$patch_abs"/*.patch 2>"$$am_err"; then \
						cat "$$am_err" >&2; \
						exit 1; \
					fi; \
					grep -v '^warning: quoted CRLF detected$$' "$$am_err" >&2 || true; \
					rm -f "$$am_err"; \
				fi; \
			fi; \
			if test "$$name" = picodrive; then \
				git -C "$$dir" submodule update --init --depth 1 --filter=blob:none --jobs "$(JOBS)" -- cpu/cyclone pico/sound/emu2413 platform/libpicofe; \
				dr_dir="$$dir/platform/common/dr_libs"; \
				dr_url=$$(git -C "$$dir" config -f .gitmodules submodule.platform/common/dr_libs.url); \
				dr_commit=$$(git -C "$$dir" rev-parse HEAD:platform/common/dr_libs); \
				if test -d "$$dr_dir/.git"; then \
					git -C "$$dr_dir" remote set-url origin "$$dr_url"; \
				else \
					rm -rf "$$dr_dir"; \
					git init -q "$$dr_dir"; \
					git -C "$$dr_dir" remote add origin "$$dr_url"; \
				fi; \
				if ! git -C "$$dr_dir" cat-file -e "$$dr_commit^{commit}" 2>/dev/null; then \
					$(DEP_GIT_ENV) git -C "$$dr_dir" fetch --depth 1 --filter=blob:none origin "$$dr_commit"; \
				fi; \
				git -C "$$dr_dir" sparse-checkout init --no-cone; \
				printf '/dr_mp3.h\n/LICENSE\n' >"$$dr_dir/.git/info/sparse-checkout"; \
				git -C "$$dr_dir" checkout -q "$$dr_commit"; \
				git -C "$$dr_dir" reset --hard -q "$$dr_commit"; \
				git -C "$$dr_dir" clean -fdx -q; \
			else \
				git -C "$$dir" submodule update --init --depth 1 --filter=blob:none --jobs "$(JOBS)"; \
			fi; \
			for nested_spec in $(NESTED_CORE_SPECS); do \
				parent=$${nested_spec%%|*}; nested_rest=$${nested_spec#*|}; \
				subpath=$${nested_rest%%|*}; nested_rest=$${nested_rest#*|}; \
				base=$${nested_rest%%|*}; patch_dir=$${nested_rest#*|}; \
				test "$$parent" = "$$name" || continue; \
				subdir="$$dir/$$subpath"; \
				git -C "$$subdir" cat-file -e "$$base^{commit}" 2>/dev/null || { \
					echo "missing nested commit for $$parent/$$subpath: $$base"; exit 1; }; \
				git -C "$$subdir" am --abort >/dev/null 2>&1 || true; \
				git -C "$$subdir" reset --hard -q; \
				git -C "$$subdir" clean -ffdx -q; \
				git -C "$$subdir" checkout -q -B unifrog-base "$$base"; \
				git -C "$$subdir" checkout -q -B unifrog-current unifrog-base; \
				patch_abs="$(abspath .)/$$patch_dir"; \
				git -C "$$subdir" config user.name "UniFrog CI"; \
				git -C "$$subdir" config user.email "ci@unifrog.local"; \
				git -C "$$subdir" am -3 --keep-cr -q "$$patch_abs"/*.patch; \
			done; \
		) & \
		pids="$$pids $$!"; \
	done; \
	for pid in $$pids; do \
		wait $$pid || status=$$?; \
	done; \
	exit $$status

doctor:
	@command -v $(CROSS_COMPILE)gcc >/dev/null || { echo "missing: $(CROSS_COMPILE)gcc"; exit 1; }
	@command -v $(CROSS_COMPILE)g++ >/dev/null || { echo "missing: $(CROSS_COMPILE)g++"; exit 1; }
	@command -v $(CROSS_COMPILE)ar >/dev/null || { echo "missing: $(CROSS_COMPILE)ar"; exit 1; }
	@test -d "$(SDK)/include" || { echo "missing SDK include: $(SDK)/include"; exit 1; }
	@set -e; for target in libretro-common $(REQUESTED_CORE_TARGETS); do \
		found=0; \
		for spec in $(CORE_SPECS); do \
			name=$${spec%%|*}; rest=$${spec#*|}; dir=$${rest%%|*}; \
			test "$$name" = "$$target" || continue; \
			found=1; break; \
		done; \
		test "$$found" = 1 || { echo "unknown core target: $$target"; exit 1; }; \
		git -C "$$dir" rev-parse --git-dir >/dev/null 2>&1 || { echo "missing core checkout: $$dir; run: make init CORE_IDS=\"$(CORE_IDS_EFFECTIVE)\""; exit 1; }; \
	done
	@if printf '%s\n' "$(REQUESTED_CORE_TARGETS)" | grep -Eq '(^| )gpsp( |$$)'; then \
		test -f $(GPSP_DIR)/libretro/libretro-common/include/libretro.h || { echo "missing gpSP bundled libretro-common; run: make init"; exit 1; }; \
	fi
	@if printf '%s\n' "$(REQUESTED_SUPPORT_TARGETS)" | grep -q chd-support; then \
		test -f $(LIBCHDR_DIR)/src/libchdr_chd.c || { echo "missing support checkout: $(LIBCHDR_DIR); run: make init"; exit 1; }; \
	fi
	@test -f $(ZSTD_DIR)/decompress/zstd_decompress.c || { echo "missing support checkout: $(ZSTD_DIR); run: make init"; exit 1; }
	@test -f $(ZLIB_DIR)/inflate.c || { echo "missing support checkout: $(ZLIB_DIR); run: make init"; exit 1; }
	@echo "OK"

smoke-doctor:
	@command -v $(CROSS_COMPILE)gcc >/dev/null || { echo "missing: $(CROSS_COMPILE)gcc"; exit 1; }
	@command -v $(CROSS_COMPILE)g++ >/dev/null || { echo "missing: $(CROSS_COMPILE)g++"; exit 1; }
	@command -v $(CROSS_COMPILE)ar >/dev/null || { echo "missing: $(CROSS_COMPILE)ar"; exit 1; }
	@test -d "$(SDK)/include" || { echo "missing SDK include: $(SDK)/include"; exit 1; }
	@test -e $(LIBRETRO_COMMON_DIR)/.git || { echo "missing core checkout: $(LIBRETRO_COMMON_DIR); run: make smoke-init"; exit 1; }
	@test -e $(QUICKNES_DIR)/.git || { echo "missing core checkout: $(QUICKNES_DIR); run: make smoke-init"; exit 1; }
	@echo "OK"

dep-status:
	@set -e; \
	dep="$(DEP_TARGET)"; \
	core_filter=""; support_filter=""; \
	if test -z "$$dep" && test -n "$(CORE_IDS)"; then \
		core_filter="libretro-common $(REQUESTED_CORE_TARGETS)"; \
		support_filter="$(REQUESTED_SUPPORT_DEPS)"; \
	fi; \
	found=0; \
	print_spec() { \
		kind="$$1"; spec="$$2"; \
		case "$(DEP_STATUS_KIND)" in ""|all) ;; "$$kind") ;; *) return 0 ;; esac; \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		policy=$${rest%%|*}; rest=$${rest#*|}; \
		ref=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; rest=$${rest#*|}; \
		patch_dir=$${rest%%|*}; rest=$${rest#*|}; \
		if test -n "$$dep" && test "$$name" != "$$dep"; then return 0; fi; \
		if test -z "$$dep" && test -n "$$core_filter$$support_filter"; then \
			case "$$kind" in \
			core) case " $$core_filter " in *" $$name "*) ;; *) return 0 ;; esac ;; \
			support) case " $$support_filter " in *" $$name "*) ;; *) return 0 ;; esac ;; \
			esac; \
		fi; \
		found=1; \
		if test -d "$$dir/.git"; then \
			head=$$(git -C "$$dir" rev-parse --short HEAD); \
			branch=$$(git -C "$$dir" branch --show-current 2>/dev/null || true); \
			dirty=clean; \
			git -C "$$dir" diff --quiet --ignore-submodules -- && git -C "$$dir" diff --cached --quiet --ignore-submodules -- || dirty=dirty; \
		else \
			head=missing; branch=-; dirty=missing; \
		fi; \
		if test "$$patch_dir" != "-" && ls "$$patch_dir"/*.patch >/dev/null 2>&1; then patch_state=patches; else patch_state=no-patches; fi; \
		echo "$$kind/$$name policy=$$policy ref=$$ref commit=$$commit patch=$$patch_dir $$patch_state head=$$head branch=$$branch state=$$dirty dir=$$dir"; \
	}; \
	for spec in $(SUPPORT_SPECS); do \
		print_spec support "$$spec"; \
	done; \
	for spec in $(CORE_SPECS); do \
		print_spec core "$$spec"; \
	done; \
	if test -n "$$dep"; then test "$$found" = 1 || { echo "unknown dependency: $$dep"; exit 1; }; fi

pin-status:
	@set -e; \
	mode_override="$(PIN_MODE)"; \
	case "$$mode_override" in policy|head|tag) ;; *) echo "MODE must be policy, head, or tag"; exit 1;; esac; \
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
	for spec in $(SUPPORT_SPECS) $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		policy=$${rest%%|*}; rest=$${rest#*|}; \
		ref=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; \
		mode="$$mode_override"; \
		if test "$$mode" = policy; then mode="$$policy"; fi; \
		case "$$mode" in head|tag) ;; *) echo "$$name: policy must be head or tag"; exit 1;; esac; \
		set -- $$(resolve_ref "$$url" "$$mode"); \
		printf '%-16s policy=%s mode=%s ref=%s pinned=%s latest=%s source=%s:%s\n' "$$name" "$$policy" "$$mode" "$$ref" "$$commit" "$$1" "$$2" "$$3"; \
	done

upgrade-pins:
	@set -e; \
	mode_override="$(PIN_MODE)"; \
	case "$$mode_override" in policy|head|tag) ;; *) echo "MODE must be policy, head, or tag"; exit 1;; esac; \
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
	for spec in $(SUPPORT_SPECS) $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		policy=$${rest%%|*}; rest=$${rest#*|}; \
		old_ref=$${rest%%|*}; rest=$${rest#*|}; \
		old=$${rest%%|*}; \
		mode="$$mode_override"; \
		if test "$$mode" = policy; then mode="$$policy"; fi; \
		case "$$mode" in head|tag) ;; *) echo "$$name: policy must be head or tag"; exit 1;; esac; \
		set -- $$(resolve_ref "$$url" "$$mode"); \
		new=$$1; kind=$$2; label=$$3; \
		if test -z "$$new"; then echo "$$name: unable to resolve latest $$mode"; exit 1; fi; \
		if test "$$new" != "$$old"; then \
			sed -i.bak "/'$$name|/s|$$old_ref|$$label|; /'$$name|/s|$$old|$$new|" manifest.mk; \
			rm -f manifest.mk.bak; \
			echo "  PIN     $$name $$old -> $$new ($$kind $$label, policy $$policy, mode $$mode)"; \
		else \
			echo "  PIN     $$name already $$old ($$kind $$label, policy $$policy, mode $$mode)"; \
		fi; \
	done

dep-diff:
	@test -n "$(DEP_TARGET)" || { echo "usage: make dep-diff DEP=<dependency>"; exit 1; }
	@set -e; found=0; \
	for spec in $(SUPPORT_SPECS) $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		policy=$${rest%%|*}; rest=$${rest#*|}; \
		ref=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; \
		if test "$$name" = "$(DEP_TARGET)"; then found=1; git -C "$$dir" diff "$$commit..HEAD"; fi; \
	done; \
	test "$$found" = 1 || { echo "unknown dependency: $(DEP_TARGET)"; exit 1; }

dep-log:
	@test -n "$(DEP_TARGET)" || { echo "usage: make dep-log DEP=<dependency>"; exit 1; }
	@set -e; found=0; \
	for spec in $(SUPPORT_SPECS) $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		policy=$${rest%%|*}; rest=$${rest#*|}; \
		ref=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; \
		if test "$$name" = "$(DEP_TARGET)"; then found=1; git -C "$$dir" log --oneline -20 "$$commit..HEAD"; fi; \
	done; \
	test "$$found" = 1 || { echo "unknown dependency: $(DEP_TARGET)"; exit 1; }

dep-update:
	@test -n "$(DEP_TARGET)" || { echo "usage: make dep-update DEP=<dependency> [REF=<ref>]"; exit 1; }
	@set -e; found=0; \
	for spec in $(SUPPORT_SPECS) $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		policy=$${rest%%|*}; rest=$${rest#*|}; \
		ref=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; rest=$${rest#*|}; \
		patch_dir=$${rest%%|*}; rest=$${rest#*|}; \
		sparse=$$rest; \
		if test "$$name" = "$(DEP_TARGET)"; then \
			found=1; \
			depth_arg=""; filter_arg=""; \
			if test "$(DEP_DEPTH)" != 0; then depth_arg="--depth $(DEP_DEPTH)"; fi; \
			if test "$(DEP_CHECKOUT)" = sparse; then filter_arg="--filter=blob:none"; fi; \
			if test -d "$$dir/.git"; then \
				git -C "$$dir" remote set-url origin "$$url"; \
			else \
				mkdir -p "$$(dirname "$$dir")"; \
				git init -q "$$dir"; \
				git -C "$$dir" remote add origin "$$url"; \
			fi; \
			target_ref="$${REF:-$$ref}"; \
			target_commit="$$commit"; \
			if test -n "$(REF)"; then target_commit=""; fi; \
			if test -z "$$target_commit" || ! git -C "$$dir" cat-file -e "$$target_commit^{commit}" 2>/dev/null; then \
				if printf '%s\n' "$$target_ref" | grep -Eq '^[0-9a-fA-F]{40}$$'; then \
					$(DEP_GIT_ENV) git -C "$$dir" fetch $$depth_arg $$filter_arg origin "$$target_ref"; \
				else \
					case "$$target_ref" in \
					v[0-9]*|[0-9]*|n[0-9]*) $(DEP_GIT_ENV) git -C "$$dir" fetch $$depth_arg $$filter_arg origin "refs/tags/$$target_ref:refs/tags/$$target_ref" || $(DEP_GIT_ENV) git -C "$$dir" fetch $$depth_arg $$filter_arg origin "$$target_ref" ;; \
					*) $(DEP_GIT_ENV) git -C "$$dir" fetch $$depth_arg $$filter_arg origin "$${target_commit:-$$target_ref}" ;; \
					esac; \
				fi; \
			fi; \
			if test -z "$$target_commit"; then target_commit=$$(git -C "$$dir" rev-parse "$$target_ref^{commit}" 2>/dev/null || git -C "$$dir" rev-parse FETCH_HEAD^{commit}); fi; \
			if test "$(DEP_CHECKOUT)" = sparse; then \
				case "$$sparse" in \
				:files\ *) git -C "$$dir" sparse-checkout set --no-cone $${sparse#*:files } ;; \
				:root-files) git -C "$$dir" sparse-checkout init --no-cone; printf '/*\n!/*/\n' >"$$dir/.git/info/sparse-checkout" ;; \
				.) git -C "$$dir" sparse-checkout disable >/dev/null 2>&1 || true ;; \
				*) git -C "$$dir" sparse-checkout init --cone; git -C "$$dir" sparse-checkout set --skip-checks $$sparse ;; \
				esac; \
			else \
				git -C "$$dir" sparse-checkout disable >/dev/null 2>&1 || true; \
			fi; \
			git -C "$$dir" am --abort >/dev/null 2>&1 || true; \
			git -C "$$dir" reset --hard -q; \
			git -C "$$dir" clean -fdx -q; \
			branch=unifrog-current; \
			if test -n "$(REF)"; then branch=unifrog-update; fi; \
			git -C "$$dir" checkout -q -B unifrog-base "$$target_commit" >/dev/null; \
			git -C "$$dir" reset --hard -q "$$target_commit"; \
			git -C "$$dir" checkout -q -B "$$branch" unifrog-base >/dev/null; \
			if test "$$patch_dir" != "-"; then \
				patch_abs="$(abspath .)/$$patch_dir"; \
				if ls "$$patch_abs"/*.patch >/dev/null 2>&1; then \
					git -C "$$dir" config user.name "UniFrog CI"; \
					git -C "$$dir" config user.email "ci@unifrog.local"; \
					git -C "$$dir" am -3 --keep-cr "$$patch_abs"/*.patch; \
				fi; \
			fi; \
		fi; \
	done; \
	test "$$found" = 1 || { echo "unknown dependency: $(DEP_TARGET)"; exit 1; }

dep-edit:
	@test -n "$(DEP_TARGET)" || { echo "usage: make dep-edit DEP=<dependency>"; exit 1; }
	$(Q)$(MAKE) --no-print-directory dep-update DEP="$(DEP_TARGET)"
	@echo "edit checkout prepared on unifrog-current for DEP=$(DEP_TARGET)"

dep-refresh:
	@test -n "$(DEP_TARGET)" || { echo "usage: make dep-refresh DEP=<dependency>"; exit 1; }
	@set -e; found=0; \
	for spec in $(SUPPORT_SPECS) $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		policy=$${rest%%|*}; rest=$${rest#*|}; \
		ref=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; rest=$${rest#*|}; \
		patch_dir=$${rest%%|*}; rest=$${rest#*|}; \
		if test "$$name" = "$(DEP_TARGET)"; then \
			found=1; \
			test "$$patch_dir" != "-" || { echo "$(DEP_TARGET) has no patch directory in manifest"; exit 1; }; \
			test -d "$$dir/.git" || { echo "missing checkout: $$dir"; exit 1; }; \
			git -C "$$dir" diff --quiet --ignore-submodules -- || { echo "dirty worktree: $$dir"; exit 1; }; \
			git -C "$$dir" diff --cached --quiet --ignore-submodules -- || { echo "staged changes in: $$dir"; exit 1; }; \
			patch_abs="$(abspath .)/$$patch_dir"; \
			mkdir -p "$$patch_abs"; \
			find "$$patch_abs" -name '*.patch' -type f -delete; \
			git -C "$$dir" format-patch --zero-commit --full-index --binary --base="$$commit" -o "$$patch_abs" "$$commit..HEAD"; \
		fi; \
	done; \
	test "$$found" = 1 || { echo "unknown dependency: $(DEP_TARGET)"; exit 1; }

dep-finalize:
	@test -n "$(DEP_TARGET)" || { echo "usage: make dep-finalize DEP=<dependency> REF=<ref>"; exit 1; }
	@test -n "$(REF)" || { echo "usage: make dep-finalize DEP=<dependency> REF=<ref>"; exit 1; }
	@set -e; found=0; \
	for spec in $(SUPPORT_SPECS) $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		policy=$${rest%%|*}; rest=$${rest#*|}; \
		old_ref=$${rest%%|*}; rest=$${rest#*|}; \
		old_commit=$${rest%%|*}; rest=$${rest#*|}; \
		patch_dir=$${rest%%|*}; rest=$${rest#*|}; \
		if test "$$name" = "$(DEP_TARGET)"; then \
			found=1; \
			test -d "$$dir/.git" || { echo "missing checkout: $$dir"; exit 1; }; \
			branch=$$(git -C "$$dir" branch --show-current 2>/dev/null || true); \
			test "$$branch" = unifrog-update || { echo "$$name must be on unifrog-update; run dep-update first"; exit 1; }; \
			git -C "$$dir" diff --quiet --ignore-submodules -- || { echo "dirty worktree: $$dir"; exit 1; }; \
			git -C "$$dir" diff --cached --quiet --ignore-submodules -- || { echo "staged changes in: $$dir"; exit 1; }; \
			new_commit=$$(git -C "$$dir" rev-parse unifrog-base^{commit}); \
			new_ref="$(REF)"; \
			sed -i.bak "/'$$name|/s|$$old_ref|$$new_ref|; /'$$name|/s|$$old_commit|$$new_commit|" manifest.mk; \
			rm -f manifest.mk.bak; \
			echo "  PIN     $$name $$old_commit -> $$new_commit ($$new_ref)"; \
			if test "$$patch_dir" != "-"; then \
				$(MAKE) --no-print-directory dep-refresh DEP="$$name"; \
			fi; \
			$(MAKE) --no-print-directory dep-update DEP="$$name" REF=; \
			$(MAKE) --no-print-directory dep-status DEP="$$name"; \
		fi; \
	done; \
	test "$$found" = 1 || { echo "unknown dependency: $(DEP_TARGET)"; exit 1; }

dep-patches-check:
	$(Q)$(MAKE) --no-print-directory core-init CORE_IDS="$(CORE_IDS_EFFECTIVE)"
	$(Q)$(MAKE) --no-print-directory dep-status \
		CORE_IDS="$(CORE_IDS_EFFECTIVE)" DEP_STATUS_KIND=core

dev-core:
	@test -n "$(CORE)" || { echo "usage: make dev-core CORE=<id> CORE_DIR=<path>"; exit 1; }
	@test -n "$(CORE_DIR)" || { echo "usage: make dev-core CORE=<id> CORE_DIR=<path>"; exit 1; }
	@test -d "$(CORE_DIR)" || { echo "missing CORE_DIR: $(CORE_DIR)"; exit 1; }
	@mkdir -p $(OUT)
	$(MAKE) -C "$(CORE_DIR)" -f "$(DEV_CORE_MAKEFILE)" $(DEV_CORE_ARGS)
	@test -s "$(CORE_DIR)/$(DEV_CORE_TARGET)" || { echo "missing built core archive: $(CORE_DIR)/$(DEV_CORE_TARGET)"; exit 1; }
	cp "$(CORE_DIR)/$(DEV_CORE_TARGET)" "$(OUT)/$(DEV_CORE_TARGET)"

$(OUT):
	mkdir -p $@

$(BUILD):
	mkdir -p $@

$(BUILD)/libretro-common/%.o: $(LIBRETRO_COMMON_DIR)/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -MD -MP -c $< -o $@

$(LIBRETRO_COMMON): $(LIBRETRO_COMMON_OBJECTS) | $(OUT) smoke-doctor
	$(LOG_ECHO) "  AR      $@"
	$(AR) rcs $@ $(LIBRETRO_COMMON_OBJECTS)

libretro-common: $(LIBRETRO_COMMON)

chd-support: $(CHD_SUPPORT_LIB)

$(BUILD)/chd-support/libchdr_%.o: $(LIBCHDR_DIR)/src/libchdr_%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/lzma_%.o: $(LZMA_DIR)/src/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/zstd_%.o: $(ZSTD_DIR)/common/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/zstd_huf_decompress.o: $(ZSTD_DIR)/decompress/huf_decompress.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/zstd_zstd_%.o: $(ZSTD_DIR)/decompress/zstd_%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/zlib_%.o: $(ZLIB_DIR)/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) $(CHD_SUPPORT_EXTRA_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/zlib_gz%.o: CHD_SUPPORT_EXTRA_CFLAGS := -include sys/unistd.h
$(BUILD)/chd-support/zlib_deflate.o: CHD_SUPPORT_EXTRA_CFLAGS := -include support/zlib_deflate_compat.h

$(BUILD)/chd-support/sf2000_%.o: support/%.c | $(BUILD)
	$(LOG_ECHO) "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(CHD_SUPPORT_LIB): $(CHD_SUPPORT_OBJECTS) | $(OUT) doctor
	$(LOG_ECHO) "  AR      $@"
	$(AR) rcs $@ $(CHD_SUPPORT_OBJECTS)

gambatte: doctor | $(OUT)
	$(MAKE) -C $(GAMBATTE_DIR) -f Makefile.libretro platform=sf2000 \
		MIPS=$(CROSS_COMPILE) CC="$(CC)" \
		CXX="$(CXX)" AR=$(AR)
	$(call copy_core_lib,$(GAMBATTE_LIB))

gpsp: doctor | $(OUT)
	CFLAGS="$(GPSP_CFLAGS)" $(MAKE) -C $(GPSP_DIR) -f Makefile $(GPSP_MAKE_ARGS)
	$(call copy_core_lib,$(GPSP_LIB))

gpsp_multicore: doctor | $(OUT)
	CFLAGS="$(GPSP_MULTICORE_CFLAGS)" $(MAKE) -C $(GPSP_MULTICORE_DIR) -f Makefile $(GPSP_MULTICORE_MAKE_ARGS)
	$(call copy_core_lib,$(GPSP_MULTICORE_LIB))

picodrive: doctor $(CHD_SUPPORT_LIB) | $(OUT)
	CFLAGS="$(PICODRIVE_CFLAGS)" CXXFLAGS="$(PICODRIVE_CFLAGS)" \
	$(MAKE) -C $(PICODRIVE_DIR) -f Makefile.libretro platform=unix \
		CC="$(CC)" CXX="$(CXX)" AR=$(AR) STATIC_LINKING=1 \
		STATIC_LINKING_LINK=1 EXTERNAL_CHD_MEDIA=1 EXTERNAL_ZLIB=1 \
		EXTERNAL_CHD_INCFLAGS="$(CHD_SUPPORT_INCFLAGS)" \
		EXTERNAL_ZLIB_INCFLAGS="-I$(abspath $(ZLIB_DIR))" \
		fpic= TARGET=picodrive_libretro_sf2000.a
	$(call copy_core_lib,$(PICODRIVE_LIB))

snes9x2005: doctor | $(OUT)
	CFLAGS="$(SNES9X2005_CFLAGS)" CXXFLAGS="$(SNES9X2005_CFLAGS)" \
	$(MAKE) -C $(SNES9X2005_DIR) -f Makefile platform=unix \
		CC="$(CC)" CXX="$(CXX)" AR=$(AR) STATIC_LINKING=1 \
		fpic= TARGET=snes9x2005_libretro_sf2000.a
	$(call copy_core_lib,$(SNES9X2005_LIB))

snes9x2002: doctor | $(OUT)
	CFLAGS="$(SNES9X2002_CFLAGS)" CXXFLAGS="$(SNES9X2002_CFLAGS)" \
	$(MAKE) -C $(SNES9X2002_DIR) -f Makefile platform=unix \
		CC="$(CC)" CXX="$(CXX)" AR=$(AR) STATIC_LINKING=1 \
		fpic= TARGET=snes9x2002_libretro_sf2000.a
	$(call copy_core_lib,$(SNES9X2002_LIB))

quicknes: smoke-doctor | $(OUT)
	CFLAGS="$(QUICKNES_CFLAGS)" CXXFLAGS="$(QUICKNES_CFLAGS)" \
	$(MAKE) -C $(QUICKNES_DIR) -f Makefile platform=unix \
		CC="$(CC)" CXX="$(CXX)" AR=$(AR) STATIC_LINKING=1 \
		fpic= TARGET=quicknes_libretro_sf2000.a
	$(call copy_core_lib,$(QUICKNES_LIB))

smoke-check: $(SMOKE_TARGETS)
	@echo "OK"

fceumm: doctor | $(OUT)
	CFLAGS="$(FCEUMM_CFLAGS) -DFRONTEND_SUPPORTS_RGB565=1" \
	CXXFLAGS="$(FCEUMM_CFLAGS) -DFRONTEND_SUPPORTS_RGB565=1" \
	$(MAKE) -C $(FCEUMM_DIR) -f Makefile.libretro platform=unix \
		CC="$(CC)" CXX="$(CXX)" AR=$(AR) STATIC_LINKING=1 \
		EXTERNAL_ZLIB=1 HAVE_NTSC=0 WANT_32BPP=0 fpic= \
		TARGET=fceumm_libretro_sf2000.a
	$(call copy_core_lib,$(FCEUMM_LIB))

gearboy: doctor | $(OUT)
	CFLAGS="$(GEARBOY_CFLAGS)" CXXFLAGS="$(GEARBOY_CFLAGS)" \
	$(MAKE) -C $(GEARBOY_DIR) -f Makefile platform=unix \
		CC="$(CC)" CXX="$(CXX)" AR=$(AR) STATIC_LINKING=1 \
		fpic= TARGET=gearboy_libretro_sf2000.a
	$(call copy_core_lib,$(GEARBOY_LIB))

pce-fast: doctor $(CHD_SUPPORT_LIB) | $(OUT)
	CFLAGS="$(PCE_FAST_CFLAGS)" CXXFLAGS="$(PCE_FAST_CFLAGS)" \
	$(MAKE) -C $(PCE_FAST_DIR) -f Makefile platform=unix \
		CC="$(CC)" CXX="$(CXX)" AR=$(AR) STATIC_LINKING=1 \
		EXTERNAL_CHD_MEDIA=1 HAVE_CHD=1 \
		EXTERNAL_CHD_INCFLAGS="$(CHD_SUPPORT_INCFLAGS)" \
		EXTERNAL_ZLIB_INCFLAGS="-I$(abspath $(ZLIB_DIR))" \
		fpic= TARGET=pce_fast_libretro_sf2000.a
	$(call copy_core_lib,$(PCE_FAST_LIB))

qpsx: doctor | $(OUT)
	CFLAGS="$(QPSX_CFLAGS)" CXXFLAGS="$(QPSX_CXXFLAGS)" \
	$(MAKE) -C $(QPSX_DIR) -f Makefile.libretro platform=sf2000 \
		QPSX_PLATFORM=$(QPSX_PLATFORM) \
		MIPS=$(CROSS_COMPILE) CC="$(CC)" CXX="$(CXX)" AR=$(AR) \
		STATIC_LINKING=1 TARGET=pcsx4all_libretro_sf2000.a
	$(call copy_core_lib,$(QPSX_LIB))

$(BUILD)/frogui/%.o: $(FROGUI_DIR)/%.c
	$(LOG_ECHO) "  CC      $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(FROGUI_CFLAGS) -MD -MP -c $< -o $@

frogui: doctor $(FROGUI_OBJECTS) | $(OUT)
	$(LOG_ECHO) "  AR      $(FROGUI_LIB)"
	$(Q)rm -f $(FROGUI_LIB)
	$(Q)$(AR) rcs $(FROGUI_LIB) $(FROGUI_OBJECTS)

define SF2000_PORT_RULE
$(1): doctor | $(OUT)
	$$(LOG_ECHO) "  CORE    $(1)"
	$$(Q)CPATH="$(abspath $(SDK))/include/hcrtos:$(abspath $(SDK))/include" \
		$$(MAKE) -C $$($(1)_PORT_DIR) -f $$($(1)_PORT_MAKEFILE) \
		platform=sf2000 MIPS="$$(CROSS_COMPILE)" CC="$$(CC)" CXX="$$(CXX)" \
		AR="$$(AR)" STATIC_LINKING=1 $$($(1)_PORT_ARGS)
	$$(Q)test -s $$($(1)_PORT_SOURCE_ARCHIVE)
	$$(Q)rm -f $$($(1)_PORT_OUTPUT_ARCHIVE)
	$$(Q)$$(OBJCOPY) $$(call libretro_static_rename_objcopy,$$($(1)_PORT_PREFIX)) \
		$$($(1)_PORT_SOURCE_ARCHIVE) $$($(1)_PORT_OUTPUT_ARCHIVE)
	$$(Q)set -eu; symbols="$$($(1)_PORT_OUTPUT_ARCHIVE).symbols"; \
		trap 'rm -f "$$$$symbols"' EXIT HUP INT TERM; \
		$$(NM) -g --defined-only $$($(1)_PORT_OUTPUT_ARCHIVE) | awk '{print $$$$3}' > "$$$$symbols"; \
		for symbol in $$(LIBRETRO_API_SYMBOLS); do \
			grep -Fqx "$$($(1)_PORT_PREFIX)_$$$$symbol" "$$$$symbols" || { \
				echo "missing renamed libretro entry point: $$$$symbol"; exit 1; }; \
			if grep -Fqx "$$$$symbol" "$$$$symbols"; then \
				echo "unrenamed libretro entry point remains: $$$$symbol"; exit 1; \
			fi; \
		done
	$$(Q)rm -f $$($(1)_PORT_SOURCE_ARCHIVE)
endef

$(foreach target,$(SF2000_PORT_TARGETS),$(eval $(call SF2000_PORT_RULE,$(target))))

check: doctor
	@set -e; \
	for spec in $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; \
		case " libretro-common $(REQUESTED_CORE_TARGETS) " in *" $$name "*) ;; *) continue;; esac; \
		test -d "$$dir/.git" -o -f "$$dir/.git" || { echo "missing core checkout: $$dir"; exit 1; }; \
		status=$$(git -C "$$dir" status --porcelain); \
		for nested_spec in $(NESTED_CORE_SPECS); do \
			parent=$${nested_spec%%|*}; nested_rest=$${nested_spec#*|}; \
			subpath=$${nested_rest%%|*}; \
			if test "$$parent" = "$$name"; then \
				status=$$(printf '%s\n' "$$status" | sed "\\|^ M $$subpath$$|d"); \
			fi; \
		done; \
		if test -n "$$status"; then \
			echo "dirty core checkout: $$dir"; exit 1; \
		fi; \
	done
	@set -e; \
	for nested_spec in $(NESTED_CORE_SPECS); do \
		parent=$${nested_spec%%|*}; rest=$${nested_spec#*|}; \
		subpath=$${rest%%|*}; rest=$${rest#*|}; \
		base=$${rest%%|*}; patch_dir=$${rest#*|}; \
		case " $(REQUESTED_CORE_TARGETS) " in *" $$parent "*) ;; *) continue;; esac; \
		parent_dir=""; \
		for core_spec in $(CORE_SPECS); do \
			core_name=$${core_spec%%|*}; core_rest=$${core_spec#*|}; \
			if test "$$core_name" = "$$parent"; then parent_dir=$${core_rest%%|*}; break; fi; \
		done; \
		subdir="$$parent_dir/$$subpath"; \
		test -n "$$parent_dir" -a -e "$$subdir/.git" || { echo "missing nested checkout: $$parent/$$subpath"; exit 1; }; \
		git -C "$$subdir" merge-base --is-ancestor "$$base" HEAD || { echo "wrong nested base: $$parent/$$subpath"; exit 1; }; \
		test -z "$$(git -C "$$subdir" status --porcelain)" || { echo "dirty nested checkout: $$parent/$$subpath"; exit 1; }; \
		test -n "$$(find "$$patch_dir" -name '*.patch' -type f -print -quit)" || { echo "missing nested patches: $$patch_dir"; exit 1; }; \
	done
	@echo "OK"

nested-patches-refresh:
	@test -n "$(CORE)" -a -n "$(NESTED)" || { echo "usage: make nested-patches-refresh CORE=<core> NESTED=<path>"; exit 1; }
	@set -e; found=0; \
	for spec in $(NESTED_CORE_SPECS); do \
		parent=$${spec%%|*}; rest=$${spec#*|}; \
		subpath=$${rest%%|*}; rest=$${rest#*|}; \
		base=$${rest%%|*}; patch_dir=$${rest#*|}; \
		test "$$parent" = "$(CORE)" -a "$$subpath" = "$(NESTED)" || continue; \
		found=1; parent_dir=""; \
		for core_spec in $(CORE_SPECS); do \
			core_name=$${core_spec%%|*}; core_rest=$${core_spec#*|}; \
			if test "$$core_name" = "$$parent"; then parent_dir=$${core_rest%%|*}; break; fi; \
		done; \
		subdir="$$parent_dir/$$subpath"; \
		test -z "$$(git -C "$$subdir" status --porcelain)" || { echo "dirty nested checkout: $$subdir"; exit 1; }; \
		mkdir -p "$$patch_dir"; find "$$patch_dir" -name '*.patch' -type f -delete; \
		git -C "$$subdir" format-patch --zero-commit --full-index --binary \
			--base="$$base" -o "$(abspath .)/$$patch_dir" "$$base..HEAD"; \
	done; \
	test "$$found" = 1 || { echo "unknown nested dependency: $(CORE)/$(NESTED)"; exit 1; }

clean:
	$(MAKE) -C $(GAMBATTE_DIR) -f Makefile.libretro clean platform=sf2000 \
		MIPS=$(CROSS_COMPILE) CC=$(CROSS_COMPILE)gcc \
		CXX=$(CROSS_COMPILE)g++ AR=$(CROSS_COMPILE)ar || true
	CFLAGS="$(GPSP_CFLAGS)" $(MAKE) -C $(GPSP_DIR) -f Makefile clean \
		$(GPSP_MAKE_ARGS) || true
	CFLAGS="$(GPSP_MULTICORE_CFLAGS)" $(MAKE) -C $(GPSP_MULTICORE_DIR) -f Makefile clean \
		$(GPSP_MULTICORE_MAKE_ARGS) || true
	CFLAGS="$(PICODRIVE_CFLAGS)" CXXFLAGS="$(PICODRIVE_CFLAGS)" \
		$(MAKE) -C $(PICODRIVE_DIR) -f Makefile.libretro clean \
		platform=unix CC=$(CC) CXX=$(CXX) AR=$(AR) STATIC_LINKING=1 \
		STATIC_LINKING_LINK=1 EXTERNAL_CHD_MEDIA=1 EXTERNAL_ZLIB=1 \
		EXTERNAL_CHD_INCFLAGS="$(CHD_SUPPORT_INCFLAGS)" \
		EXTERNAL_ZLIB_INCFLAGS="-I$(abspath $(ZLIB_DIR))" \
		fpic= TARGET=picodrive_libretro_sf2000.a || true
	CFLAGS="$(SNES9X2005_CFLAGS)" CXXFLAGS="$(SNES9X2005_CFLAGS)" \
		$(MAKE) -C $(SNES9X2005_DIR) -f Makefile clean platform=unix \
		CC=$(CC) CXX=$(CXX) AR=$(AR) STATIC_LINKING=1 fpic= \
		TARGET=snes9x2005_libretro_sf2000.a || true
	CFLAGS="$(SNES9X2002_CFLAGS)" CXXFLAGS="$(SNES9X2002_CFLAGS)" \
		$(MAKE) -C $(SNES9X2002_DIR) -f Makefile clean platform=unix \
		CC=$(CC) CXX=$(CXX) AR=$(AR) STATIC_LINKING=1 fpic= \
		TARGET=snes9x2002_libretro_sf2000.a || true
	CFLAGS="$(QUICKNES_CFLAGS)" CXXFLAGS="$(QUICKNES_CFLAGS)" \
		$(MAKE) -C $(QUICKNES_DIR) -f Makefile clean platform=unix \
		CC=$(CC) CXX=$(CXX) AR=$(AR) STATIC_LINKING=1 fpic= \
		TARGET=quicknes_libretro_sf2000.a || true
	CFLAGS="$(FCEUMM_CFLAGS) -DFRONTEND_SUPPORTS_RGB565=1" \
		CXXFLAGS="$(FCEUMM_CFLAGS) -DFRONTEND_SUPPORTS_RGB565=1" \
		$(MAKE) -C $(FCEUMM_DIR) -f Makefile.libretro clean \
		platform=unix CC=$(CC) CXX=$(CXX) AR=$(AR) STATIC_LINKING=1 \
		EXTERNAL_ZLIB=1 HAVE_NTSC=0 WANT_32BPP=0 fpic= \
		TARGET=fceumm_libretro_sf2000.a || true
	CFLAGS="$(GEARBOY_CFLAGS)" CXXFLAGS="$(GEARBOY_CFLAGS)" \
		$(MAKE) -C $(GEARBOY_DIR) -f Makefile clean platform=unix \
		CC=$(CC) CXX=$(CXX) AR=$(AR) STATIC_LINKING=1 fpic= \
		TARGET=gearboy_libretro_sf2000.a || true
	CFLAGS="$(PCE_FAST_CFLAGS)" CXXFLAGS="$(PCE_FAST_CFLAGS)" \
		$(MAKE) -C $(PCE_FAST_DIR) -f Makefile clean platform=unix \
		CC=$(CC) CXX=$(CXX) AR=$(AR) STATIC_LINKING=1 fpic= \
		EXTERNAL_CHD_MEDIA=1 HAVE_CHD=1 \
		EXTERNAL_CHD_INCFLAGS="$(CHD_SUPPORT_INCFLAGS)" \
		EXTERNAL_ZLIB_INCFLAGS="-I$(abspath $(ZLIB_DIR))" \
		TARGET=pce_fast_libretro_sf2000.a || true
	CFLAGS="$(QPSX_CFLAGS)" CXXFLAGS="$(QPSX_CXXFLAGS)" \
		$(MAKE) -C $(QPSX_DIR) -f Makefile.libretro clean platform=sf2000 \
		MIPS=$(CROSS_COMPILE) CC=$(CC) CXX=$(CXX) AR=$(AR) \
		STATIC_LINKING=1 TARGET=pcsx4all_libretro_sf2000.a || true
	rm -rf $(BUILD)/frogui
	@$(foreach target,$(SF2000_PORT_TARGETS),\
		$(MAKE) -C $($(target)_PORT_DIR) -f $($(target)_PORT_MAKEFILE) clean \
		platform=sf2000 MIPS=$(CROSS_COMPILE) CC=$(CC) CXX=$(CXX) AR=$(AR) \
		$($(target)_PORT_ARGS) >/dev/null 2>&1 || true;)
	rm -rf $(OUT) $(BUILD)

ifneq ($(filter libretro-common all,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)),)
-include $(LIBRETRO_COMMON_OBJECTS:.o=.d)
endif
ifneq ($(filter chd-support picodrive pce-fast all,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)),)
-include $(CHD_SUPPORT_OBJECTS:.o=.d)
endif
ifneq ($(filter frogui all,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)),)
-include $(FROGUI_OBJECTS:.o=.d)
endif
