#!/bin/sh
set -eu

: "${TOOLCHAIN:?}"
: "${TOOLCHAIN_URL:?}"
: "${TOOLCHAIN_SHA256:?}"
: "${TOOLCHAIN_ARCHIVE:?}"

if [ ! -x "$TOOLCHAIN/bin/mipsel-mti-elf-gcc" ]; then
	command -v curl >/dev/null || { echo "missing: curl" >&2; exit 1; }
	command -v sha256sum >/dev/null || { echo "missing: sha256sum" >&2; exit 1; }
	command -v tar >/dev/null || { echo "missing: tar" >&2; exit 1; }
	archive_dir=$(dirname "$TOOLCHAIN_ARCHIVE")
	mkdir -p "$archive_dir"
	if [ -e "$TOOLCHAIN_ARCHIVE" ]; then
		echo "  VERIFY  $TOOLCHAIN_ARCHIVE"
	else
		echo "  FETCH   $TOOLCHAIN_URL"
		curl -fL --retry 3 --retry-delay 2 "$TOOLCHAIN_URL" -o "$TOOLCHAIN_ARCHIVE"
	fi
	actual=$(sha256sum "$TOOLCHAIN_ARCHIVE" | awk '{print $1}')
	[ "$actual" = "$TOOLCHAIN_SHA256" ] || {
		echo "toolchain checksum mismatch: $actual" >&2
		exit 1
	}
	stage="$TOOLCHAIN.tmp.$$"
	cleanup() {
		rm -rf "$stage"
	}
	trap cleanup EXIT HUP INT TERM
	[ ! -e "$TOOLCHAIN" ] || {
		echo "toolchain path exists but is incomplete: $TOOLCHAIN" >&2
		exit 1
	}
	mkdir -p "$stage"
	tar -C "$stage" -xf "$TOOLCHAIN_ARCHIVE"
	found=$(find "$stage" -path '*/bin/mipsel-mti-elf-gcc' -type f -print -quit)
	[ -n "$found" ] || {
		echo "toolchain archive has no mipsel-mti-elf-gcc" >&2
		exit 1
	}
	root=$(dirname "$(dirname "$found")")
	mkdir -p "$(dirname "$TOOLCHAIN")"
	mv "$root" "$TOOLCHAIN"
	trap - EXIT HUP INT TERM
	rm -rf "$stage"
fi

test -x "$TOOLCHAIN/bin/mipsel-mti-elf-gcc" || {
	echo "missing: $TOOLCHAIN/bin/mipsel-mti-elf-gcc" >&2
	exit 1
}
