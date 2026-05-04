TOOLCHAIN ?= /opt/mipsel-mti-elf
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
SDK ?= ../../unifrog-hcrtos-sdk
JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
CCACHE ?=
CCACHE_PREFIX := $(if $(CCACHE),$(CCACHE) )

include manifest.mk

.SUFFIXES:

OUT := output
CORE ?=
LIBRETRO_COMMON_DIR := $(CORE_SOURCE_ROOT)/libretro-common
GAMBATTE_DIR := $(CORE_SOURCE_ROOT)/gambatte-libretro
GAMBATTE_LIB := $(GAMBATTE_DIR)/gambatte_libretro_sf2000.a
GPSP_DIR := $(CORE_SOURCE_ROOT)/gpsp-libretro
GPSP_LIB := $(GPSP_DIR)/gpsp_libretro_sf2000.a
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
PMP_VIDEO_DIR := $(CORE_SOURCE_ROOT)/sf2000-video-player
PMP_VIDEO_LIB := $(PMP_VIDEO_DIR)/pmp_libretro_sf2000.a
LIBRETRO_COMMON := $(OUT)/libretro-common-sf2000.a
BUILD := build
CC := $(CCACHE_PREFIX)$(CROSS_COMPILE)gcc
CXX := $(CCACHE_PREFIX)$(CROSS_COMPILE)g++
AR := $(CROSS_COMPILE)ar

copy_core_lib = cp $(1) $(OUT)/ && rm -f $(1)

COMMON_CFLAGS := -EL -march=mips32 -mtune=mips32 -Os -pipe \
	-msoft-float -fsigned-char -G0 -mno-abicalls -fno-pic \
	-ffast-math -fomit-frame-pointer \
	-ffunction-sections -fdata-sections \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-DSF2000 \
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

GPSP_RENAME_CFLAGS := $(call libretro_static_rename_cflags,gpsp)
PICODRIVE_RENAME_CFLAGS := $(call libretro_static_rename_cflags,picodrive)
SNES9X2005_RENAME_CFLAGS := $(call libretro_static_rename_cflags,snes9x2005)
SNES9X2002_RENAME_CFLAGS := $(call libretro_static_rename_cflags,snes9x2002)
QUICKNES_RENAME_CFLAGS := $(call libretro_static_rename_cflags,quicknes)
FCEUMM_RENAME_CFLAGS := $(call libretro_static_rename_cflags,fceumm)
GEARBOY_RENAME_CFLAGS := $(call libretro_static_rename_cflags,gearboy)
PCE_FAST_RENAME_CFLAGS := $(call libretro_static_rename_cflags,pce_fast)
QPSX_RENAME_CFLAGS := $(call libretro_static_rename_cflags,qpsx)
PMP_VIDEO_RENAME_CFLAGS := $(call libretro_static_rename_cflags,pmp_video)

CORE_BASE_CFLAGS := -EL -march=mips32 -mtune=mips32 -Os -pipe \
	-msoft-float -fsigned-char -G0 -mno-abicalls -fno-pic \
	-ffast-math -fomit-frame-pointer \
	-ffunction-sections -fdata-sections \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-DSF2000 \
	-isystem $(abspath $(SDK))/include \
	-isystem $(abspath $(SDK))/include/hcrtos \
	-isystem $(abspath $(SDK))/include/newlib

PICODRIVE_CFLAGS := $(CORE_BASE_CFLAGS) $(PICODRIVE_RENAME_CFLAGS)
SNES9X2005_CFLAGS := $(CORE_BASE_CFLAGS) $(SNES9X2005_RENAME_CFLAGS)
SNES9X2002_CFLAGS := $(CORE_BASE_CFLAGS) $(SNES9X2002_RENAME_CFLAGS)
QUICKNES_CFLAGS := $(CORE_BASE_CFLAGS) $(QUICKNES_RENAME_CFLAGS)
FCEUMM_CFLAGS := $(CORE_BASE_CFLAGS) $(FCEUMM_RENAME_CFLAGS)
GEARBOY_CFLAGS := $(CORE_BASE_CFLAGS) $(GEARBOY_RENAME_CFLAGS) -DMINIZ_NO_TIME
PCE_FAST_CFLAGS := $(CORE_BASE_CFLAGS) $(PCE_FAST_RENAME_CFLAGS)
QPSX_CFLAGS := $(CORE_BASE_CFLAGS) $(QPSX_RENAME_CFLAGS) \
	-Wno-error=incompatible-pointer-types \
	-Wno-error=implicit-function-declaration \
	-Wno-error=int-conversion \
	-Wno-incompatible-pointer-types \
	-Wno-implicit-function-declaration \
	-Wno-int-conversion
QPSX_CXXFLAGS := $(CORE_BASE_CFLAGS) $(QPSX_RENAME_CFLAGS)
PMP_VIDEO_CFLAGS := $(CORE_BASE_CFLAGS) $(PMP_VIDEO_RENAME_CFLAGS) \
	-include stdlib.h \
	-DFPM_DEFAULT \
	-I$(abspath $(PMP_VIDEO_DIR)) \
	-I$(abspath $(PMP_VIDEO_DIR))/xvid \
	-I$(abspath $(PMP_VIDEO_DIR))/xvid/bitstream \
	-I$(abspath $(PMP_VIDEO_DIR))/xvid/dct \
	-I$(abspath $(PMP_VIDEO_DIR))/xvid/image \
	-I$(abspath $(PMP_VIDEO_DIR))/xvid/motion \
	-I$(abspath $(PMP_VIDEO_DIR))/xvid/prediction \
	-I$(abspath $(PMP_VIDEO_DIR))/xvid/quant \
	-I$(abspath $(PMP_VIDEO_DIR))/xvid/utils \
	-I$(abspath $(PMP_VIDEO_DIR))/libmad \
	-Wall -Wno-unused-function -Wno-unused-variable \
	-Wno-unused-but-set-variable \
	-Wno-error=implicit-function-declaration \
	-Wno-error=builtin-declaration-mismatch \
	-Wno-implicit-function-declaration \
	-Wno-builtin-declaration-mismatch

GPSP_CFLAGS := -EL -march=mips32 -mtune=mips32 -msoft-float -G0 \
	-mno-abicalls -fno-pic -fsigned-char -ffast-math \
	-fomit-frame-pointer -ffunction-sections -fdata-sections \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-DSF2000 -DROM_BUFFER_SIZE=32 \
	-DSMALL_TRANSLATION_CACHE \
	$(GPSP_RENAME_CFLAGS)

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

ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif
ifeq ($(filter --output-sync% -O%,$(MAKEFLAGS)),)
MAKEFLAGS += --output-sync=target
endif

.PHONY: all support-init init doctor \
	libretro-common chd-support \
	gambatte gpsp picodrive snes9x2005 snes9x2002 quicknes fceumm \
	gearboy pce-fast qpsx pmp-video core-status diff-core log-core update-core \
	check clean status

all: libretro-common gambatte gpsp picodrive snes9x2005 snes9x2002 \
	quicknes fceumm gearboy pce-fast qpsx pmp-video

support-init:
	@set -e; \
	mkdir -p "$(CORE_SUPPORT_ROOT)"; \
	for spec in $(SUPPORT_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; rest=$${rest#*|}; \
		sparse=$$rest; \
		if test -d "$$dir/.git"; then \
			git -C "$$dir" remote set-url origin "$$url"; \
			if git -C "$$dir" cat-file -e "$$commit^{commit}" 2>/dev/null; then \
				echo "  REUSE   support/$$name $$commit"; \
			else \
				echo "  FETCH   support/$$name $$commit"; \
				git -C "$$dir" fetch --depth 1 --filter=blob:none origin "$$commit"; \
			fi; \
		else \
			echo "  FETCH   support/$$name $$commit"; \
			rm -rf "$$dir"; \
			git init -q "$$dir"; \
			git -C "$$dir" remote add origin "$$url"; \
			git -C "$$dir" fetch --depth 1 --filter=blob:none origin "$$commit"; \
		fi; \
		if test "$$sparse" = ":root-files"; then \
			git -C "$$dir" sparse-checkout init --no-cone; \
			printf '/*\n!/*/\n' >"$$dir/.git/info/sparse-checkout"; \
		elif test "$$sparse" != "."; then \
			git -C "$$dir" sparse-checkout init --cone; \
			git -C "$$dir" sparse-checkout set --skip-checks $$sparse; \
		fi; \
		git -C "$$dir" checkout -q "$$commit"; \
		git -C "$$dir" reset --hard -q "$$commit"; \
		git -C "$$dir" clean -fdx -q; \
	done

init: support-init
	@set -e; \
	mkdir -p "$(CORE_SOURCE_ROOT)"; \
	for spec in $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; rest=$${rest#*|}; \
		sparse=$$rest; \
		if test -d "$$dir/.git"; then \
			git -C "$$dir" remote set-url origin "$$url"; \
			if git -C "$$dir" cat-file -e "$$commit^{commit}" 2>/dev/null; then \
				echo "  REUSE   $$name $$commit"; \
			else \
				echo "  FETCH   $$name $$commit"; \
				git -C "$$dir" fetch --depth 1 --filter=blob:none origin "$$commit"; \
			fi; \
		else \
			echo "  FETCH   $$name $$commit"; \
			rm -rf "$$dir"; \
			git init -q "$$dir"; \
			git -C "$$dir" remote add origin "$$url"; \
			git -C "$$dir" fetch --depth 1 --filter=blob:none origin "$$commit"; \
		fi; \
		if test "$$sparse" != "."; then \
			git -C "$$dir" sparse-checkout init --cone; \
			git -C "$$dir" sparse-checkout set --skip-checks $$sparse; \
		fi; \
		git -C "$$dir" checkout -q "$$commit"; \
		git -C "$$dir" am --abort >/dev/null 2>&1 || true; \
		git -C "$$dir" reset --hard -q "$$commit"; \
		git -C "$$dir" clean -fdx -q; \
		git -C "$$dir" config user.name "UniFrog CI"; \
		git -C "$$dir" config user.email "ci@unifrog.local"; \
		if ls "patches/$$name"/*.patch >/dev/null 2>&1; then \
			git -C "$$dir" am --keep-cr -q "$(abspath patches/$$name)"/*.patch; \
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
				git -C "$$dr_dir" fetch --depth 1 --filter=blob:none origin "$$dr_commit"; \
			fi; \
			git -C "$$dr_dir" sparse-checkout init --no-cone; \
			printf '/dr_mp3.h\n/LICENSE\n' >"$$dr_dir/.git/info/sparse-checkout"; \
			git -C "$$dr_dir" checkout -q "$$dr_commit"; \
			git -C "$$dr_dir" reset --hard -q "$$dr_commit"; \
			git -C "$$dr_dir" clean -fdx -q; \
		else \
			git -C "$$dir" submodule update --init --depth 1 --filter=blob:none --jobs "$(JOBS)"; \
		fi; \
	done

doctor:
	@command -v $(CROSS_COMPILE)gcc >/dev/null || { echo "missing: $(CROSS_COMPILE)gcc"; exit 1; }
	@command -v $(CROSS_COMPILE)g++ >/dev/null || { echo "missing: $(CROSS_COMPILE)g++"; exit 1; }
	@command -v $(CROSS_COMPILE)ar >/dev/null || { echo "missing: $(CROSS_COMPILE)ar"; exit 1; }
	@test -d "$(SDK)/include" || { echo "missing SDK include: $(SDK)/include"; exit 1; }
	@test -e $(LIBRETRO_COMMON_DIR)/.git || { echo "missing core checkout: $(LIBRETRO_COMMON_DIR); run: make init"; exit 1; }
	@test -e $(GAMBATTE_DIR)/.git || { echo "missing core checkout: $(GAMBATTE_DIR); run: make init"; exit 1; }
	@test -e $(GPSP_DIR)/.git || { echo "missing core checkout: $(GPSP_DIR); run: make init"; exit 1; }
	@test -e $(PICODRIVE_DIR)/.git || { echo "missing core checkout: $(PICODRIVE_DIR); run: make init"; exit 1; }
	@test -e $(SNES9X2005_DIR)/.git || { echo "missing core checkout: $(SNES9X2005_DIR); run: make init"; exit 1; }
	@test -e $(SNES9X2002_DIR)/.git || { echo "missing core checkout: $(SNES9X2002_DIR); run: make init"; exit 1; }
	@test -e $(QUICKNES_DIR)/.git || { echo "missing core checkout: $(QUICKNES_DIR); run: make init"; exit 1; }
	@test -e $(FCEUMM_DIR)/.git || { echo "missing core checkout: $(FCEUMM_DIR); run: make init"; exit 1; }
	@test -e $(GEARBOY_ROOT)/.git || { echo "missing core checkout: $(GEARBOY_ROOT); run: make init"; exit 1; }
	@test -e $(PCE_FAST_DIR)/.git || { echo "missing core checkout: $(PCE_FAST_DIR); run: make init"; exit 1; }
	@test -e $(QPSX_DIR)/.git || { echo "missing core checkout: $(QPSX_DIR); run: make init"; exit 1; }
	@test -e $(PMP_VIDEO_DIR)/.git || { echo "missing core checkout: $(PMP_VIDEO_DIR); run: make init"; exit 1; }
	@test -f $(GPSP_DIR)/libretro/libretro-common/include/libretro.h || { echo "missing gpSP bundled libretro-common; run: make init"; exit 1; }
	@test -f $(LIBCHDR_DIR)/src/libchdr_chd.c || { echo "missing support checkout: $(LIBCHDR_DIR); run: make init"; exit 1; }
	@test -f $(ZSTD_DIR)/decompress/zstd_decompress.c || { echo "missing support checkout: $(ZSTD_DIR); run: make init"; exit 1; }
	@test -f $(ZLIB_DIR)/inflate.c || { echo "missing support checkout: $(ZLIB_DIR); run: make init"; exit 1; }
	@echo "TOOLCHAIN=$(TOOLCHAIN)"
	@echo "CCACHE=$(if $(CCACHE),$(CCACHE),disabled)"
	@echo "OK"

core-status:
	@set -e; \
	for spec in $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; \
		head=$$(git -C "$$dir" rev-parse --short HEAD); \
		echo "$$name upstream=$$commit head=$$head dir=$$dir"; \
	done

diff-core:
	@test -n "$(CORE)" || { echo "usage: make diff-core CORE=<core>"; exit 1; }
	@set -e; found=0; \
	for spec in $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; \
		if test "$$name" = "$(CORE)"; then found=1; git -C "$$dir" diff "$$commit..HEAD"; fi; \
	done; \
	test "$$found" = 1 || { echo "unknown core: $(CORE)"; exit 1; }

log-core:
	@test -n "$(CORE)" || { echo "usage: make log-core CORE=<core>"; exit 1; }
	@set -e; found=0; \
	for spec in $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; \
		if test "$$name" = "$(CORE)"; then found=1; git -C "$$dir" log --oneline -20 "$$commit..HEAD"; fi; \
	done; \
	test "$$found" = 1 || { echo "unknown core: $(CORE)"; exit 1; }

update-core:
	@test -n "$(CORE)" || { echo "usage: make update-core CORE=<core>"; exit 1; }
	@set -e; found=0; \
	for spec in $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; rest=$${rest#*|}; \
		url=$${rest%%|*}; rest=$${rest#*|}; \
		commit=$${rest%%|*}; rest=$${rest#*|}; \
		sparse=$$rest; \
		if test "$$name" = "$(CORE)"; then \
			found=1; \
			git -C "$$dir" remote set-url origin "$$url"; \
			if git -C "$$dir" cat-file -e "$$commit^{commit}" 2>/dev/null; then \
				:; \
			else \
				git -C "$$dir" fetch --depth 1 --filter=blob:none origin "$$commit"; \
			fi; \
			if test "$$sparse" != "."; then \
				git -C "$$dir" sparse-checkout init --cone; \
				git -C "$$dir" sparse-checkout set --skip-checks $$sparse; \
			fi; \
			git -C "$$dir" checkout -q "$$commit"; \
			git -C "$$dir" am --abort >/dev/null 2>&1 || true; \
			git -C "$$dir" reset --hard -q "$$commit"; \
			git -C "$$dir" clean -fdx -q; \
			if ls "patches/$$name"/*.patch >/dev/null 2>&1; then \
				git -C "$$dir" am --keep-cr -q "$(abspath patches/$$name)"/*.patch; \
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
					git -C "$$dr_dir" fetch --depth 1 --filter=blob:none origin "$$dr_commit"; \
				fi; \
				git -C "$$dr_dir" sparse-checkout init --no-cone; \
				printf '/dr_mp3.h\n/LICENSE\n' >"$$dr_dir/.git/info/sparse-checkout"; \
				git -C "$$dr_dir" checkout -q "$$dr_commit"; \
				git -C "$$dr_dir" reset --hard -q "$$dr_commit"; \
				git -C "$$dr_dir" clean -fdx -q; \
			else \
				git -C "$$dir" submodule update --init --depth 1 --filter=blob:none --jobs "$(JOBS)"; \
			fi; \
		fi; \
	done; \
	test "$$found" = 1 || { echo "unknown core: $(CORE)"; exit 1; }

$(OUT):
	mkdir -p $@

$(BUILD):
	mkdir -p $@

$(BUILD)/libretro-common/%.o: $(LIBRETRO_COMMON_DIR)/%.c | $(BUILD)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -MD -MP -c $< -o $@

$(LIBRETRO_COMMON): doctor $(LIBRETRO_COMMON_OBJECTS) | $(OUT)
	@echo "  AR      $@"
	$(AR) rcs $@ $(LIBRETRO_COMMON_OBJECTS)

libretro-common: $(LIBRETRO_COMMON)

chd-support: $(CHD_SUPPORT_LIB)

$(BUILD)/chd-support/libchdr_%.o: $(LIBCHDR_DIR)/src/libchdr_%.c | $(BUILD)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/lzma_%.o: $(LZMA_DIR)/src/%.c | $(BUILD)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/zstd_%.o: $(ZSTD_DIR)/common/%.c | $(BUILD)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/zstd_huf_decompress.o: $(ZSTD_DIR)/decompress/huf_decompress.c | $(BUILD)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/zstd_zstd_%.o: $(ZSTD_DIR)/decompress/zstd_%.c | $(BUILD)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/zlib_%.o: $(ZLIB_DIR)/%.c | $(BUILD)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) $(CHD_SUPPORT_EXTRA_CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/chd-support/zlib_gz%.o: CHD_SUPPORT_EXTRA_CFLAGS := -include sys/unistd.h

$(BUILD)/chd-support/sf2000_%.o: support/%.c | $(BUILD)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(CC) $(CHD_SUPPORT_CFLAGS) -MD -MP -c $< -o $@

$(CHD_SUPPORT_LIB): doctor $(CHD_SUPPORT_OBJECTS) | $(OUT)
	@echo "  AR      $@"
	$(AR) rcs $@ $(CHD_SUPPORT_OBJECTS)

gambatte: doctor | $(OUT)
	$(MAKE) -C $(GAMBATTE_DIR) -f Makefile.libretro platform=sf2000 \
		MIPS=$(CROSS_COMPILE) CC="$(CC)" \
		CXX="$(CXX)" AR=$(AR)
	$(call copy_core_lib,$(GAMBATTE_LIB))

gpsp: doctor | $(OUT)
	CFLAGS="$(GPSP_CFLAGS)" $(MAKE) -C $(GPSP_DIR) -f Makefile $(GPSP_MAKE_ARGS)
	$(call copy_core_lib,$(GPSP_LIB))

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

quicknes: doctor | $(OUT)
	CFLAGS="$(QUICKNES_CFLAGS)" CXXFLAGS="$(QUICKNES_CFLAGS)" \
	$(MAKE) -C $(QUICKNES_DIR) -f Makefile platform=unix \
		CC="$(CC)" CXX="$(CXX)" AR=$(AR) STATIC_LINKING=1 \
		fpic= TARGET=quicknes_libretro_sf2000.a
	$(call copy_core_lib,$(QUICKNES_LIB))

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
		MIPS=$(CROSS_COMPILE) CC="$(CC)" CXX="$(CXX)" AR=$(AR) \
		STATIC_LINKING=1 TARGET=pcsx4all_libretro_sf2000.a
	$(call copy_core_lib,$(QPSX_LIB))

pmp-video: doctor | $(OUT)
	$(MAKE) -C $(PMP_VIDEO_DIR) -f Makefile platform=sf2000 \
		MIPS=$(CROSS_COMPILE) CC="$(CC)" CXX="$(CXX)" AR=$(AR) \
		CFLAGS="$(PMP_VIDEO_CFLAGS)" STATIC_LINKING=1 \
		TARGET=pmp_libretro_sf2000.a
	$(call copy_core_lib,$(PMP_VIDEO_LIB))

check: doctor
	@set -e; \
	for spec in $(CORE_SPECS); do \
		name=$${spec%%|*}; rest=$${spec#*|}; \
		dir=$${rest%%|*}; \
		test -d "$$dir/.git" -o -f "$$dir/.git" || { echo "missing core checkout: $$dir"; exit 1; }; \
		if test -n "$$(git -C "$$dir" status --porcelain)"; then \
			echo "dirty core checkout: $$dir"; exit 1; \
		fi; \
	done
	@echo "OK"

status:
	git -C $(LIBRETRO_COMMON_DIR) status --short
	git -C $(GAMBATTE_DIR) status --short
	git -C $(GPSP_DIR) status --short
	git -C $(PICODRIVE_DIR) status --short
	git -C $(SNES9X2005_DIR) status --short
	git -C $(SNES9X2002_DIR) status --short
	git -C $(QUICKNES_DIR) status --short
	git -C $(FCEUMM_DIR) status --short
	git -C $(GEARBOY_ROOT) status --short
	git -C $(PCE_FAST_DIR) status --short
	git -C $(QPSX_DIR) status --short
	git -C $(PMP_VIDEO_DIR) status --short

clean:
	$(MAKE) -C $(GAMBATTE_DIR) -f Makefile.libretro clean platform=sf2000 \
		MIPS=$(CROSS_COMPILE) CC=$(CROSS_COMPILE)gcc \
		CXX=$(CROSS_COMPILE)g++ AR=$(CROSS_COMPILE)ar || true
	CFLAGS="$(GPSP_CFLAGS)" $(MAKE) -C $(GPSP_DIR) -f Makefile clean \
		$(GPSP_MAKE_ARGS) || true
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
	$(MAKE) -C $(PMP_VIDEO_DIR) -f Makefile clean platform=sf2000 \
		MIPS=$(CROSS_COMPILE) CC=$(CC) CXX=$(CXX) AR=$(AR) \
		CFLAGS="$(PMP_VIDEO_CFLAGS)" STATIC_LINKING=1 \
		TARGET=pmp_libretro_sf2000.a || true
	rm -rf $(OUT) $(BUILD)

ifneq ($(filter libretro-common all,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)),)
-include $(LIBRETRO_COMMON_OBJECTS:.o=.d)
endif
ifneq ($(filter chd-support picodrive pce-fast all,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)),)
-include $(CHD_SUPPORT_OBJECTS:.o=.d)
endif
