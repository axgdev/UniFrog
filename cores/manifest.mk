CORE_SOURCE_URL ?= https://github.com/axgdev/UniFrogCores.git
CORE_SOURCE_ROOT ?= ../.deps/cores

# name|dir|branch
CORE_SPECS := \
	'libretro-common|$(CORE_SOURCE_ROOT)/libretro-common|unifrog/libretro-common' \
	'gambatte|$(CORE_SOURCE_ROOT)/gambatte-libretro|unifrog/gambatte' \
	'gpsp|$(CORE_SOURCE_ROOT)/gpsp-libretro|unifrog/gpsp' \
	'picodrive|$(CORE_SOURCE_ROOT)/picodrive|unifrog/picodrive' \
	'snes9x2005|$(CORE_SOURCE_ROOT)/snes9x2005|unifrog/snes9x2005' \
	'snes9x2002|$(CORE_SOURCE_ROOT)/snes9x2002|unifrog/snes9x2002' \
	'quicknes|$(CORE_SOURCE_ROOT)/QuickNES_Core|unifrog/quicknes' \
	'fceumm|$(CORE_SOURCE_ROOT)/libretro-fceumm|unifrog/fceumm' \
	'gearboy|$(CORE_SOURCE_ROOT)/Gearboy|unifrog/gearboy' \
	'pce-fast|$(CORE_SOURCE_ROOT)/beetle-pce-fast-libretro|unifrog/pce-fast' \
	'qpsx|$(CORE_SOURCE_ROOT)/sf2000-qpsx-playstation-emulator|unifrog/qpsx' \
	'pmp-video|$(CORE_SOURCE_ROOT)/sf2000-video-player|unifrog/pmp-video'
