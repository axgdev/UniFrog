CORE_SOURCE_ROOT ?= ../.deps/cores
CORE_SUPPORT_ROOT ?= ../.deps/support

# name|checkout-directory|upstream-url|pin-policy|upstream-commit|sparse-paths
# Use ":files /patterns" for sparse file patterns when cone mode would pull
# extra platform assets.
SUPPORT_SPECS := \
	'libchdr|$(CORE_SUPPORT_ROOT)/libchdr|https://github.com/rtissera/libchdr.git|head|25fcb10cde7b9af71dbbb76d68033a4975af0a08|include src deps/lzma-25.01/include deps/lzma-25.01/src' \
	'zstd|$(CORE_SUPPORT_ROOT)/zstd|https://github.com/facebook/zstd.git|tag|f8745da6ff1ad1e7bab384bd1f9d742439278e99|build/single_file_libs lib' \
	'zlib|$(CORE_SUPPORT_ROOT)/zlib|https://github.com/madler/zlib.git|tag|da607da739fa6047df13e66a2af6b8bec7c2a498|:root-files'

# name|checkout-directory|upstream-url|pin-policy|upstream-commit|sparse-paths
CORE_SPECS := \
	'libretro-common|$(CORE_SOURCE_ROOT)/libretro-common|https://github.com/libretro/libretro-common.git|head|e2e3eccfd245a04771e6a435320b42234c8cc4d7|compat encodings file formats streams string time vfs include' \
	'gambatte|$(CORE_SOURCE_ROOT)/gambatte-libretro|https://github.com/libretro/gambatte-libretro.git|head|2147d9257911b484b07666994ceecc4c5a2cb318|common libgambatte' \
	'gpsp|$(CORE_SOURCE_ROOT)/gpsp-libretro|https://github.com/libretro/gpsp.git|head|eca3bee1e2d2043d42f0480012c1e7ec85498f88|.' \
	'gpsp_multicore|$(CORE_SOURCE_ROOT)/gpsp_multicore|https://github.com/tzubertowski/gpsp_multicore.git|head|63dd94953c27bb2664872331bbc7f212a088db4b|.' \
	'picodrive|$(CORE_SOURCE_ROOT)/picodrive|https://github.com/libretro/picodrive.git|head|f0d4a0118a9733a1f10bce5a4ac772c474f9300d|cpu/cyclone cpu/cz80 cpu/drc cpu/fame cpu/sh2 pico platform/common platform/libretro unzip zlib' \
	'snes9x2005|$(CORE_SOURCE_ROOT)/snes9x2005|https://github.com/libretro/snes9x2005.git|head|b60356971fc9caae02cd0853676dced886a08be7|.' \
	'snes9x2002|$(CORE_SOURCE_ROOT)/snes9x2002|https://github.com/libretro/snes9x2002.git|head|39e0d8c6daf4b1b1302eeecfee8309570aeb6a82|.' \
	'quicknes|$(CORE_SOURCE_ROOT)/QuickNES_Core|https://github.com/libretro/QuickNES_Core.git|head|7848e1ac22b1c69d056ae4cb57710651ff1dd169|.' \
	'fceumm|$(CORE_SOURCE_ROOT)/libretro-fceumm|https://github.com/libretro/libretro-fceumm.git|head|3a84a6fd0ba20dd4877c06b1d58741172148395f|src' \
	'gearboy|$(CORE_SOURCE_ROOT)/Gearboy|https://github.com/drhelius/Gearboy.git|tag|36f9faf04bcb6c023176de12dddae99ffc1ceb10|:files /LICENSE /src/** /platforms/libretro/Makefile /platforms/libretro/Makefile.common /platforms/libretro/libretro.cpp /platforms/libretro/libretro.h /platforms/libretro/libretro_core_options.h /platforms/libretro/link.T /platforms/shared/dependencies/miniz/**' \
	'pce-fast|$(CORE_SOURCE_ROOT)/beetle-pce-fast-libretro|https://github.com/libretro/beetle-pce-fast-libretro.git|head|9ba79648d6ec85e833aef719d7f359117498d89c|mednafen libretro-common' \
	'qpsx|$(CORE_SOURCE_ROOT)/sf2000-qpsx-playstation-emulator|https://github.com/angree/sf2000-qpsx-playstation-emulator.git|head|368310aa1b94fe764b8fdf4ddbd7afd06d7bd2a1|src libretro'

SMOKE_SUPPORT_SPECS := \
	'zlib|$(CORE_SUPPORT_ROOT)/zlib|https://github.com/madler/zlib.git|tag|da607da739fa6047df13e66a2af6b8bec7c2a498|:root-files'

SMOKE_CORE_SPECS := \
	'libretro-common|$(CORE_SOURCE_ROOT)/libretro-common|https://github.com/libretro/libretro-common.git|head|e2e3eccfd245a04771e6a435320b42234c8cc4d7|compat encodings file formats streams string time vfs include' \
	'quicknes|$(CORE_SOURCE_ROOT)/QuickNES_Core|https://github.com/libretro/QuickNES_Core.git|head|7848e1ac22b1c69d056ae4cb57710651ff1dd169|.'
