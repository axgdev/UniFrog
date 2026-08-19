#!/bin/sh
set -eu

: "${NM:?}"
: "${READELF:?}"

for image do
	ebss_hex=$("$NM" -n "$image" | awk '$3 == "_ebss" { print $1; exit }')
	if [ -z "$ebss_hex" ]; then
		echo "$image: missing _ebss symbol" >&2
		exit 1
	fi
	"$READELF" -SW "$image" | awk -v image="$image" -v ebss_hex="$ebss_hex" '
	function h2d(s, i, c, n, v) {
		sub(/^0x/, "", s)
		n = 0
		for (i = 1; i <= length(s); i++) {
			c = tolower(substr(s, i, 1))
			v = index("0123456789abcdef", c) - 1
			if (v < 0) { return -1 }
			n = n * 16 + v
		}
		return n
	}
	BEGIN { ebss = h2d(ebss_hex); bad = 0 }
	$2 ~ /^\./ && $3 == "NOBITS" && index($8, "A") {
		start = h2d($4)
		size = h2d($6)
		end = start + size
		printf("  LAYOUT  %-18s 0x%08x..0x%08x flags=%s\n", $2, start, end, $8)
		if (end > ebss) {
			printf("%s: %s ends at 0x%08x after _ebss=0x%08x\n", image, $2, end, ebss) > "/dev/stderr"
			bad = 1
		}
	}
	END {
		if (bad) { exit 1 }
		printf("  OK      %s _ebss=0x%08x covers alloc NOBITS reservations\n", image, ebss)
	}'
done
