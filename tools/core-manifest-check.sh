#!/bin/sh
set -eu

: "${BUILD:?}"
: "${CORES:?}"
: "${ASSOCIATIONS_DEFAULT:?}"
: "${CORE_PACKAGE_SPECS:?}"
: "${CORE_LICENSE_SPECS:?}"
: "${CORE_SPECS:?}"
: "${LIBRETRO_CORE_IDS:?}"
: "${NESTED_CORE_SPECS:?}"

tmp="$BUILD/core-manifest-check"
mkdir -p "$BUILD"
: >"$tmp.ids"
: >"$tmp.targets"
: >"$tmp.license-ids"
: >"$tmp.license-names"
: >"$tmp.nested"

eval "set -- $CORE_PACKAGE_SPECS"
for module do
	id=${module%%:*}; rest=${module#*:}
	var=${rest%%:*}; rest=${rest#*:}
	stem=${rest%%:*}; rest=${rest#*:}
	target=${rest%%:*}; rest=${rest#*:}
	archive=${rest%%:*}
	echo "$id" >>"$tmp.ids"
	echo "$target" >>"$tmp.targets"
	case " $LIBRETRO_CORE_IDS " in *" $id "*) ;; *) echo "missing registered core id: $id"; exit 1 ;; esac
	grep -q "'$target|" "$CORES/manifest.mk" || { echo "missing CORE_SPECS record for target $target"; exit 1; }
	grep -Eq "^[[:space:]]*$target:" "$CORES/Makefile" || { echo "missing cores/Makefile build rule for $target"; exit 1; }
	grep -q "$archive""_libretro_sf2000.a" "$CORES/Makefile" || { echo "missing archive reference for $target: $archive"; exit 1; }
done

for license in $CORE_LICENSE_SPECS; do
	id=${license%%|*}; rest=${license#*|}
	source=${rest%%|*}; destination=${rest#*|}
	test "$source" != "$rest" && test "$destination" != "$rest" || {
		echo "invalid core license spec: $license"
		exit 1
	}
	grep -qx "$id" "$tmp.ids" || {
		echo "license for unknown core package: $id"
		exit 1
	}
	echo "$id" >>"$tmp.license-ids"
	echo "$destination" >>"$tmp.license-names"
done

if sort "$tmp.license-ids" | uniq -d | grep .; then
	echo "duplicate core license ids"
	exit 1
fi
if sort "$tmp.license-names" | uniq -d | grep .; then
	echo "duplicate packaged core license names"
	exit 1
fi
sort "$tmp.ids" >"$tmp.ids.sorted"
sort "$tmp.license-ids" >"$tmp.license-ids.sorted"
if ! diff -u "$tmp.ids.sorted" "$tmp.license-ids.sorted"; then
	echo "every external core package must provide license material"
	exit 1
fi

eval "set -- $NESTED_CORE_SPECS"
for nested do
	parent=${nested%%|*}; rest=${nested#*|}
	subpath=${rest%%|*}; rest=${rest#*|}
	commit=${rest%%|*}; patch_dir=${rest#*|}
	grep -qx "$parent" "$tmp.ids" || {
		echo "nested dependency for unknown core: $parent"
		exit 1
	}
	case "$subpath" in ""|/*|*..*) echo "unsafe nested path: $subpath"; exit 1 ;; esac
	case "$commit" in *[!0-9a-f]*) echo "invalid nested commit: $commit"; exit 1 ;; esac
	test "${#commit}" -eq 40 || {
		echo "invalid nested commit length: $commit"
		exit 1
	}
	test -d "$CORES/$patch_dir" || {
		echo "missing nested patch dir: $CORES/$patch_dir"
		exit 1
	}
	echo "$parent/$subpath" >>"$tmp.nested"
done
if sort "$tmp.nested" | uniq -d | grep .; then
	echo "duplicate nested dependencies"
	exit 1
fi

awk -F= -v core_ids="$LIBRETRO_CORE_IDS" '
function fail(message) {
   print message >"/dev/stderr"
   failed=1
}
function trim(text) {
   sub(/^[ \t]+/, "", text)
   sub(/[ \t]+$/, "", text)
   return text
}
function extension_name(key, field, extension) {
   extension=key
   sub(/^extension[.]/, "", extension)
   sub("[.]" field "$", "", extension)
   return extension
}
function validate_extension(extension) {
   if (length(extension) < 1 || length(extension) > 15 ||
       extension !~ /^[abcdefghijklmnopqrstuvwxyz0123456789._+-]+$/)
      fail("invalid association extension: " extension)
}
function validate_handler(extension, handler) {
   if (length(handler) < 1 || length(handler) > 31)
      fail("invalid handler for association ." extension ": " handler)
   else if (!known[handler] && !builtin[handler])
      fail("association ." extension " names unknown handler: " handler)
}
BEGIN {
   split(core_ids, ids, " ")
   for (i in ids)
      known[ids[i]]=1
   split("media reader native ffmpeg wav-auddec hcplayer hcplayer-audio hcplayer-muted",
      ids, " ")
   for (i in ids)
      builtin[ids[i]]=1
}
/^extension[.].+[.]handlers=/ {
   extension=extension_name($1, "handlers")
   validate_extension(extension)
   if (++handler_defs[extension] != 1)
      fail("duplicate handlers for association ." extension)
   count=split($2, values, ",")
   if (count > 8)
      fail("too many handlers for association ." extension)
   for (i=1; i<=count; i++) {
      handler=trim(values[i])
      validate_handler(extension, handler)
      key=extension SUBSEP handler
      if (members[key]++)
         fail("duplicate handler for association ." extension ": " handler)
   }
}
/^extension[.].+[.]default=/ {
   extension=extension_name($1, "default")
   handler=trim($2)
   validate_extension(extension)
   validate_handler(extension, handler)
   if (++default_defs[extension] != 1)
      fail("duplicate default for association ." extension)
   defaults[extension]=handler
}
END {
   for (extension in handler_defs) {
      if (default_defs[extension] != 1)
         fail("association ." extension " needs exactly one default")
      else if (!members[extension SUBSEP defaults[extension]])
         fail("association ." extension " default is not in handlers: " \
            defaults[extension])
   }
   for (extension in default_defs)
      if (handler_defs[extension] != 1)
         fail("association ." extension " needs exactly one handlers list")
   exit failed
}' "$ASSOCIATIONS_DEFAULT"

if sort "$tmp.ids" | uniq -d | grep .; then
	echo "duplicate core package ids"
	exit 1
fi

eval "set -- $CORE_SPECS"
for spec do
	name=${spec%%|*}; rest=${spec#*|}
	dir=${rest%%|*}; rest=${rest#*|}
	url=${rest%%|*}; rest=${rest#*|}
	policy=${rest%%|*}; rest=${rest#*|}
	ref=${rest%%|*}; rest=${rest#*|}
	commit=${rest%%|*}; rest=${rest#*|}
	patch_dir=${rest%%|*}; rest=${rest#*|}
	if [ "$patch_dir" != "-" ]; then
		test -d "$CORES/$patch_dir" || { echo "missing patch dir: $CORES/$patch_dir"; exit 1; }
	fi
	case "$name" in
	libretro-common) ;;
	*) grep -qx "$name" "$tmp.targets" || { echo "unpackaged core spec: $name"; exit 1; } ;;
	esac
done

rm -f "$tmp.ids" "$tmp.targets" "$tmp.license-ids" \
	"$tmp.license-names" "$tmp.ids.sorted" "$tmp.license-ids.sorted"
rm -f "$tmp.nested"
echo "OK"
