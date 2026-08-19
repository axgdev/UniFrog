#!/bin/sh
set -eu

: "${MAKE_CMD:=make}"

generated_pattern='(^build/|^output/|^\.deps|^cores/build/|^cores/output/|^components/frontend/assets/output/|^js2300/build/|^js2300/output/|~$|\.tmp$|\.DS_Store$|\.o$|\.d$|\.out$|\.map$|\.bin$|\.dtb$|\.dts\.tmp$|\.pre\.tmp$)'

git diff --check -- . ':(exclude)cores/patches/**' ':(exclude)patches/**'
git diff --cached --check -- . ':(exclude)cores/patches/**' ':(exclude)patches/**'
"$MAKE_CMD" --no-print-directory core-manifest-check

tracked=$(git ls-files | grep -E "$generated_pattern" || true)
if [ -n "$tracked" ]; then
	echo "tracked generated file found" >&2
	printf '%s\n' "$tracked" >&2
	exit 1
fi

echo "OK"
