# Central option inventory used by Make defaults, checks, and generated examples.

# User-facing path and dependency defaults. Override these in untracked
# config.mk or on the make command line.
DEPS ?= .deps
TOOLCHAIN_VERSION ?= 1.3.2
TOOLCHAIN_ROOT ?= $(abspath $(DEPS)/frog-toolchain-v$(TOOLCHAIN_VERSION)-$(TOOLCHAIN_HOST_ARCH))
TOOLCHAIN ?= $(TOOLCHAIN_ROOT)/mipsel-mti-elf
TOOLCHAIN_URL ?= https://github.com/axgdev/frog-toolchain/releases/download/v$(TOOLCHAIN_VERSION)/toolchain-stable-static-$(TOOLCHAIN_HOST_ARCH)-gcc16.2.0-binutils2.47-newlib4.6.0.20260123.tar.xz
ifeq ($(TOOLCHAIN_HOST_ARCH),x86_64)
TOOLCHAIN_SHA256 ?= bcf2554637f1d536abcc3d5c7b97df2ec94e243c263fc2ea541820cdb1e71b8f
else ifeq ($(TOOLCHAIN_HOST_ARCH),arm64)
TOOLCHAIN_SHA256 ?= 57bf6df928d2b97255ec9ee67035039646c211b4b578dead0da77446598ae2b7
else
TOOLCHAIN_SHA256 ?= unsupported-host-architecture
endif
TOOLCHAIN_ARCHIVE_DIR ?= $(DEPS)/archives
TOOLCHAIN_ARCHIVE ?= $(TOOLCHAIN_ARCHIVE_DIR)/toolchain-stable-static-$(TOOLCHAIN_HOST_ARCH)-gcc16.2.0-binutils2.47-newlib4.6.0.20260123.tar.xz
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
DEP_CHECKOUT ?= sparse
DEP_DEPTH ?= 1
DEP_GIT_ENV ?= GIT_TERMINAL_PROMPT=0 GIT_SSH_COMMAND='ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new'
SDK ?= unifrog-hcrtos-sdk
CORES ?= cores
CORE_IDS ?=
CORE_SOURCE_ROOT ?= $(DEPS)/cores
CORE_SUPPORT_ROOT ?= $(DEPS)/support
JS2300 ?= $(DEPS)/frog2k-javascript
JS2300_URL ?= git@github.com:axgdev/frog2k-javascript.git
JS2300_BRANCH ?= main_unifrog
JS2300_REF ?= b9dc3d316f72178f2a38fb93dd691c5519fc6e58
LVGL_DIR ?= $(DEPS)/support/lvgl
LVGL_URL ?= https://github.com/lvgl/lvgl.git
LVGL_REF ?= 0019fc541f759b3323add63034502b0248afc58f
LZ4_DIR ?= $(CORE_SUPPORT_ROOT)/lz4
STB_DIR ?= $(CORE_SUPPORT_ROOT)/stb
NANOSVG_DIR ?= $(CORE_SUPPORT_ROOT)/nanosvg/src
HCRTOS_FFMPEG_URL ?= https://git.ffmpeg.org/ffmpeg.git
HCRTOS_FFMPEG_REF ?= n8.1.2
HCRTOS_FFMPEG_COMMIT ?= 38b88335f99e76ed89ff3c93f877fdefce736c13
HCRTOS_FFMPEG_PATCH ?= patches/hcrtos-ffmpeg-compat.patch
HCRTOS_FFMPEG_SOURCE ?= $(CORE_SUPPORT_ROOT)/ffmpeg-upstream
HCRTOS_FFMPEG_INSTALL ?= $(CORE_SUPPORT_ROOT)/hcrtos-ffmpeg
HCRTOS_FFMPEG_INCLUDE ?= $(HCRTOS_FFMPEG_INSTALL)/include
HCRTOS_FFMPEG_INSTALL_TARGETS ?= install-libs install-headers

# Build-mode defaults.
JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
BUILD_PROGRESS ?= $(if $(V),1,0)
PIN_MODE ?= $(if $(MODE),$(MODE),policy)
SD_MODE ?= wide25
SD_FORCE_PIO ?= 0
SD_DMA_MODE ?= wrap
LOG_AUTO_FLUSH_BYTES ?= 65536
LOG_DISK_WRITES ?= 1
STORAGE_BOOT_MOUNT ?= 0
HCRTOS_MEDIA ?= native
MMC_HOST_IMPL ?= vendor

# FFmpeg is built for the HCRTOS/native hardware media path. Keep the ABI and
# warning knobs here so dependency updates do not hide local build policy.
HCRTOS_FFMPEG_ABI_CFLAGS ?= \
	-U__INT32_TYPE__ -U__UINT32_TYPE__ \
	-D__INT32_TYPE__=int -D__UINT32_TYPE__=unsigned \
	-D__have_long64=0 -D__have_longlong64=1
HCRTOS_FFMPEG_WARN_CFLAGS ?= \
	-Wno-error=incompatible-pointer-types \
	-Wno-array-parameter \
	-Wno-declaration-after-statement \
	-Wno-discarded-qualifiers \
	-Wno-format-truncation \
	-Wno-redundant-decls \
	-Wno-stringop-overread \
	-Wno-unused-but-set-variable \
	-Wno-unused-function \
	-Wno-unused-variable
HCRTOS_FFMPEG_DEMUXERS ?= \
	gsm mp3 aac ac3 avi asf amr ape amrnb amrwb dts dvbsub dvbtxt eac3 \
	flac flv h261 h263 h264 hls mjpeg m4v mov mpegps mpegts mpegtsraw \
	mjpeg_2000 mpegvideo mpjpeg matroska ogg pcm_alaw pcm_f32be pcm_f32le \
	pcm_f64be pcm_f64le pcm_mulaw pcm_s16be pcm_s16le pcm_s24be pcm_s24le \
	pcm_s32be pcm_s32le pcm_s8 pcm_u16be pcm_u16le pcm_u24be pcm_u24le \
	pcm_u32be pcm_u32le pcm_u8 rm spdif wav image2 image2pipe \
	image_bmp_pipe image_gif_pipe image_jpeg_pipe image_jpegls_pipe \
	image_png_pipe srt ass microdvd mpl2 sami webvtt vobsub ico lrc
HCRTOS_FFMPEG_PARSERS ?= \
	gsm aac aac_latm ac3 flac gif h261 h263 h264 jpeg2000 mjpeg \
	mpeg4video mpegaudio mpegvideo opus vc1 vorbis dvbsub dvdsub cook \
	webp vp8 rv34
HCRTOS_FFMPEG_DECODERS ?= \
	aac aac_fixed aac_latm alac ape flac mp1 mp2 mp3 \
	opus vorbis wavpack gsm gsm_ms bmp gif png mjpeg tiff \
	webp targa pcm_alaw pcm_bluray pcm_dvd pcm_f32be pcm_f32le pcm_f64be \
	pcm_f64le pcm_mulaw pcm_s16be pcm_s16be_planar pcm_s16le \
	pcm_s16le_planar pcm_s24be pcm_s24le pcm_s32be pcm_s32le pcm_s8 \
	pcm_u16be pcm_u16le pcm_u24be pcm_u24le pcm_u32be pcm_u32le pcm_u8 \
	h261 h263 h264 mpeg2video mpeg4 vp8 rv30 rv40 vc1 wmalossless wmapro \
	pgssub srt dvbsub dvdsub ass movtext ssa microdvd sami mpl2 webvtt \
	vplayer stl pjs subviewer1 text subrip adpcm_4xm adpcm_afc adpcm_agm \
	adpcm_aica adpcm_argo adpcm_ct adpcm_dtk adpcm_ea adpcm_ea_maxis_xa \
	adpcm_ea_r1 adpcm_ea_r2 adpcm_ea_r3 adpcm_ea_xas adpcm_ima_amv \
	adpcm_ima_apc adpcm_ima_apm adpcm_ima_cunning adpcm_ima_dat4 \
	adpcm_ima_dk3 adpcm_ima_dk4 adpcm_ima_ea_eacs adpcm_ima_ea_sead \
	adpcm_ima_iss adpcm_ima_moflex adpcm_ima_mtf adpcm_ima_oki \
	adpcm_ima_qt adpcm_ima_rad adpcm_ima_smjpeg adpcm_ima_wav \
	adpcm_ima_ws adpcm_ms adpcm_mtaf adpcm_psx adpcm_sbpro_2 \
	adpcm_sbpro_3 adpcm_sbpro_4 adpcm_swf adpcm_thp_le adpcm_thp \
	adpcm_xa adpcm_yamaha adpcm_zork

UNIFROG_DEFAULT_AUDIO ?= 1
UNIFROG_DEFAULT_CPU ?= 918
UNIFROG_DEFAULT_FRAMESKIP ?= 1
UNIFROG_DEFAULT_DISPLAY ?= 0
UNIFROG_DEFAULT_FRAMEBUFFER ?= 0
UNIFROG_DEFAULT_GAIN ?= 1
UNIFROG_DEFAULT_GE_CLOCK ?= -1
UNIFROG_DEFAULT_BACKLIGHT ?= -1
UNIFROG_DEFAULT_KEYMAP ?= 0
UNIFROG_DEFAULT_STATE_SLOT ?= 0
UNIFROG_DEFAULT_STATE_AUTO_LOAD ?= 0
UNIFROG_DEFAULT_STATE_AUTO_SAVE ?= 0
UNIFROG_DEFAULT_SORT_DESC ?= 0
UNIFROG_DEFAULT_SHOW_HIDDEN ?= 0
UNIFROG_DEFAULT_FOLDER_COUNTS ?= 0
UNIFROG_DEFAULT_MIXED_CONTENT ?= 1
UNIFROG_DEFAULT_DISPLAY_EMPTY_FOLDER ?= 1
UNIFROG_DEFAULT_MENU_COUNTER_FOLDER ?= 1
UNIFROG_DEFAULT_MENU_COUNTER_FILE ?= 1
UNIFROG_DEFAULT_CONTENT_COLLECT ?= 1
UNIFROG_DEFAULT_CONTENT_HISTORY ?= 1
UNIFROG_DEFAULT_CLOCK_ENABLED ?= 0
UNIFROG_DEFAULT_TITLE_INCLUDE_ROOT ?= 0
UNIFROG_DEFAULT_THEME_ALTERNATE ?= 0
UNIFROG_DEFAULT_BOXART_HIDDEN ?= 1
UNIFROG_DEFAULT_LAUNCH_SPLASH ?= 1
UNIFROG_DEFAULT_SOUND_ENABLED ?= 0
UNIFROG_DEFAULT_LOG_LEVEL ?= trace
UNIFROG_DEFAULT_LANGUAGE_NAME ?= english
UNIFROG_DEFAULT_THEME_NAME ?= muos
UNIFROG_DEFAULT_DEVICE_BOARD ?= auto
UNIFROG_DEFAULT_STORAGE_PROFILE ?= wide25
UNIFROG_DEFAULT_ROM_ROOT ?= /ROMS
UNIFROG_DEFAULT_ROM_ROOTS ?= /ROMS
UNIFROG_DEFAULT_ROM_ROOT_LABEL ?= ROMs
UNIFROG_DEFAULT_ROM_SYSTEM ?= gba:gpsp

# Packaged runtime media defaults. Power users override these on-device in
# /unifrog_data/unifrog.ini, not with Make command-line media flags.
override UNIFROG_DEFAULT_MEDIA_VIDEO_FEED_LEAD_MS := 500
override UNIFROG_DEFAULT_MEDIA_AUDIO_FEED_LEAD_MS := 3000
override UNIFROG_DEFAULT_MEDIA_VIDEO_KSHM_SIZE := 8388608
override UNIFROG_DEFAULT_MEDIA_VIDEO_LOWRES_KSHM_SIZE := $(UNIFROG_DEFAULT_MEDIA_VIDEO_KSHM_SIZE)
override UNIFROG_DEFAULT_MEDIA_FILE_BUFFER_SIZE := 65536
override UNIFROG_DEFAULT_MEDIA_FILE_BUFFER_MIN_SIZE := 16384
override UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_SIZE := 2097152
override UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_MIN_SIZE := 524288
override UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_SLOTS := 1
override UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_SIZE := 524288
override UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_MIN_SIZE := 262144
override UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_SLOTS := 16
override UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_TARGET_MS := 5000
override UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_MIN_BYTES := 524288
override UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_MAX_BYTES := 2097152
override UNIFROG_DEFAULT_MEDIA_VIDEO_PRELOAD_MAX_BYTES := 0
override UNIFROG_DEFAULT_MEDIA_AUDIO_MAX_HW_AHEAD_MS := 4000
override UNIFROG_DEFAULT_MEDIA_VIDEO_MAX_HW_AHEAD_MS := 4000
override UNIFROG_DEFAULT_MEDIA_SEEK_WARMUP_PACKETS := 96
override UNIFROG_DEFAULT_MEDIA_SEEK_VIDEO_WARMUP_PACKETS := 24
override UNIFROG_DEFAULT_MEDIA_SEEK_VIDEO_RECOVER_WARMUP_PACKETS := 8
override UNIFROG_DEFAULT_MEDIA_HW_AHEAD_MAX_WAIT_MS := 2500
override UNIFROG_DEFAULT_MEDIA_SEEK_SETTLE_MS := 140
override UNIFROG_DEFAULT_MEDIA_SEEK_ACCELERATE_FRAMES := 0
override UNIFROG_DEFAULT_MEDIA_SEEK_KEYFRAME_DROP_LIMIT := 240
override UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_DECODE_MS := 2500
override UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_HD_DECODE_MS := 900
override UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_KEYFRAME_MAX_BYTES := 65536
override UNIFROG_DEFAULT_MEDIA_VIDEO_STUCK_BEHIND_MS := 3000
override UNIFROG_DEFAULT_MEDIA_VIDEO_STALL_RECOVER_MS := 4500
override UNIFROG_DEFAULT_MEDIA_VIDEO_RECOVER_GAP_MS := 6000
override UNIFROG_DEFAULT_MEDIA_VIDEO_WRITE_RECOVER_MAX := 3
override UNIFROG_DEFAULT_MEDIA_VIDEO_WRITE_EPERM_RECOVER_MS := 120
override UNIFROG_DEFAULT_MEDIA_FILE_SLOW_READ_LOG_MS := 250
override UNIFROG_DEFAULT_MEDIA_AUDIO_BUFFERING_START_MS := 500
override UNIFROG_DEFAULT_MEDIA_AUDIO_BUFFERING_END_MS := 3000
override UNIFROG_DEFAULT_MEDIA_VIDEO_BUFFERING_START_MS := 500
override UNIFROG_DEFAULT_MEDIA_VIDEO_BUFFERING_END_MS := 3000
override UNIFROG_DEFAULT_MEDIA_RESET_VIDDEC_ON_FAIL := 1
override UNIFROG_DEFAULT_MEDIA_GB300_AUDDEC_PROBE_ONCE := 0

RUNTIME_SETTING_KEYS := \
	audio cpu frameskip display framebuffer gain ge_clock backlight keymap \
	state_slot state_auto_load state_auto_save rtc_offset_minutes sort_desc show_hidden \
	folder_counts mixed_content display_empty_folder menu_counter_folder \
	menu_counter_file content_collect content_history clock_enabled \
	title_include_root theme_alternate boxart_hidden launch_splash \
		sound_enabled log_level language_name theme_name artwork_layout \
		artwork_box_templates artwork_preview_templates artwork_text_templates \
		device_board storage_profile storage_normal_profile \
		storage_fallback_profile \
		battery_mv_empty battery_mv_25 battery_mv_50 battery_mv_75 \
		battery_mv_full battery_discharge_mv_per_hour battery_estimate_discharge \
		default_boot \
	rom_root rom_roots rom_root_label \
	rom_system \
	media_video_feed_lead_ms media_audio_feed_lead_ms \
	media_video_kshm_size media_video_lowres_kshm_size \
	media_file_buffer_size media_file_buffer_min_size \
	media_file_readahead_size media_file_readahead_min_size \
	media_file_readahead_slots media_video_readahead_size \
	media_video_readahead_min_size media_video_readahead_slots \
	media_video_prefill_target_ms media_video_prefill_min_bytes \
	media_video_prefill_max_bytes media_video_preload_max_bytes \
	media_audio_max_hw_ahead_ms media_video_max_hw_ahead_ms \
	media_seek_warmup_packets media_seek_video_warmup_packets \
	media_seek_video_recover_warmup_packets media_hw_ahead_max_wait_ms \
	media_seek_settle_ms media_seek_accelerate_frames \
	media_seek_keyframe_drop_limit media_seek_preroll_decode_ms \
	media_seek_preroll_hd_decode_ms \
	media_seek_preroll_keyframe_max_bytes media_video_stuck_behind_ms \
	media_video_stall_recover_ms media_video_recover_gap_ms \
	media_video_write_recover_max media_video_write_eperm_recover_ms \
	media_file_slow_read_log_ms media_audio_buffering_start_ms \
	media_audio_buffering_end_ms media_video_buffering_start_ms \
	media_video_buffering_end_ms media_reset_viddec_on_fail \
	media_gb300_auddec_probe_once

RUNTIME_SETTINGS_LINES := \
	'\# UniFrog runtime configuration defaults.' \
	'\# The installer copies this to /unifrog_data/unifrog.ini on first install.' \
	'\# Edit that file on the SD card or use Config/Open With on the device.' \
	'\# Lines beginning with \# or ; are comments. Values use key=value.' \
	'' \
	'\# Libretro launch defaults.' \
	'\# audio: 0/1. gain: integer multiplier from 0 through 4.' \
	'\# cpu MHz: 0, 198, 297, 396, 594, 702, 756, 808, 810, 864, or 918.' \
	'\# Zero uses the platform default.' \
	'\# frameskip: 0=off, 1=auto, 2=fixed one, 3=fixed two.' \
	'\# display: 0=fit, 1=stretch, 2=original.' \
	'\# framebuffer: 0=RGB565, 1=XRGB8888; ge_clock/backlight: -1=default.' \
	'\# keymap: 0=default, 1=RetroArch, 2=Genesis, 3=swap AB, 4=swap XY.' \
	'\# state_slot: 0..9; state_auto_load/state_auto_save: 0/1.' \
	'\# rtc_offset_minutes: -5270400..5270400; zero uses the device clock.' \
	'audio=$(UNIFROG_DEFAULT_AUDIO)' \
	'cpu=$(UNIFROG_DEFAULT_CPU)' \
	'frameskip=$(UNIFROG_DEFAULT_FRAMESKIP)' \
	'display=$(UNIFROG_DEFAULT_DISPLAY)' \
	'framebuffer=$(UNIFROG_DEFAULT_FRAMEBUFFER)' \
	'gain=$(UNIFROG_DEFAULT_GAIN)' \
	'ge_clock=$(UNIFROG_DEFAULT_GE_CLOCK)' \
	'backlight=$(UNIFROG_DEFAULT_BACKLIGHT)' \
	'keymap=$(UNIFROG_DEFAULT_KEYMAP)' \
	'state_slot=$(UNIFROG_DEFAULT_STATE_SLOT)' \
	'state_auto_load=$(UNIFROG_DEFAULT_STATE_AUTO_LOAD)' \
	'state_auto_save=$(UNIFROG_DEFAULT_STATE_AUTO_SAVE)' \
	'rtc_offset_minutes=0' \
	'' \
	'\# Browser and menu behavior. Every value in this group is 0 or 1.' \
	'\# sort_desc reverses sorting; show_hidden includes dot files.' \
	'\# folder_counts scans folders for counts; mixed_content shows media with ROMs.' \
	'\# display_empty_folder keeps empty folders visible.' \
	'\# menu_counter_folder/menu_counter_file show position counters.' \
	'\# content_collect enables favorites; content_history records recent content.' \
	'\# clock_enabled shows the clock; title_include_root includes the root name.' \
	'\# theme_alternate selects alternate theme styling; boxart_hidden hides art.' \
	'\# launch_splash shows loading screens; sound_enabled enables menu sounds.' \
		'\# log_level: trace, debug, info, warn, error, or off.' \
		'\# trace is the most detailed and is the default while UniFrog is in testing.' \
	'sort_desc=$(UNIFROG_DEFAULT_SORT_DESC)' \
	'show_hidden=$(UNIFROG_DEFAULT_SHOW_HIDDEN)' \
	'folder_counts=$(UNIFROG_DEFAULT_FOLDER_COUNTS)' \
	'mixed_content=$(UNIFROG_DEFAULT_MIXED_CONTENT)' \
	'display_empty_folder=$(UNIFROG_DEFAULT_DISPLAY_EMPTY_FOLDER)' \
	'menu_counter_folder=$(UNIFROG_DEFAULT_MENU_COUNTER_FOLDER)' \
	'menu_counter_file=$(UNIFROG_DEFAULT_MENU_COUNTER_FILE)' \
	'content_collect=$(UNIFROG_DEFAULT_CONTENT_COLLECT)' \
	'content_history=$(UNIFROG_DEFAULT_CONTENT_HISTORY)' \
	'clock_enabled=$(UNIFROG_DEFAULT_CLOCK_ENABLED)' \
	'title_include_root=$(UNIFROG_DEFAULT_TITLE_INCLUDE_ROOT)' \
	'theme_alternate=$(UNIFROG_DEFAULT_THEME_ALTERNATE)' \
	'boxart_hidden=$(UNIFROG_DEFAULT_BOXART_HIDDEN)' \
	'launch_splash=$(UNIFROG_DEFAULT_LAUNCH_SPLASH)' \
	'sound_enabled=$(UNIFROG_DEFAULT_SOUND_ENABLED)' \
		'log_level=$(UNIFROG_DEFAULT_LOG_LEVEL)' \
	'' \
	'\# Theme, language, storage, and ROM roots.' \
	'\# language_name and theme_name select matching files/directories by name.' \
	'language_name=$(UNIFROG_DEFAULT_LANGUAGE_NAME)' \
	'theme_name=$(UNIFROG_DEFAULT_THEME_NAME)' \
	'' \
	'\# Artwork templates are searched left-to-right; | separates fallbacks.' \
	'\# Tokens: {rom_dir}, {system}, {name}, and {filename}.' \
	'\# artwork_layout: muos, skraper, beside, or custom.' \
	'artwork_layout=muos' \
	'artwork_box_templates=unifrog_data/artwork/{system}/box/{name}.png|MUOS/info/catalogue/{system}/box/{name}.png|muos/info/catalogue/{system}/box/{name}.png' \
	'artwork_preview_templates=unifrog_data/artwork/{system}/preview/{name}.png|MUOS/info/catalogue/{system}/preview/{name}.png|muos/info/catalogue/{system}/preview/{name}.png' \
	'artwork_text_templates=unifrog_data/artwork/{system}/text/{name}.txt|MUOS/info/catalogue/{system}/text/{name}.txt|muos/info/catalogue/{system}/text/{name}.txt' \
	'\# device_board: auto, sf2000, gb300. Use sf2000 for an SF2000 with a GB300 replacement screen.' \
	'device_board=$(UNIFROG_DEFAULT_DEVICE_BOARD)' \
	'\# Storage profiles are runtime settings. The build boots with wide25.' \
	'\# Profiles: auto, boot, safe, wide1, wide2, wide4, wide8, wide10,' \
	'\# wide12, wide14, wide16, wide18, wide20, wide22, wide24, wide25,' \
	'\# wide37, hs1, wide50, wide, uhs12, uhs25, or uhs.' \
	'storage_profile=$(UNIFROG_DEFAULT_STORAGE_PROFILE)' \
	'\# Auto mode uses storage_normal_profile and tries storage_fallback_profile' \
	'\# only when the normal profile fails. Battery warnings never change SD mode.' \
	'storage_normal_profile=wide25' \
	'storage_fallback_profile=safe' \
		'\# Battery calibration points are millivolts at 0, 25, 50, 75, and 100 percent.' \
		'\# Each accepts 2500..5000 and values must be strictly increasing.' \
		'\# Low-battery warnings require three consecutive samples and never change SD mode.' \
		'battery_mv_empty=3500' \
		'battery_mv_25=3660' \
		'battery_mv_50=3720' \
		'battery_mv_75=3800' \
		'battery_mv_full=4000' \
		'\# Discharge rate accepts 0..2000 mV/hour; 0 disables remaining-time display.' \
		'\# battery_estimate_discharge: 1=learn filtered rate, 0=use configured rate.' \
		'battery_discharge_mv_per_hour=120' \
		'battery_estimate_discharge=1' \
		'\# Default boot: unifrog or an SD-root-relative .asd path.' \
		'\# Hold B at power-on to cancel the default and open UniFrog.' \
		'\# On-device: Power > Firmware Boot, highlight firmware, then press X.' \
		'default_boot=unifrog' \
	'\# rom_root is the primary root; rom_roots is a |-separated ordered list.' \
	'\# rom_system=FOLDER:CORE_ID chooses a core for every ROM below that folder.' \
	'\# Names are case-insensitive. Later duplicate entries replace earlier ones.' \
	'\# This takes precedence over generic extension rules, which is essential for' \
	'\# .zip files because the archive filename alone does not identify a system.' \
	'\# Tiny Set Go arcade files target MAME 2003 Plus. UniFrog currently provides' \
	'\# MAME 2000 and FB Alpha 2012, so some games need a matching ROM-set version.' \
	'\# Change ARCADE to mame2000 when using a matching MAME 0.37b5 set.' \
	'rom_root=$(UNIFROG_DEFAULT_ROM_ROOT)' \
	'rom_roots=$(UNIFROG_DEFAULT_ROM_ROOTS)' \
	'rom_root_label=$(UNIFROG_DEFAULT_ROM_ROOT_LABEL)' \
	'rom_system=ARCADE:fbalpha2012' \
	'rom_system=NEOGEO:fbalpha2012' \
	'rom_system=ATARI:stella2014' \
	'rom_system=GB:gambatte' \
	'rom_system=GBC:gambatte' \
	'rom_system=FC:quicknes' \
	'rom_system=NES:quicknes' \
	'rom_system=SFC:snes9x2005' \
	'rom_system=SNES:snes9x2005' \
	'rom_system=GG:picodrive' \
	'rom_system=PCE:pce-fast' \
	'rom_system=TG16:pce-fast' \
	'rom_system=PSX:qpsx' \
	'rom_system=$(UNIFROG_DEFAULT_ROM_SYSTEM)' \
	'' \
	'\# Advanced media tuning. Normally leave these unchanged.' \
	'\# _ms values are milliseconds; _size/_bytes are bytes; booleans use 0/1.' \
	'\# Unsigned/size values: 0..4294967295; signed values: -2147483648..2147483647.' \
	'\# Feed lead controls how far compressed streams are queued ahead.' \
	'media_video_feed_lead_ms=$(UNIFROG_DEFAULT_MEDIA_VIDEO_FEED_LEAD_MS)' \
	'media_audio_feed_lead_ms=$(UNIFROG_DEFAULT_MEDIA_AUDIO_FEED_LEAD_MS)' \
	'\# KSHM values size hardware decoder compressed-data rings.' \
	'media_video_kshm_size=$(UNIFROG_DEFAULT_MEDIA_VIDEO_KSHM_SIZE)' \
	'media_video_lowres_kshm_size=$(UNIFROG_DEFAULT_MEDIA_VIDEO_LOWRES_KSHM_SIZE)' \
	'\# General file buffering and readahead sizes, minima, and slot counts.' \
	'media_file_buffer_size=$(UNIFROG_DEFAULT_MEDIA_FILE_BUFFER_SIZE)' \
	'media_file_buffer_min_size=$(UNIFROG_DEFAULT_MEDIA_FILE_BUFFER_MIN_SIZE)' \
	'media_file_readahead_size=$(UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_SIZE)' \
	'media_file_readahead_min_size=$(UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_MIN_SIZE)' \
	'media_file_readahead_slots=$(UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_SLOTS)' \
	'\# Video-specific readahead sizes, minima, and slot counts.' \
	'media_video_readahead_size=$(UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_SIZE)' \
	'media_video_readahead_min_size=$(UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_MIN_SIZE)' \
	'media_video_readahead_slots=$(UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_SLOTS)' \
	'\# Initial video prefill target and byte limits; preload 0 disables it.' \
	'media_video_prefill_target_ms=$(UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_TARGET_MS)' \
	'media_video_prefill_min_bytes=$(UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_MIN_BYTES)' \
	'media_video_prefill_max_bytes=$(UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_MAX_BYTES)' \
	'media_video_preload_max_bytes=$(UNIFROG_DEFAULT_MEDIA_VIDEO_PRELOAD_MAX_BYTES)' \
	'\# Maximum duration queued ahead in the audio and video hardware.' \
	'media_audio_max_hw_ahead_ms=$(UNIFROG_DEFAULT_MEDIA_AUDIO_MAX_HW_AHEAD_MS)' \
	'media_video_max_hw_ahead_ms=$(UNIFROG_DEFAULT_MEDIA_VIDEO_MAX_HW_AHEAD_MS)' \
	'\# Seek warmup packet counts and maximum wait for hardware queues.' \
	'media_seek_warmup_packets=$(UNIFROG_DEFAULT_MEDIA_SEEK_WARMUP_PACKETS)' \
	'media_seek_video_warmup_packets=$(UNIFROG_DEFAULT_MEDIA_SEEK_VIDEO_WARMUP_PACKETS)' \
	'media_seek_video_recover_warmup_packets=$(UNIFROG_DEFAULT_MEDIA_SEEK_VIDEO_RECOVER_WARMUP_PACKETS)' \
	'media_hw_ahead_max_wait_ms=$(UNIFROG_DEFAULT_MEDIA_HW_AHEAD_MAX_WAIT_MS)' \
	'\# Seek settle/catch-up behavior and preroll limits.' \
	'media_seek_settle_ms=$(UNIFROG_DEFAULT_MEDIA_SEEK_SETTLE_MS)' \
	'media_seek_accelerate_frames=$(UNIFROG_DEFAULT_MEDIA_SEEK_ACCELERATE_FRAMES)' \
	'media_seek_keyframe_drop_limit=$(UNIFROG_DEFAULT_MEDIA_SEEK_KEYFRAME_DROP_LIMIT)' \
	'media_seek_preroll_decode_ms=$(UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_DECODE_MS)' \
	'media_seek_preroll_hd_decode_ms=$(UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_HD_DECODE_MS)' \
	'media_seek_preroll_keyframe_max_bytes=$(UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_KEYFRAME_MAX_BYTES)' \
	'\# Video stall detection and decoder-write recovery thresholds.' \
	'media_video_stuck_behind_ms=$(UNIFROG_DEFAULT_MEDIA_VIDEO_STUCK_BEHIND_MS)' \
	'media_video_stall_recover_ms=$(UNIFROG_DEFAULT_MEDIA_VIDEO_STALL_RECOVER_MS)' \
	'media_video_recover_gap_ms=$(UNIFROG_DEFAULT_MEDIA_VIDEO_RECOVER_GAP_MS)' \
	'media_video_write_recover_max=$(UNIFROG_DEFAULT_MEDIA_VIDEO_WRITE_RECOVER_MAX)' \
	'media_video_write_eperm_recover_ms=$(UNIFROG_DEFAULT_MEDIA_VIDEO_WRITE_EPERM_RECOVER_MS)' \
	'\# Log reads slower than this threshold.' \
	'media_file_slow_read_log_ms=$(UNIFROG_DEFAULT_MEDIA_FILE_SLOW_READ_LOG_MS)' \
	'\# Decoder buffering windows. Signed integers; -1 uses backend behavior.' \
	'media_audio_buffering_start_ms=$(UNIFROG_DEFAULT_MEDIA_AUDIO_BUFFERING_START_MS)' \
	'media_audio_buffering_end_ms=$(UNIFROG_DEFAULT_MEDIA_AUDIO_BUFFERING_END_MS)' \
	'media_video_buffering_start_ms=$(UNIFROG_DEFAULT_MEDIA_VIDEO_BUFFERING_START_MS)' \
	'media_video_buffering_end_ms=$(UNIFROG_DEFAULT_MEDIA_VIDEO_BUFFERING_END_MS)' \
	'\# Decoder recovery/probing switches, both boolean 0/1.' \
	'media_reset_viddec_on_fail=$(UNIFROG_DEFAULT_MEDIA_RESET_VIDDEC_ON_FAIL)' \
	'media_gb300_auddec_probe_once=$(UNIFROG_DEFAULT_MEDIA_GB300_AUDDEC_PROBE_ONCE)' \
	'' \
	'[frogui]' \
	'\# FrogUI alternative frontend appearance. Change these in FrogUI Settings' \
	'\# or here. theme accepts a built-in FrogUI theme name: MinUI Style,' \
	'\# Emerald, Orange, Golden, Rose, Purple, Green, Red, Commodore 64,' \
	'\# Game Boy, or NES. font accepts GamePocket or Monogram.' \
	'\# FrogUI uses global rom_root and repeated rom_system=FOLDER:CORE_ID values.' \
	'theme=MinUI Style' \
	'font=GamePocket'

CONFIG_EXAMPLE_LINES := \
	'\# UniFrog local build options example.' \
	'\# Copy selected lines into untracked config.mk when needed.' \
	'' \
	'TOOLCHAIN ?= $(TOOLCHAIN)' \
	'SDK ?= $(SDK)' \
	'DEPS ?= $(DEPS)' \
	'DEP_CHECKOUT ?= $(DEP_CHECKOUT)' \
	'DEP_DEPTH ?= $(DEP_DEPTH)' \
	'CORE_IDS ?= $(CORE_IDS)' \
	'BUILD_PROGRESS ?= $(BUILD_PROGRESS)' \
	'' \
	'HCRTOS_MEDIA ?= $(HCRTOS_MEDIA)' \
	'LOG_AUTO_FLUSH_BYTES ?= $(LOG_AUTO_FLUSH_BYTES)' \
	'LOG_DISK_WRITES ?= $(LOG_DISK_WRITES)'

DEFAULT_OPTIONS_HEADER_LINES := \
	'\#ifndef UNIFROG_DEFAULT_OPTIONS_H' \
	'\#define UNIFROG_DEFAULT_OPTIONS_H' \
	'' \
	'\#define UNIFROG_DEFAULT_AUDIO $(UNIFROG_DEFAULT_AUDIO)' \
	'\#define UNIFROG_DEFAULT_CPU $(UNIFROG_DEFAULT_CPU)' \
	'\#define UNIFROG_DEFAULT_FRAMESKIP $(UNIFROG_DEFAULT_FRAMESKIP)' \
	'\#define UNIFROG_DEFAULT_DISPLAY $(UNIFROG_DEFAULT_DISPLAY)' \
	'\#define UNIFROG_DEFAULT_FRAMEBUFFER $(UNIFROG_DEFAULT_FRAMEBUFFER)' \
	'\#define UNIFROG_DEFAULT_GAIN $(UNIFROG_DEFAULT_GAIN)' \
	'\#define UNIFROG_DEFAULT_GE_CLOCK $(UNIFROG_DEFAULT_GE_CLOCK)' \
	'\#define UNIFROG_DEFAULT_BACKLIGHT $(UNIFROG_DEFAULT_BACKLIGHT)' \
	'\#define UNIFROG_DEFAULT_KEYMAP $(UNIFROG_DEFAULT_KEYMAP)' \
	'\#define UNIFROG_DEFAULT_STATE_SLOT $(UNIFROG_DEFAULT_STATE_SLOT)' \
	'\#define UNIFROG_DEFAULT_STATE_AUTO_LOAD $(UNIFROG_DEFAULT_STATE_AUTO_LOAD)' \
	'\#define UNIFROG_DEFAULT_STATE_AUTO_SAVE $(UNIFROG_DEFAULT_STATE_AUTO_SAVE)' \
	'\#define UNIFROG_DEFAULT_SORT_DESC $(UNIFROG_DEFAULT_SORT_DESC)' \
	'\#define UNIFROG_DEFAULT_SHOW_HIDDEN $(UNIFROG_DEFAULT_SHOW_HIDDEN)' \
	'\#define UNIFROG_DEFAULT_FOLDER_COUNTS $(UNIFROG_DEFAULT_FOLDER_COUNTS)' \
	'\#define UNIFROG_DEFAULT_MIXED_CONTENT $(UNIFROG_DEFAULT_MIXED_CONTENT)' \
	'\#define UNIFROG_DEFAULT_DISPLAY_EMPTY_FOLDER $(UNIFROG_DEFAULT_DISPLAY_EMPTY_FOLDER)' \
	'\#define UNIFROG_DEFAULT_MENU_COUNTER_FOLDER $(UNIFROG_DEFAULT_MENU_COUNTER_FOLDER)' \
	'\#define UNIFROG_DEFAULT_MENU_COUNTER_FILE $(UNIFROG_DEFAULT_MENU_COUNTER_FILE)' \
	'\#define UNIFROG_DEFAULT_CONTENT_COLLECT $(UNIFROG_DEFAULT_CONTENT_COLLECT)' \
	'\#define UNIFROG_DEFAULT_CONTENT_HISTORY $(UNIFROG_DEFAULT_CONTENT_HISTORY)' \
	'\#define UNIFROG_DEFAULT_CLOCK_ENABLED $(UNIFROG_DEFAULT_CLOCK_ENABLED)' \
	'\#define UNIFROG_DEFAULT_TITLE_INCLUDE_ROOT $(UNIFROG_DEFAULT_TITLE_INCLUDE_ROOT)' \
	'\#define UNIFROG_DEFAULT_THEME_ALTERNATE $(UNIFROG_DEFAULT_THEME_ALTERNATE)' \
	'\#define UNIFROG_DEFAULT_BOXART_HIDDEN $(UNIFROG_DEFAULT_BOXART_HIDDEN)' \
	'\#define UNIFROG_DEFAULT_LAUNCH_SPLASH $(UNIFROG_DEFAULT_LAUNCH_SPLASH)' \
	'\#define UNIFROG_DEFAULT_SOUND_ENABLED $(UNIFROG_DEFAULT_SOUND_ENABLED)' \
		'\#define UNIFROG_DEFAULT_LOG_LEVEL "$(UNIFROG_DEFAULT_LOG_LEVEL)"' \
	'\#define UNIFROG_DEFAULT_LANGUAGE_NAME "$(UNIFROG_DEFAULT_LANGUAGE_NAME)"' \
	'\#define UNIFROG_DEFAULT_THEME_NAME "$(UNIFROG_DEFAULT_THEME_NAME)"' \
	'\#define UNIFROG_DEFAULT_DEVICE_BOARD "$(UNIFROG_DEFAULT_DEVICE_BOARD)"' \
	'\#define UNIFROG_DEFAULT_STORAGE_PROFILE "$(UNIFROG_DEFAULT_STORAGE_PROFILE)"' \
	'\#define UNIFROG_DEFAULT_ROM_ROOT "$(UNIFROG_DEFAULT_ROM_ROOT)"' \
	'\#define UNIFROG_DEFAULT_ROM_ROOT_LABEL "$(UNIFROG_DEFAULT_ROM_ROOT_LABEL)"' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_FEED_LEAD_MS $(UNIFROG_DEFAULT_MEDIA_VIDEO_FEED_LEAD_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_AUDIO_FEED_LEAD_MS $(UNIFROG_DEFAULT_MEDIA_AUDIO_FEED_LEAD_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_KSHM_SIZE $(UNIFROG_DEFAULT_MEDIA_VIDEO_KSHM_SIZE)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_LOWRES_KSHM_SIZE $(UNIFROG_DEFAULT_MEDIA_VIDEO_LOWRES_KSHM_SIZE)' \
	'\#define UNIFROG_DEFAULT_MEDIA_FILE_BUFFER_SIZE $(UNIFROG_DEFAULT_MEDIA_FILE_BUFFER_SIZE)' \
	'\#define UNIFROG_DEFAULT_MEDIA_FILE_BUFFER_MIN_SIZE $(UNIFROG_DEFAULT_MEDIA_FILE_BUFFER_MIN_SIZE)' \
	'\#define UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_SIZE $(UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_SIZE)' \
	'\#define UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_MIN_SIZE $(UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_MIN_SIZE)' \
	'\#define UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_SLOTS $(UNIFROG_DEFAULT_MEDIA_FILE_READAHEAD_SLOTS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_SIZE $(UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_SIZE)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_MIN_SIZE $(UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_MIN_SIZE)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_SLOTS $(UNIFROG_DEFAULT_MEDIA_VIDEO_READAHEAD_SLOTS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_TARGET_MS $(UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_TARGET_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_MIN_BYTES $(UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_MIN_BYTES)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_MAX_BYTES $(UNIFROG_DEFAULT_MEDIA_VIDEO_PREFILL_MAX_BYTES)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_PRELOAD_MAX_BYTES $(UNIFROG_DEFAULT_MEDIA_VIDEO_PRELOAD_MAX_BYTES)' \
	'\#define UNIFROG_DEFAULT_MEDIA_AUDIO_MAX_HW_AHEAD_MS $(UNIFROG_DEFAULT_MEDIA_AUDIO_MAX_HW_AHEAD_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_MAX_HW_AHEAD_MS $(UNIFROG_DEFAULT_MEDIA_VIDEO_MAX_HW_AHEAD_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_SEEK_WARMUP_PACKETS $(UNIFROG_DEFAULT_MEDIA_SEEK_WARMUP_PACKETS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_SEEK_VIDEO_WARMUP_PACKETS $(UNIFROG_DEFAULT_MEDIA_SEEK_VIDEO_WARMUP_PACKETS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_SEEK_VIDEO_RECOVER_WARMUP_PACKETS $(UNIFROG_DEFAULT_MEDIA_SEEK_VIDEO_RECOVER_WARMUP_PACKETS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_HW_AHEAD_MAX_WAIT_MS $(UNIFROG_DEFAULT_MEDIA_HW_AHEAD_MAX_WAIT_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_SEEK_SETTLE_MS $(UNIFROG_DEFAULT_MEDIA_SEEK_SETTLE_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_SEEK_ACCELERATE_FRAMES $(UNIFROG_DEFAULT_MEDIA_SEEK_ACCELERATE_FRAMES)' \
	'\#define UNIFROG_DEFAULT_MEDIA_SEEK_KEYFRAME_DROP_LIMIT $(UNIFROG_DEFAULT_MEDIA_SEEK_KEYFRAME_DROP_LIMIT)' \
	'\#define UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_DECODE_MS $(UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_DECODE_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_HD_DECODE_MS $(UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_HD_DECODE_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_KEYFRAME_MAX_BYTES $(UNIFROG_DEFAULT_MEDIA_SEEK_PREROLL_KEYFRAME_MAX_BYTES)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_STUCK_BEHIND_MS $(UNIFROG_DEFAULT_MEDIA_VIDEO_STUCK_BEHIND_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_STALL_RECOVER_MS $(UNIFROG_DEFAULT_MEDIA_VIDEO_STALL_RECOVER_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_RECOVER_GAP_MS $(UNIFROG_DEFAULT_MEDIA_VIDEO_RECOVER_GAP_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_WRITE_RECOVER_MAX $(UNIFROG_DEFAULT_MEDIA_VIDEO_WRITE_RECOVER_MAX)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_WRITE_EPERM_RECOVER_MS $(UNIFROG_DEFAULT_MEDIA_VIDEO_WRITE_EPERM_RECOVER_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_FILE_SLOW_READ_LOG_MS $(UNIFROG_DEFAULT_MEDIA_FILE_SLOW_READ_LOG_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_AUDIO_BUFFERING_START_MS $(UNIFROG_DEFAULT_MEDIA_AUDIO_BUFFERING_START_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_AUDIO_BUFFERING_END_MS $(UNIFROG_DEFAULT_MEDIA_AUDIO_BUFFERING_END_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_BUFFERING_START_MS $(UNIFROG_DEFAULT_MEDIA_VIDEO_BUFFERING_START_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_VIDEO_BUFFERING_END_MS $(UNIFROG_DEFAULT_MEDIA_VIDEO_BUFFERING_END_MS)' \
	'\#define UNIFROG_DEFAULT_MEDIA_RESET_VIDDEC_ON_FAIL $(UNIFROG_DEFAULT_MEDIA_RESET_VIDDEC_ON_FAIL)' \
	'\#define UNIFROG_DEFAULT_MEDIA_GB300_AUDDEC_PROBE_ONCE $(UNIFROG_DEFAULT_MEDIA_GB300_AUDDEC_PROBE_ONCE)' \
	'' \
	'\#endif'
