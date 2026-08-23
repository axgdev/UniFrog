#!/bin/sh
set -eu

: "${BUILD:?}"
: "${LANGUAGE_FILES:?}"

tmp="$BUILD/language-keys"
rm -rf "$tmp"
mkdir -p "$tmp"

set -- $LANGUAGE_FILES
test "$#" -gt 0 || {
	echo "no language files configured" >&2
	exit 1
}

reference="$tmp/reference"
awk -F= '/^[^#;][^=]*=/{print $1}' "$1" | sort -u >"$reference"
reference_count=$(wc -l <"$reference")
test "$reference_count" -le 256 || {
	echo "language key count exceeds frontend limit: $reference_count" >&2
	exit 1
}

for language do
	keys="$tmp/$(basename "$language")"
	awk -F= '/^[^#;][^=]*=/{print $1}' "$language" | sort -u >"$keys"
	if ! cmp -s "$reference" "$keys"; then
		echo "language keys differ: $language" >&2
		diff -u "$reference" "$keys" >&2
		exit 1
	fi
done

rm -rf "$tmp"
echo "OK language catalogs ($reference_count keys)"
