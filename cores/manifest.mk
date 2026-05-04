CORE_SOURCE_ROOT ?= ../.deps/cores

# name|checkout-directory|upstream-url|upstream-commit
CORE_SPECS := \
	'libretro-common|$(CORE_SOURCE_ROOT)/libretro-common|https://github.com/libretro/libretro-common.git|0a59f5a69929019cb9917fab60b935e9952b2f40' \
	'gambatte|$(CORE_SOURCE_ROOT)/gambatte-libretro|https://github.com/libretro/gambatte-libretro.git|2147d9257911b484b07666994ceecc4c5a2cb318' \
	'gpsp|$(CORE_SOURCE_ROOT)/gpsp-libretro|https://github.com/libretro/gpsp.git|eca3bee1e2d2043d42f0480012c1e7ec85498f88' \
	'picodrive|$(CORE_SOURCE_ROOT)/picodrive|https://github.com/libretro/picodrive.git|f0d4a0118a9733a1f10bce5a4ac772c474f9300d' \
	'snes9x2005|$(CORE_SOURCE_ROOT)/snes9x2005|https://github.com/libretro/snes9x2005.git|b60356971fc9caae02cd0853676dced886a08be7' \
	'snes9x2002|$(CORE_SOURCE_ROOT)/snes9x2002|https://github.com/libretro/snes9x2002.git|39e0d8c6daf4b1b1302eeecfee8309570aeb6a82' \
	'quicknes|$(CORE_SOURCE_ROOT)/QuickNES_Core|https://github.com/libretro/QuickNES_Core.git|7848e1ac22b1c69d056ae4cb57710651ff1dd169' \
	'fceumm|$(CORE_SOURCE_ROOT)/libretro-fceumm|https://github.com/libretro/libretro-fceumm.git|a3467f6af9d44a1c6de51202b35524f31cc27a8e' \
	'gearboy|$(CORE_SOURCE_ROOT)/Gearboy|https://github.com/libretro/Gearboy.git|368521ef59f3a0e600346ba22041b05331188275' \
	'pce-fast|$(CORE_SOURCE_ROOT)/beetle-pce-fast-libretro|https://github.com/libretro/beetle-pce-fast-libretro.git|9ba79648d6ec85e833aef719d7f359117498d89c' \
	'qpsx|$(CORE_SOURCE_ROOT)/sf2000-qpsx-playstation-emulator|https://github.com/angree/sf2000-qpsx-playstation-emulator.git|368310aa1b94fe764b8fdf4ddbd7afd06d7bd2a1' \
	'pmp-video|$(CORE_SOURCE_ROOT)/sf2000-video-player|https://github.com/angree/sf2000-video-player.git|3fba73781f3b502642ef7eb7748ab0dca357926d'
