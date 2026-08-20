#!/bin/sh
set -eu

cmd=${1:-}
dep=${DEP:-}
ref=${REF:-}

case "$cmd" in
dep-status) cmd=status ;;
dep-edit) cmd=edit ;;
dep-update) cmd=update ;;
dep-refresh) cmd=refresh ;;
dep-finalize) cmd=finalize ;;
dep-log) cmd=log ;;
dep-diff) cmd=diff ;;
dep-patches-check) cmd=patches-check ;;
esac

normalize_dep() {
	case "$1" in
	hcrtos-ffmpeg) printf '%s\n' ffmpeg ;;
	*) printf '%s\n' "$1" ;;
	esac
}

progress() {
	if [ "${BUILD_PROGRESS:-0}" = 1 ]; then
		printf '  %-7s %s\n' "$1" "$2"
	fi
}

is_known_root_dep() {
	case "$(normalize_dep "$1")" in
	lvgl|ffmpeg) return 0 ;;
	*) return 1 ;;
	esac
}

ensure_clean() {
	dir=$1
	git -C "$dir" diff --quiet --ignore-submodules -- || {
		echo "dirty worktree: $dir" >&2
		exit 1
	}
	git -C "$dir" diff --cached --quiet --ignore-submodules -- || {
		echo "staged changes in: $dir" >&2
		exit 1
	}
}

ensure_repo() {
	dir=$1
	url=$2
	mkdir -p "$(dirname "$dir")"
	if [ -d "$dir/.git" ]; then
		progress FETCH "$dir"
		git -C "$dir" remote set-url origin "$url"
		fresh=0
	else
		progress CLONE "$url"
		rm -rf "$dir"
		git init -q "$dir"
		git -C "$dir" remote add origin "$url"
		fresh=1
	fi
}

fetch_ref() {
	dir=$1
	target_ref=$2
	target_commit=${3:-}
	depth_arg=
	filter_arg=--filter=blob:none
	if [ "${DEP_DEPTH:-1}" != 0 ]; then
		depth_arg="--depth ${DEP_DEPTH:-1}"
	fi
	if [ -n "$target_commit" ] && git -C "$dir" cat-file -e "$target_commit^{commit}" 2>/dev/null; then
		return
	fi
	if printf '%s\n' "$target_ref" | grep -Eq '^[0-9a-fA-F]{40}$'; then
		git -C "$dir" fetch $depth_arg $filter_arg origin "$target_ref"
		return
	fi
	case "$target_ref" in
	v[0-9]*|[0-9]*|n[0-9]*)
		git -C "$dir" fetch $depth_arg $filter_arg origin "refs/tags/$target_ref:refs/tags/$target_ref" ||
			git -C "$dir" fetch $depth_arg $filter_arg origin "$target_ref"
		;;
	*)
		git -C "$dir" fetch $depth_arg $filter_arg origin "${target_commit:-$target_ref}"
		;;
	esac
}

apply_raw_patch_queue() {
	dir=$1
	patch=$2
	if [ -d "$patch" ]; then
		for patch_file in "$patch"/*.patch; do
			[ -e "$patch_file" ] || continue
			git -C "$dir" apply --quiet --3way --index "$(pwd)/$patch_file"
		done
	elif [ -s "$patch" ]; then
		git -C "$dir" apply --quiet --3way --index "$(pwd)/$patch"
	fi
	if ! git -C "$dir" diff --cached --quiet; then
		git -C "$dir" commit -q -m "Apply UniFrog $(basename "$dir") patch queue"
	fi
}

checkout_base() {
	name=$1
	dir=$2
	url=$3
	current_commit=$4
	default_ref=$5
	patch=$6
	patch_mode=$7

	target_ref=${ref:-$default_ref}
	target_commit=$current_commit
	if [ -n "$ref" ]; then
		target_commit=
	fi

	ensure_repo "$dir" "$url"
	fetch_ref "$dir" "$target_ref" "$target_commit"
	if [ -z "$target_commit" ]; then
		target_commit=$(git -C "$dir" rev-parse "$target_ref^{commit}" 2>/dev/null ||
			git -C "$dir" rev-parse FETCH_HEAD^{commit})
	fi

	git -C "$dir" am --abort >/dev/null 2>&1 || true
	if [ "$fresh" -eq 0 ]; then
		git -C "$dir" reset --hard -q
		git -C "$dir" clean -fdx -q
	fi
	git -C "$dir" checkout -q -B unifrog-base "$target_commit"
	git -C "$dir" reset --hard -q "$target_commit"

	branch=unifrog-current
	if [ "$patch_mode" = none ]; then
		branch=unifrog-base
	fi
	if [ -n "$ref" ]; then
		branch=unifrog-update
	fi
	if [ "$branch" != unifrog-base ]; then
		git -C "$dir" checkout -q -B "$branch" unifrog-base
	fi

	if [ "$patch_mode" = diff ]; then
		git -C "$dir" config user.name "UniFrog CI"
		git -C "$dir" config user.email "ci@unifrog.local"
		apply_raw_patch_queue "$dir" "$patch"
	fi
}

setup_dep() {
	case "$(normalize_dep "$1")" in
	lvgl)
		checkout_base lvgl "$LVGL_DIR" "$LVGL_URL" "$LVGL_REF" "$LVGL_REF" - none
		;;
	ffmpeg)
		checkout_base ffmpeg "$HCRTOS_FFMPEG_SOURCE" "$HCRTOS_FFMPEG_URL" "$HCRTOS_FFMPEG_COMMIT" "$HCRTOS_FFMPEG_REF" "$HCRTOS_FFMPEG_PATCH" diff
		if [ -n "${HCRTOS_FFMPEG_SOURCE_STAMP:-}" ]; then
			mkdir -p "$(dirname "$HCRTOS_FFMPEG_SOURCE_STAMP")"
			touch "$HCRTOS_FFMPEG_SOURCE_STAMP"
		fi
		;;
	*)
		echo "unknown root dependency: $1" >&2
		exit 2
		;;
	esac
}

show_one() {
	kind=$1 name=$2 dir=$3 policy=$4 ref_text=$5 commit=$6 patch=$7
	if [ -d "$dir/.git" ]; then
		head=$(git -C "$dir" rev-parse --short HEAD)
		branch=$(git -C "$dir" branch --show-current 2>/dev/null || true)
		dirty=clean
		git -C "$dir" diff --quiet --ignore-submodules -- &&
			git -C "$dir" diff --cached --quiet --ignore-submodules -- || dirty=dirty
	else
		head=missing
		branch=-
		dirty=missing
	fi
	if [ "$patch" != "-" ] && { [ -s "$patch" ] || ls "$patch"/*.patch >/dev/null 2>&1; }; then
		patch_state=patches
	else
		patch_state=no-patches
	fi
	echo "$kind/$name policy=$policy ref=$ref_text commit=$commit patch=$patch $patch_state head=$head branch=$branch state=$dirty dir=$dir"
}

status_dep() {
	case "$(normalize_dep "$1")" in
	"")
		status_dep lvgl
		status_dep ffmpeg
		;;
	lvgl)
		show_one root lvgl "$LVGL_DIR" head "$LVGL_REF" "$LVGL_REF" -
		;;
	ffmpeg)
		show_one root ffmpeg "$HCRTOS_FFMPEG_SOURCE" tag "$HCRTOS_FFMPEG_REF" "$HCRTOS_FFMPEG_COMMIT" "$HCRTOS_FFMPEG_PATCH"
		;;
	*)
		echo "unknown root dependency: $1" >&2
		exit 2
		;;
	esac
}

refresh_dep() {
	case "$(normalize_dep "$1")" in
	ffmpeg)
		[ -d "$HCRTOS_FFMPEG_SOURCE/.git" ] || { echo "missing checkout: $HCRTOS_FFMPEG_SOURCE" >&2; exit 1; }
		ensure_clean "$HCRTOS_FFMPEG_SOURCE"
		git -C "$HCRTOS_FFMPEG_SOURCE" diff --full-index --binary unifrog-base..HEAD >"$HCRTOS_FFMPEG_PATCH"
		;;
	lvgl)
		echo "lvgl has no UniFrog patch queue" >&2
		exit 1
		;;
	*)
		echo "unknown root dependency: $1" >&2
		exit 2
		;;
	esac
}

require_update_branch() {
	dir=$1
	name=$2
	[ -d "$dir/.git" ] || { echo "missing checkout: $dir" >&2; exit 1; }
	branch=$(git -C "$dir" branch --show-current 2>/dev/null || true)
	[ "$branch" = unifrog-update ] || {
		echo "$name must be on unifrog-update; run dep-update first" >&2
		exit 1
	}
	ensure_clean "$dir"
}

finalize_dep() {
	[ -n "$ref" ] || { echo "usage: make dep-finalize DEP=<dependency> REF=<ref>" >&2; exit 2; }
	case "$(normalize_dep "$1")" in
	ffmpeg)
		require_update_branch "$HCRTOS_FFMPEG_SOURCE" ffmpeg
		new_commit=$(git -C "$HCRTOS_FFMPEG_SOURCE" rev-parse unifrog-base^{commit})
		sed -i.bak \
			-e "s|^HCRTOS_FFMPEG_REF ?= .*|HCRTOS_FFMPEG_REF ?= $ref|" \
			-e "s|^HCRTOS_FFMPEG_COMMIT ?= .*|HCRTOS_FFMPEG_COMMIT ?= $new_commit|" \
			config/options.mk
		rm -f config/options.mk.bak
		HCRTOS_FFMPEG_REF=$ref
		HCRTOS_FFMPEG_COMMIT=$new_commit
		refresh_dep ffmpeg
		old_ref=$ref
		ref=
		setup_dep ffmpeg
		ref=$old_ref
		status_dep ffmpeg
		;;
	lvgl)
		setup_dep lvgl
		new_commit=$(git -C "$LVGL_DIR" rev-parse unifrog-base^{commit})
		sed -i.bak "s|^LVGL_REF ?= .*|LVGL_REF ?= $new_commit|" config/options.mk
		rm -f config/options.mk.bak
		LVGL_REF=$new_commit
		old_ref=$ref
		ref=
		setup_dep lvgl
		ref=$old_ref
		status_dep lvgl
		;;
	*)
		echo "unknown root dependency: $1" >&2
		exit 2
		;;
	esac
}

range_base() {
	case "$(normalize_dep "$1")" in
	lvgl) printf '%s %s\n' "$LVGL_DIR" "$LVGL_REF" ;;
	ffmpeg) printf '%s %s\n' "$HCRTOS_FFMPEG_SOURCE" "$HCRTOS_FFMPEG_COMMIT" ;;
	*) echo "unknown root dependency: $1" >&2; exit 2 ;;
	esac
}

patches_check() {
	case "$(normalize_dep "$1")" in
	"")
		setup_dep ffmpeg
		status_dep ffmpeg
		;;
	lvgl|ffmpeg)
		setup_dep "$1"
		status_dep "$1"
		;;
	*)
		echo "unknown root dependency: $1" >&2
		exit 2
		;;
	esac
}

case "$cmd" in
setup|edit|update|status|refresh|finalize|log|diff|patches-check) ;;
*) echo "usage: tools/root-deps.sh setup|edit|update|status|refresh|finalize|log|diff|patches-check" >&2; exit 2 ;;
esac

case "$cmd" in
setup)
	[ -n "$dep" ] || { echo "usage: tools/root-deps.sh setup with DEP=<dependency>" >&2; exit 2; }
	setup_dep "$dep"
	;;
edit)
	[ -n "$dep" ] || { echo "usage: make dep-edit DEP=<dependency>" >&2; exit 2; }
	setup_dep "$dep"
	case "$(normalize_dep "$dep")" in
	lvgl) echo "edit checkout prepared on unifrog-base for DEP=lvgl" ;;
	*) echo "edit checkout prepared on unifrog-current for DEP=$(normalize_dep "$dep")" ;;
	esac
	;;
update)
	[ -n "$dep" ] || { echo "usage: make dep-update DEP=<dependency> [REF=<ref>]" >&2; exit 2; }
	setup_dep "$dep"
	;;
status)
	status_dep "$dep"
	;;
refresh)
	[ -n "$dep" ] || { echo "usage: make dep-refresh DEP=<dependency>" >&2; exit 2; }
	refresh_dep "$dep"
	;;
finalize)
	[ -n "$dep" ] || { echo "usage: make dep-finalize DEP=<dependency> REF=<ref>" >&2; exit 2; }
	finalize_dep "$dep"
	;;
log)
	[ -n "$dep" ] || { echo "usage: make dep-log DEP=<dependency>" >&2; exit 2; }
	set -- $(range_base "$dep")
	git -C "$1" log --oneline -20 "$2..HEAD"
	;;
diff)
	[ -n "$dep" ] || { echo "usage: make dep-diff DEP=<dependency>" >&2; exit 2; }
	set -- $(range_base "$dep")
	git -C "$1" diff "$2..HEAD"
	;;
patches-check)
	patches_check "$dep"
	;;
esac
