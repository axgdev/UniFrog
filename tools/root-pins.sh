#!/bin/sh
set -eu

cmd=${1:-}
mode=${PIN_MODE:-policy}

case "$mode" in policy|head|tag) ;; *) echo "MODE must be policy, head, or tag" >&2; exit 1 ;; esac

resolve_ref() {
	url=$1
	resolve_mode=$2
	if [ "$resolve_mode" = tag ]; then
		tag=$(git ls-remote --tags --sort='version:refname' "$url" 'refs/tags/v[0-9]*' 'refs/tags/[0-9]*' 2>/dev/null |
			awk '$2 !~ /\^\{\}$/ && $2 ~ /^refs\/tags\/v?[0-9]+([.][0-9]+)*$/ { sub("refs/tags/", "", $2); tag=$2 } END { print tag }')
		if [ -n "$tag" ]; then
			ref=$(git ls-remote --tags "$url" "refs/tags/$tag" "refs/tags/$tag^{}" |
				awk '$2 ~ /\^\{\}$/ { peeled=$1 } $2 !~ /\^\{\}$/ { direct=$1 } END { print peeled ? peeled : direct }')
			printf '%s tag %s\n' "$ref" "$tag"
			return
		fi
	fi
	branch=$(git ls-remote --symref "$url" HEAD | awk '/^ref:/ { sub("refs\/heads/", "", $2); print $2; exit }')
	ref=$(git ls-remote "$url" HEAD | awk '/^[0-9a-f]/ { print $1; exit }')
	printf '%s head %s\n' "$ref" "${branch:-HEAD}"
}

set -- $(resolve_ref "$LVGL_URL" "$mode")
new=$1
kind=$2
label=$3
old=$LVGL_REF

case "$cmd" in
status)
	printf '%-16s pinned=%s url=%s latest=%s source=%s:%s\n' \
		lvgl "$old" "$LVGL_URL" "$new" "$kind" "$label"
	;;
upgrade)
	if [ -z "$new" ]; then
		echo "lvgl: unable to resolve latest $mode" >&2
		exit 1
	fi
	if [ "$new" != "$old" ]; then
		sed -i.bak "s|^LVGL_REF ?= .*|LVGL_REF ?= $new|" config/options.mk
		rm -f config/options.mk.bak
		echo "  PIN     lvgl $old -> $new ($kind $label)"
	else
		echo "  PIN     lvgl already $old ($kind $label)"
	fi
	;;
*)
	echo "usage: tools/root-pins.sh status|upgrade" >&2
	exit 2
	;;
esac
