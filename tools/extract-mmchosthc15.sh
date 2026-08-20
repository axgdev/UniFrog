#!/bin/sh
set -eu

sdk=${SDK:-unifrog-hcrtos-sdk}
cross=${CROSS_COMPILE:-}
[ -n "$cross" ] || {
	echo "CROSS_COMPILE is required; use the frog-toolchain prefix" >&2
	exit 2
}
out_dir=${OUT_DIR:-build/vendor/mmchosthc15}
archive=${ARCHIVE:-$sdk/lib/vendor/libmmchosthc15.a}
filter=${FILTER:-hc_mmc}

ar_tool=${AR_TOOL:-${cross}ar}
nm_tool=${NM_TOOL:-${cross}nm}
objdump_tool=${OBJDUMP_TOOL:-${cross}objdump}
strings_tool=${STRINGS_TOOL:-${cross}strings}

if [ ! -f "$archive" ]; then
	echo "missing archive: $archive" >&2
	exit 1
fi
archive_abs=$(realpath "$archive")

mkdir -p "$out_dir"
rm -rf "$out_dir/members"
mkdir -p "$out_dir/members"

printf '%s\n' "archive=$archive" > "$out_dir/manifest.txt"
printf '%s\n' "filter=$filter" >> "$out_dir/manifest.txt"
printf '%s\n' "ar=$ar_tool" >> "$out_dir/manifest.txt"
printf '%s\n' "nm=$nm_tool" >> "$out_dir/manifest.txt"
printf '%s\n' "objdump=$objdump_tool" >> "$out_dir/manifest.txt"

"$ar_tool" t "$archive" > "$out_dir/members.txt"
(
	cd "$out_dir/members"
	"$ar_tool" x "$archive_abs"
)

"$nm_tool" -a "$archive" > "$out_dir/nm-all.txt"
"$nm_tool" -g --defined-only "$archive" > "$out_dir/nm-defined.txt"
"$objdump_tool" -dr "$archive" > "$out_dir/objdump-dr.txt"
"$objdump_tool" -s "$archive" > "$out_dir/objdump-s.txt"
"$strings_tool" -a "$archive" > "$out_dir/strings.txt"

if [ -n "$filter" ]; then
	rg -n "$filter" "$out_dir/nm-all.txt" "$out_dir/nm-defined.txt" \
		"$out_dir/objdump-dr.txt" "$out_dir/strings.txt" \
		> "$out_dir/filter-index.txt" || :
else
	: > "$out_dir/filter-index.txt"
fi

echo "$out_dir"
