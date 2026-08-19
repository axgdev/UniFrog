#!/bin/sh
set -eu

map=${1:-output/sf2000.out.map}
test -s "$map" || {
	echo "missing linker map: $map" >&2
	exit 1
}

awk '
function hex(value, result, digit, i, ch) {
	result = 0
	sub(/^0x/, "", value)
	for (i = 1; i <= length(value); i++) {
		ch = substr(value, i, 1)
		digit = index("0123456789abcdef", tolower(ch)) - 1
		if (digit < 0) return 0
		result = result * 16 + digit
	}
	return result
}
function owner(path) {
	if (path ~ /libfrontend\.a/) return "frontend"
	if (path ~ /liblibretro\.a/) return "libretro"
	if (path ~ /libmedia\.a/) return "media"
	if (path ~ /libdiagnostics\.a/) return "diagnostics"
	if (path ~ /libfoundation\.a/) return "foundation"
	if (path ~ /libjs2300\.a/) return "js2300"
	if (path ~ /libretro-common/) return "libretro-common"
	if (path ~ /build\/runtime\/main\.o/) return "app"
	return ""
}
function kind(section) {
	if (section ~ /^\.text/) return "text"
	if (section ~ /^\.rodata/ || section ~ /^\.srodata/) return "rodata"
	if (section ~ /^\.data/ || section ~ /^\.sdata/) return "data"
	if (section ~ /^\.bss/ || section ~ /^\.sbss/) return "bss"
	return ""
}
{
	k = kind($1)
	o = owner($NF)
	if (k != "" && o != "" && $2 ~ /^0x/ && $3 ~ /^0x/) {
		size = hex($3)
		if (hex($2) != 0 && size != 0) {
			bytes[o, k] += size
			last_owner = o
			last_kind = k
			last_size = size
		}
		pending = ""
	} else if (k != "" && $1 ~ /^\./ && $2 !~ /^0x/) {
		pending = k
	} else if (pending != "" && $1 ~ /^0x/ && $2 ~ /^0x/ && o != "") {
		size = hex($2)
		if (hex($1) != 0 && size != 0) {
			bytes[o, pending] += size
			last_owner = o
			last_kind = pending
			last_size = size
		} else {
			last_owner = o
			last_kind = pending
			last_size = 0
		}
		pending = ""
	} else if ($1 ~ /^0x/ && $2 == "(size" && last_owner != "") {
		actual = hex($1)
		bytes[last_owner, last_kind] += actual - last_size
		last_owner = ""
		last_kind = ""
		last_size = 0
	} else if ($1 ~ /^\./) {
		pending = ""
		last_owner = ""
	}
}
END {
	printf "%-18s %10s %10s %10s %10s %10s\n",
		"component", "text", "rodata", "data", "bss", "total"
	order[1] = "app"
	order[2] = "frontend"
	order[3] = "libretro"
	order[4] = "media"
	order[5] = "diagnostics"
	order[6] = "foundation"
	order[7] = "js2300"
	order[8] = "libretro-common"
	for (i = 1; i <= 8; i++) {
		o = order[i]
		total = bytes[o, "text"] + bytes[o, "rodata"] + bytes[o, "data"] + bytes[o, "bss"]
		printf "%-18s %10u %10u %10u %10u %10u\n", o,
			bytes[o, "text"], bytes[o, "rodata"], bytes[o, "data"],
			bytes[o, "bss"], total
	}
}
' "$map"
