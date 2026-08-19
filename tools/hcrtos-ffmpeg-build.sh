#!/bin/sh
set -eu

required='
HCRTOS_FFMPEG_SOURCE
HCRTOS_FFMPEG_SOURCE_ABS
HCRTOS_FFMPEG_BUILD
HCRTOS_FFMPEG_INSTALL
HCRTOS_FFMPEG_INSTALL_ABS
HCRTOS_FFMPEG_STAMP
CROSS_COMPILE
AR
NM
SDK_ABS
ARCH_CFLAGS
OPT_SIZE
HCRTOS_FFMPEG_CONFIGURE_LOG
HCRTOS_FFMPEG_BUILD_LOG
HCRTOS_FFMPEG_INSTALL_LOG
'

for name in $required; do
	eval "value=\${$name:-}"
	if [ -z "$value" ]; then
		echo "missing environment: $name" >&2
		exit 2
	fi
done

[ -f "$HCRTOS_FFMPEG_SOURCE/configure" ] || {
	echo "missing HCRTOS FFmpeg source: $HCRTOS_FFMPEG_SOURCE" >&2
	exit 1
}

progress() {
	if [ "${BUILD_PROGRESS:-0}" = 1 ]; then
		printf '  FFMPEG  %s\n' "$1"
	fi
}

tail_log() {
	log=$1
	if [ -f "$log" ]; then
		tail -n 80 "$log"
	fi
}

run_logged() {
	label=$1
	log=$2
	shift 2
	if [ "${V:-}" = 1 ]; then
		"$@"
	else
		"$@" >"$log" 2>&1 || {
			echo "FFmpeg $label failed; log: $log" >&2
			tail_log "$log" >&2
			exit 1
		}
	fi
}

remove_tree() {
	rm -rf "$@" || {
		sleep 1
		rm -rf "$@"
	}
}

compat_dir=$HCRTOS_FFMPEG_BUILD/unifrog-compat
remove_tree "$HCRTOS_FFMPEG_BUILD" "$HCRTOS_FFMPEG_INSTALL"
mkdir -p "$HCRTOS_FFMPEG_BUILD" "$HCRTOS_FFMPEG_INSTALL" "$(dirname "$HCRTOS_FFMPEG_CONFIGURE_LOG")"
mkdir -p "$compat_dir/sys" "$compat_dir/hcuapi"

cat >"$compat_dir/pkg-config" <<'EOF'
#!/bin/sh
case "$1" in --version) echo 0.0; exit 0 ;; esac
exit 1
EOF
chmod +x "$compat_dir/pkg-config"

cat >"$compat_dir/assert.h" <<'EOF'
#pragma once
#include_next <assert.h>
EOF

cat >"$compat_dir/sys/ioctl.h" <<'EOF'
#pragma once
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
int ioctl(int fd, unsigned long request, ...);
int close(int fd);
#define TIOCGWINSZ 0
EOF

cat >"$compat_dir/hcuapi/dsc.h" <<'EOF'
#pragma once
struct dsc_buffer { void *buffer; int size; };
struct dsc_algo_params { int algo_type; int crypto_mode; int chaining_mode; int residue_mode; struct dsc_buffer key; struct dsc_buffer iv; };
struct dsc_crypt { const void *input; void *output; int size; };
#define DSC_ALGO_AES 1
#define DSC_DECRYPT 0
#define DSC_MODE_CTR 1
#define DSC_RESIDUE_AS_ATSC 0
#define DSC_DO_DECRYPT 0x1001
#define DSC_CONFIG 0x1002
EOF

set -- \
	--prefix="$HCRTOS_FFMPEG_INSTALL_ABS" \
	--enable-cross-compile \
	--cross-prefix="$CROSS_COMPILE" \
	--arch=mips \
	--target-os=none \
	--cc="${CROSS_COMPILE}gcc" \
	--ar="$AR" \
	--nm="$NM" \
	--pkg-config="$PWD/$compat_dir/pkg-config" \
	--disable-stripping \
	--disable-programs \
	--disable-doc \
	--disable-debug \
	--disable-network \
	--disable-pthreads \
	--disable-iconv \
	--enable-zlib \
	--disable-bzlib \
	--disable-lzma \
	--disable-securetransport \
	--disable-xlib \
	--disable-sdl2 \
	--disable-avdevice \
	--disable-avfilter \
	--enable-swresample \
	--disable-hwaccels \
	--disable-asm \
	--disable-inline-asm \
	--disable-faan \
	--disable-mipsfpu \
	--enable-small \
	--enable-static \
	--disable-shared \
	--enable-avcodec \
	--enable-avformat \
	--enable-avutil \
	--enable-swscale \
	--disable-encoders \
	--disable-muxers \
	--disable-demuxers

for item in ${HCRTOS_FFMPEG_DEMUXERS:-}; do
	set -- "$@" "--enable-demuxer=$item"
done

set -- "$@" --disable-parsers
for item in ${HCRTOS_FFMPEG_PARSERS:-}; do
	set -- "$@" "--enable-parser=$item"
done

set -- "$@" --disable-decoders
for item in ${HCRTOS_FFMPEG_DECODERS:-}; do
	set -- "$@" "--enable-decoder=$item"
done

set -- "$@" \
	--disable-bsfs \
	--enable-bsf=h264_mp4toannexb \
	--disable-protocols \
	--enable-protocol=file \
	--extra-cflags="-EL $ARCH_CFLAGS $OPT_SIZE -msoft-float -fsigned-char -ffunction-sections -fdata-sections -G0 ${HCRTOS_FFMPEG_WARN_CFLAGS:-} ${HCRTOS_FFMPEG_ABI_CFLAGS:-} -D_FORTIFY_SOURCE=0 -D__HCRTOS__ -DSOC_HC15XX -DSF2000 -I$PWD/$compat_dir -I$SDK_ABS/include/newlib -I$SDK_ABS/include/kernel/lib -I$SDK_ABS/include" \
	--extra-ldflags="-EL -L$SDK_ABS/lib/core" \
	--extra-libs="-lz -lc -lm -lgcc"

progress configure
run_logged configure "$HCRTOS_FFMPEG_CONFIGURE_LOG" sh -c \
	'build=$1; source=$2; shift 2; cd "$build" && "$source/configure" "$@"' \
	sh "$HCRTOS_FFMPEG_BUILD" "$HCRTOS_FFMPEG_SOURCE_ABS" "$@"

sed -i \
	-e '/^#define getenv(x) NULL/d' \
	-e 's/^#define HAVE_HYPOT 0/#define HAVE_HYPOT 1/' \
	-e 's/^#define HAVE_MEMALIGN 0/#define HAVE_MEMALIGN 1/' \
	"$HCRTOS_FFMPEG_BUILD/config.h"
sed -i \
	-e 's|^CFLAGS=\(.*\)|CFLAGS=\1 -Wno-declaration-after-statement -Wno-redundant-decls|' \
	"$HCRTOS_FFMPEG_BUILD/ffbuild/config.mak"

make_cmd=${MAKE:-make}
progress build
run_logged build "$HCRTOS_FFMPEG_BUILD_LOG" "$make_cmd" -C "$HCRTOS_FFMPEG_BUILD"

progress install
run_logged install "$HCRTOS_FFMPEG_INSTALL_LOG" "$make_cmd" -C "$HCRTOS_FFMPEG_BUILD" ${HCRTOS_FFMPEG_INSTALL_TARGETS:-install-libs install-headers}

touch "$HCRTOS_FFMPEG_STAMP"
