#!/bin/sh
# build.sh OUT: our ufs (main.go here, Harvey's ninep vendored) into OUT,
# if OUT is missing or older than a source; no network (-mod=vendor,
# GOPROXY=off). Prints OUT. boot-cli.sh and pxe.sh take it as UFS.
set -e
out=$1
here=$(cd "$(dirname "$0")" && pwd)
[ -n "$out" ] || { echo "usage: build.sh OUT" >&2; exit 2; }
if [ ! -x "$out" ] || [ -n "$(find "$here" -newer "$out" -type f -print -quit)" ]; then
	command -v go >/dev/null || { echo "build.sh: go missing" >&2; exit 1; }
	mkdir -p "$(dirname "$out")"
	(cd "$here" && GOFLAGS=-mod=vendor GOPROXY=off CGO_ENABLED=0 go build -o "$out.new" . && mv "$out.new" "$out") >&2
fi
echo "$out"
