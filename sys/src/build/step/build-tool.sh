#!/bin/bash
# The build tool for 9space: sys/src/cmd/build (Go) for GOOS=plan9 as
# /$objtype/bin/build, so that the CD and the update archives carry it and an
# installed system builds itself with it (build.exe's step install,
# sys/src/build/gen/genbuild.py). Go is toolchain/go's (GO_DIR in paths.env: the
# one with plan9/arm64), else the host's (no plan9/arm64 upstream). A tool that
# comes out the same keeps its time (-trimpath: the same sources, the same file).
# usage: [objtype=amd64] sys/src/build/step/build-tool.sh
# The rc twin for the build on 9space: sys/src/build/step/rc/build-tool.rc.
set -e
ROOT=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
. "$ROOT/sys/src/build/lib/paths.sh"	# GO_DIR
A=${objtype:-amd64}
case $A in
amd64|386|arm64) ;;
*)	echo "build-tool: Go has no plan9/$A: no /$A/bin/build" >&2; exit 0 ;;
esac
GO=$GO_DIR/go/bin/go
[ -x "$GO" ] || GO=$(command -v go) || { echo "build-tool: no Go (GO_DIR: toolchain/go, or the host's)" >&2; exit 1; }
"$GO" tool dist list | grep -qx "plan9/$A" || { echo "build-tool: $GO has no plan9/$A (toolchain/go has)" >&2; exit 1; }
out=$ROOT/$A/bin/build
mkdir -p "$ROOT/$A/bin"
(cd "$ROOT/sys/src/cmd/build" && GOOS=plan9 GOARCH=$A GO111MODULE=off CGO_ENABLED=0 \
	"$GO" build -trimpath -o "$out.new" .)
if cmp -s "$out.new" "$out"; then rm "$out.new"; else mv "$out.new" "$out"; fi
