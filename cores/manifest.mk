CORE_SOURCE_ROOT ?= ../.deps/cores
CORE_SUPPORT_ROOT ?= ../.deps/support

# Dependency specs:
# name|checkout-directory|upstream-url|pin-policy|upstream-ref|upstream-commit|patch-dir|sparse-paths
#
# upstream-ref is human-facing metadata, so prefer tags when a dependency is
# pinned to a release. upstream-commit is the exact reproducible build input.
# patch-dir is "-" when the dependency has no UniFrog patch queue.
# Use ":files /patterns" for sparse file patterns when cone mode would pull
# extra platform assets.
SUPPORT_SPECS := \
	'lz4|$(CORE_SUPPORT_ROOT)/lz4|https://github.com/lz4/lz4.git|tag|v1.10.0|ebb370ca83af193212df4dcbadcc5d87bc0de2f0|-|lib' \
	'stb|$(CORE_SUPPORT_ROOT)/stb|https://github.com/nothings/stb.git|head|31c1ad37456438565541f4919958214b6e762fb4|31c1ad37456438565541f4919958214b6e762fb4|-|:files /stb_image.h /stb_truetype.h' \
	'nanosvg|$(CORE_SUPPORT_ROOT)/nanosvg|https://github.com/memononen/nanosvg.git|head|48120e91e64b2f409ed600cdfd6d790a49ba11ab|48120e91e64b2f409ed600cdfd6d790a49ba11ab|-|:files /LICENSE.txt /README.md /src/nanosvg.h /src/nanosvgrast.h' \
	'libchdr|$(CORE_SUPPORT_ROOT)/libchdr|https://github.com/rtissera/libchdr.git|head|25fcb10cde7b9af71dbbb76d68033a4975af0a08|25fcb10cde7b9af71dbbb76d68033a4975af0a08|-|include src deps/lzma-25.01/include deps/lzma-25.01/src' \
	'zstd|$(CORE_SUPPORT_ROOT)/zstd|https://github.com/facebook/zstd.git|tag|v1.5.7|f8745da6ff1ad1e7bab384bd1f9d742439278e99|-|build/single_file_libs lib' \
	'zlib|$(CORE_SUPPORT_ROOT)/zlib|https://github.com/madler/zlib.git|tag|v1.3.2|da607da739fa6047df13e66a2af6b8bec7c2a498|-|:root-files'

CORE_SPECS := \
	'libretro-common|$(CORE_SOURCE_ROOT)/libretro-common|https://github.com/libretro/libretro-common.git|head|e2e3eccfd245a04771e6a435320b42234c8cc4d7|e2e3eccfd245a04771e6a435320b42234c8cc4d7|patches/libretro-common|compat encodings file formats streams string time vfs include' \
	'gambatte|$(CORE_SOURCE_ROOT)/gambatte-libretro|https://github.com/libretro/gambatte-libretro.git|head|2147d9257911b484b07666994ceecc4c5a2cb318|2147d9257911b484b07666994ceecc4c5a2cb318|patches/gambatte|common libgambatte' \
	'gpsp|$(CORE_SOURCE_ROOT)/gpsp-libretro|https://github.com/libretro/gpsp.git|head|eca3bee1e2d2043d42f0480012c1e7ec85498f88|eca3bee1e2d2043d42f0480012c1e7ec85498f88|patches/gpsp|.' \
	'gpsp_multicore|$(CORE_SOURCE_ROOT)/gpsp_multicore|https://github.com/tzubertowski/gpsp_multicore.git|head|63dd94953c27bb2664872331bbc7f212a088db4b|63dd94953c27bb2664872331bbc7f212a088db4b|patches/gpsp_multicore|.' \
	'picodrive|$(CORE_SOURCE_ROOT)/picodrive|https://github.com/libretro/picodrive.git|head|f0d4a0118a9733a1f10bce5a4ac772c474f9300d|f0d4a0118a9733a1f10bce5a4ac772c474f9300d|patches/picodrive|cpu/cyclone cpu/cz80 cpu/drc cpu/fame cpu/sh2 pico platform/common platform/libretro unzip zlib' \
	'snes9x2005|$(CORE_SOURCE_ROOT)/snes9x2005|https://github.com/libretro/snes9x2005.git|head|b60356971fc9caae02cd0853676dced886a08be7|b60356971fc9caae02cd0853676dced886a08be7|patches/snes9x2005|.' \
	'snes9x2002|$(CORE_SOURCE_ROOT)/snes9x2002|https://github.com/libretro/snes9x2002.git|head|39e0d8c6daf4b1b1302eeecfee8309570aeb6a82|39e0d8c6daf4b1b1302eeecfee8309570aeb6a82|-|.' \
	'quicknes|$(CORE_SOURCE_ROOT)/QuickNES_Core|https://github.com/libretro/QuickNES_Core.git|head|7848e1ac22b1c69d056ae4cb57710651ff1dd169|7848e1ac22b1c69d056ae4cb57710651ff1dd169|patches/quicknes|.' \
	'fceumm|$(CORE_SOURCE_ROOT)/libretro-fceumm|https://github.com/libretro/libretro-fceumm.git|head|3a84a6fd0ba20dd4877c06b1d58741172148395f|3a84a6fd0ba20dd4877c06b1d58741172148395f|patches/fceumm|src' \
	'gearboy|$(CORE_SOURCE_ROOT)/Gearboy|https://github.com/drhelius/Gearboy.git|tag|3.8.3|36f9faf04bcb6c023176de12dddae99ffc1ceb10|-|:files /LICENSE /src/** /platforms/libretro/Makefile /platforms/libretro/Makefile.common /platforms/libretro/libretro.cpp /platforms/libretro/libretro.h /platforms/libretro/libretro_core_options.h /platforms/libretro/link.T /platforms/shared/dependencies/miniz/**' \
	'pce-fast|$(CORE_SOURCE_ROOT)/beetle-pce-fast-libretro|https://github.com/libretro/beetle-pce-fast-libretro.git|head|9ba79648d6ec85e833aef719d7f359117498d89c|9ba79648d6ec85e833aef719d7f359117498d89c|patches/pce-fast|mednafen libretro-common' \
	'qpsx|$(CORE_SOURCE_ROOT)/sf2000-qpsx-playstation-emulator|git@github.com:axgdev/frog2k-qpsx.git|head|public_main|f1f4d6add9dcfb36eaed7aae198ddbc44b12b9c1|-|src libretro' \
	'frogui|$(CORE_SOURCE_ROOT)/FrogUI|https://github.com/tzubertowski/FrogUI.git|head|2f41acebe50e4529b7c697b04eab2e92d2785bdd|2f41acebe50e4529b7c697b04eab2e92d2785bdd|patches/frogui|:files /LICENSE /frogui_libretro.c /font.c /font.h /render.c /render.h /theme.c /theme.h /settings.h /stb_truetype.h /libretro.h /fonts/**' \
	'mame2000|$(CORE_SOURCE_ROOT)/libretro-mame2000|https://github.com/libretro/mame2000-libretro.git|head|905808fbcc3adf8c610c1c60f0e41ce4b35db1c5|905808fbcc3adf8c610c1c60f0e41ce4b35db1c5|patches/mame2000|.' \
	'fbalpha2012|$(CORE_SOURCE_ROOT)/fbalpha2012|https://github.com/libretro/fbalpha2012.git|head|b7ac554c53561d41640372f23dab15cd6fc4f0c4|b7ac554c53561d41640372f23dab15cd6fc4f0c4|patches/fbalpha2012|.' \
	'stella2014|$(CORE_SOURCE_ROOT)/libretro-stella2014|https://github.com/libretro/stella2014-libretro.git|head|db6bee857f73138ae02755bf09e392b31fef4540|db6bee857f73138ae02755bf09e392b31fef4540|patches/stella2014|.' \
	'a5200|$(CORE_SOURCE_ROOT)/a5200|https://github.com/libretro/a5200.git|head|0942c88d64cad6853b539f51b39060a9de0cbcab|0942c88d64cad6853b539f51b39060a9de0cbcab|patches/a5200|.' \
	'prosystem|$(CORE_SOURCE_ROOT)/libretro-prosystem|https://github.com/libretro/prosystem-libretro.git|head|4202ac5bdb2ce1a21f84efc0e26d75bb5aa7e248|4202ac5bdb2ce1a21f84efc0e26d75bb5aa7e248|patches/prosystem|.' \
	'atari800lib|$(CORE_SOURCE_ROOT)/libretro-atari800lib|https://github.com/nutki/libretro-atari800lib.git|head|c562f734f80bb47511e8321251751b8566bc1f0d|c562f734f80bb47511e8321251751b8566bc1f0d|patches/atari800lib|.' \
	'handy|$(CORE_SOURCE_ROOT)/libretro-handy|https://github.com/libretro/libretro-handy.git|head|65d6b865544cd441ef2bd18cde7bd834c23d0e48|65d6b865544cd441ef2bd18cde7bd834c23d0e48|patches/handy|.' \
	'race|$(CORE_SOURCE_ROOT)/RACE|https://github.com/libretro/RACE.git|head|f65011e6639ccbbbb44b6ffa63ca50c070475df4|f65011e6639ccbbbb44b6ffa63ca50c070475df4|patches/race|.' \
	'beetle-cygne|$(CORE_SOURCE_ROOT)/libretro-beetle-wswan|https://github.com/libretro/beetle-wswan-libretro.git|head|32bf70a3032a138baa969c22445f4b7821632c30|32bf70a3032a138baa969c22445f4b7821632c30|patches/beetle-cygne|.' \
	'gearcoleco|$(CORE_SOURCE_ROOT)/Gearcoleco|https://github.com/drhelius/Gearcoleco.git|head|149d9687624f845de4f7690b145da172f87d115a|149d9687624f845de4f7690b145da172f87d115a|-|.' \
	'snes9x2005-prosty|$(CORE_SOURCE_ROOT)/snes9x2005-prosty|https://github.com/tzubertowski/snes9x2005.git|head|fa25aaf57a043e999f1bc3d9327a71c4cdb1d942|fa25aaf57a043e999f1bc3d9327a71c4cdb1d942|patches/snes9x2005-prosty|.' \
	'snes9x2002-prosty|$(CORE_SOURCE_ROOT)/snes9x2002-prosty|https://github.com/tzubertowski/snes9x2002.git|head|864c7d26b6bc42f7d648d1ba68dfc37520878629|864c7d26b6bc42f7d648d1ba68dfc37520878629|patches/snes9x2002-prosty|.' \
	'gambatte-prosty|$(CORE_SOURCE_ROOT)/libretro-gambatte-prosty|https://github.com/tzubertowski/libretro-gambatte.git|head|9e8bbe6a9a5e2cb35cfe3a851aaa631a4760f2e3|9e8bbe6a9a5e2cb35cfe3a851aaa631a4760f2e3|-|.' \
	'quicknes-prosty|$(CORE_SOURCE_ROOT)/QuickNES_Core-prosty|https://github.com/tzubertowski/QuickNES_Core.git|head|9a6852e768cbabfcaa884f2d69cd8ea8cea37b69|9a6852e768cbabfcaa884f2d69cd8ea8cea37b69|-|.' \
	'frodo-prosty|$(CORE_SOURCE_ROOT)/libretro-frodo-prosty|https://github.com/tzubertowski/libretro-frodo.git|head|e2de1193e420f00c3eb65a1182bb31aa58fdfebb|e2de1193e420f00c3eb65a1182bb31aa58fdfebb|patches/frodo-prosty|.' \
	'fake08-prosty|$(CORE_SOURCE_ROOT)/fake-08-prosty|https://github.com/tzubertowski/fake-08.git|head|b87983eaf7492fdd945f2897024e0bb725e1e15d|b87983eaf7492fdd945f2897024e0bb725e1e15d|patches/fake08-prosty|.' \
	'bluemsx-prosty|$(CORE_SOURCE_ROOT)/libretro-blueMSX-prosty|https://github.com/tzubertowski/libretro-blueMSX.git|head|0b47ea3e7370bab5766eaa7c470d21247da3764a|0b47ea3e7370bab5766eaa7c470d21247da3764a|patches/bluemsx-prosty|.' \
	'fceumm-prosty|$(CORE_SOURCE_ROOT)/libretro-fceumm-prosty|https://github.com/tzubertowski/libretro-fceumm.git|head|e6111e684e7a7761f3f1d6c80d0a825e2c8cdc7e|e6111e684e7a7761f3f1d6c80d0a825e2c8cdc7e|-|.'

# Patch queues for source repositories pinned as submodules by a core fork.
# parent-core|submodule-path|pinned-commit|patch-dir
NESTED_CORE_SPECS := \
	'fake08-prosty|libs/z8lua|f0eb8df934c5ad588b12ce40bf1ebf1c2d63edf6|patches/fake08-prosty-z8lua'

# Package-facing libretro module specs:
# package-id:make-var:module-file-stem:core-target:archive-stem:extensions:symbol-prefix:support-libs
CORE_PACKAGE_SPECS := \
	gambatte:GAMBATTE:gambatte:gambatte:gambatte:gb\|gbc:-:- \
	gpsp:GPSP:gpsp:gpsp:gpsp:gba:gpsp:- \
	gpsp-gbac-prosty:GPSP_MULTICORE:gpsp-gbac-prosty:gpsp_multicore:gpsp_multicore:gba:gpsp_multicore:- \
	picodrive:PICODRIVE:picodrive:picodrive:picodrive:md\|gen\|smd\|sms\|gg\|sg\|32x\|cue\|chd\|iso:picodrive:$(CHD_SUPPORT_CORE_LIB) \
	snes9x2005:SNES9X2005:snes9x2005:snes9x2005:snes9x2005:sfc\|smc:snes9x2005:- \
	snes9x2002:SNES9X2002:snes9x2002:snes9x2002:snes9x2002:sfc\|smc:snes9x2002:- \
	quicknes:QUICKNES:quicknes:quicknes:quicknes:nes:quicknes:- \
	fceumm:FCEUMM:fceumm:fceumm:fceumm:nes\|fds:fceumm:- \
	gearboy:GEARBOY:gearboy:gearboy:gearboy:gb\|gbc:gearboy:- \
	pce-fast:PCE_FAST:pce-fast:pce-fast:pce_fast:pce\|sgx\|cue\|chd:pce_fast:$(CHD_SUPPORT_CORE_LIB) \
	qpsx:QPSX:qpsx:qpsx:pcsx4all:bin\|iso\|img\|cue\|pbp:qpsx:- \
	frogui:FROGUI:frogui:frogui:frogui:frogui:frogui:- \
	mame2000:MAME2000:mame2000:mame2000:mame2000:zip:mame2000:- \
	fbalpha2012:FBALPHA2012:fbalpha2012:fbalpha2012:fbalpha2012:zip:fbalpha2012:- \
	stella2014:STELLA2014:stella2014:stella2014:stella2014:a26\|bin:stella2014:- \
	a5200:A5200:a5200:a5200:a5200:a52\|bin:a5200:- \
	prosystem:PROSYSTEM:prosystem:prosystem:prosystem:a78\|bin:prosystem:- \
	atari800lib:ATARI800LIB:atari800lib:atari800lib:atari800lib:xex\|atr\|atx\|cdm\|cas\|a52:atari800lib:- \
	handy:HANDY:handy:handy:handy:lnx:handy:- \
	race:RACE:race:race:race:ngp\|ngc:race:- \
	beetle-cygne:BEETLE_CYGNE:beetle-cygne:beetle-cygne:beetle_cygne:ws\|wsc:beetle_cygne:- \
	gearcoleco:GEARCOLECO:gearcoleco:gearcoleco:gearcoleco:col\|rom:gearcoleco:- \
	snes9x2005-prosty:SNES9X2005_PROSTY:snes9x2005-prosty:snes9x2005-prosty:snes9x2005_prosty:sfc\|smc:snes9x2005_prosty:- \
	snes9x2002-prosty:SNES9X2002_PROSTY:snes9x2002-prosty:snes9x2002-prosty:snes9x2002_prosty:sfc\|smc:snes9x2002_prosty:- \
	gambatte-prosty:GAMBATTE_PROSTY:gambatte-prosty:gambatte-prosty:gambatte_prosty:gb\|gbc:gambatte_prosty:- \
	quicknes-prosty:QUICKNES_PROSTY:quicknes-prosty:quicknes-prosty:quicknes_prosty:nes:quicknes_prosty:- \
	frodo-prosty:FRODO_PROSTY:frodo-prosty:frodo-prosty:frodo_prosty:d64\|t64\|x64\|p00\|prg:frodo_prosty:- \
	fake08-prosty:FAKE08_PROSTY:fake08-prosty:fake08-prosty:fake08_prosty:p8:fake08_prosty:- \
	bluemsx-prosty:BLUEMSX_PROSTY:bluemsx-prosty:bluemsx-prosty:bluemsx_prosty:rom\|mx1\|mx2\|dsk\|cas:bluemsx_prosty:- \
	fceumm-prosty:FCEUMM_PROSTY:fceumm-prosty:fceumm-prosty:fceumm_prosty:nes\|fds:fceumm_prosty:-

# License material copied verbatim beside packaged core modules. Keep the
# destination names stable so SD artifacts remain auditable without fetching
# the source checkouts.
# package-id|source-file|artifact-name
CORE_LICENSE_SPECS := \
	gambatte|$(CORE_SOURCE_ROOT)/gambatte-libretro/COPYING|gambatte-COPYING \
	gpsp|$(CORE_SOURCE_ROOT)/gpsp-libretro/COPYING|gpsp-COPYING \
	gpsp-gbac-prosty|$(CORE_SOURCE_ROOT)/gpsp_multicore/COPYING|gpsp-gbac-prosty-COPYING \
	picodrive|$(CORE_SOURCE_ROOT)/picodrive/COPYING|picodrive-COPYING \
	snes9x2005|$(CORE_SOURCE_ROOT)/snes9x2005/copyright|snes9x2005-copyright \
	snes9x2002|$(CORE_SOURCE_ROOT)/snes9x2002/src/copyright.h|snes9x2002-copyright.h \
	quicknes|$(CORE_SOURCE_ROOT)/QuickNES_Core/LICENSE|quicknes-LICENSE \
	fceumm|$(CORE_SOURCE_ROOT)/libretro-fceumm/Copying|fceumm-COPYING \
	gearboy|$(CORE_SOURCE_ROOT)/Gearboy/LICENSE|gearboy-LICENSE \
	pce-fast|$(CORE_SOURCE_ROOT)/beetle-pce-fast-libretro/COPYING|pce-fast-COPYING \
	qpsx|$(CORE_SOURCE_ROOT)/sf2000-qpsx-playstation-emulator/LICENSE|qpsx-LICENSE \
	frogui|$(CORE_SOURCE_ROOT)/FrogUI/LICENSE|frogui-LICENSE \
	mame2000|$(CORE_SOURCE_ROOT)/libretro-mame2000/readme.txt|mame2000-license.txt \
	fbalpha2012|$(CORE_SOURCE_ROOT)/fbalpha2012/svn-current/trunk/src/license.txt|fbalpha2012-license.txt \
	stella2014|$(CORE_SOURCE_ROOT)/libretro-stella2014/stella/license.txt|stella2014-LICENSE \
	a5200|$(CORE_SOURCE_ROOT)/a5200/License.txt|a5200-LICENSE \
	prosystem|$(CORE_SOURCE_ROOT)/libretro-prosystem/License.txt|prosystem-LICENSE \
	atari800lib|$(CORE_SOURCE_ROOT)/libretro-atari800lib/atari800/COPYING|atari800lib-COPYING \
	handy|$(CORE_SOURCE_ROOT)/libretro-handy/lynx/license.txt|handy-license.txt \
	race|$(CORE_SOURCE_ROOT)/RACE/license.txt|race-LICENSE \
	beetle-cygne|$(CORE_SOURCE_ROOT)/libretro-beetle-wswan/COPYING|beetle-cygne-COPYING \
	gearcoleco|$(CORE_SOURCE_ROOT)/Gearcoleco/LICENSE|gearcoleco-LICENSE \
	snes9x2005-prosty|$(CORE_SOURCE_ROOT)/snes9x2005-prosty/copyright|snes9x2005-prosty-copyright \
	snes9x2002-prosty|$(CORE_SOURCE_ROOT)/snes9x2002-prosty/src/copyright.h|snes9x2002-prosty-copyright.h \
	gambatte-prosty|$(CORE_SOURCE_ROOT)/libretro-gambatte-prosty/COPYING|gambatte-prosty-COPYING \
	quicknes-prosty|$(CORE_SOURCE_ROOT)/QuickNES_Core-prosty/LICENSE|quicknes-prosty-LICENSE \
	frodo-prosty|$(CORE_SOURCE_ROOT)/libretro-frodo-prosty/COPYING|frodo-prosty-COPYING \
	fake08-prosty|$(CORE_SOURCE_ROOT)/fake-08-prosty/LICENSE.MD|fake08-prosty-LICENSE.md \
	bluemsx-prosty|$(CORE_SOURCE_ROOT)/libretro-blueMSX-prosty/license.txt|bluemsx-prosty-license.txt \
	fceumm-prosty|$(CORE_SOURCE_ROOT)/libretro-fceumm-prosty/Copying|fceumm-prosty-COPYING

DEFAULT_CORE_IDS := gambatte gpsp gpsp-gbac-prosty picodrive snes9x2005 \
	snes9x2002 quicknes fceumm gearboy pce-fast qpsx frogui \
	mame2000 fbalpha2012 stella2014 a5200 prosystem atari800lib handy race \
	beetle-cygne gearcoleco snes9x2005-prosty snes9x2002-prosty \
	gambatte-prosty quicknes-prosty frodo-prosty fake08-prosty \
	bluemsx-prosty fceumm-prosty

# Clean rebuilds of these cores cost far more than the rest combined
# (benchmarked with a warm ccache, -j9: fbalpha2012 and mame2000 run for
# many minutes; gpsp 15s, gpsp-gbac-prosty 17s, picodrive 14s, qpsx 12s;
# everything else lands under 5s). Quick iterations skip them; release
# packaging (make sd-zip) and explicit CORE_IDS=... or `make -C cores
# <target>` still build them.
SLOW_CORE_IDS := fbalpha2012 mame2000 gpsp gpsp-gbac-prosty picodrive qpsx
FAST_CORE_IDS := $(filter-out $(SLOW_CORE_IDS),$(DEFAULT_CORE_IDS))

SMOKE_SUPPORT_SPECS := \
	'zlib|$(CORE_SUPPORT_ROOT)/zlib|https://github.com/madler/zlib.git|tag|v1.3.2|da607da739fa6047df13e66a2af6b8bec7c2a498|-|:root-files'

SMOKE_CORE_SPECS := \
	'libretro-common|$(CORE_SOURCE_ROOT)/libretro-common|https://github.com/libretro/libretro-common.git|head|e2e3eccfd245a04771e6a435320b42234c8cc4d7|e2e3eccfd245a04771e6a435320b42234c8cc4d7|patches/libretro-common|compat encodings file formats streams string time vfs include' \
	'quicknes|$(CORE_SOURCE_ROOT)/QuickNES_Core|https://github.com/libretro/QuickNES_Core.git|head|7848e1ac22b1c69d056ae4cb57710651ff1dd169|7848e1ac22b1c69d056ae4cb57710651ff1dd169|patches/quicknes|.'
