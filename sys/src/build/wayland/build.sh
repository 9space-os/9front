#!/bin/bash
# Build libffi and libwayland (build.exe -D: sys/src/libffi, sys/src/wayland) and
# wlsys (sys/src/cmd/wlsys, the Wayland server in wsys) with the cross gcc of
# toolchain/gcc (GCC_DIR); wlsys goes to outdir. sys/src/build/step/ape.sh runs it
# in the full build (and installs wlsys).
# usage: sys/src/build/wayland/build.sh outdir   (amd64)
set -e
[ $# -eq 1 ] || { echo "usage: sys/src/build/wayland/build.sh outdir" >&2; exit 1; }
ROOT=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
# amd64 only (sys/src/build/gen/apeprogs.py builds the Wayland parts for amd64):
# the manifests below are amd64's, and common.sh would set up another
# objtype's compiler and sysroot first
[ "${objtype:-amd64}" = amd64 ] || { echo "wayland/build.sh: objtype=$objtype: amd64 only" >&2; exit 1; }
. "$ROOT/sys/src/build/mesa/common.sh"
LOG=${TMPDIR:-/tmp}/wayland-build.$$.log
for m in sys/src/libffi/build.json sys/src/wayland/build.json; do
	CC=$CROSS/$TRIPLE-gcc ARCH=amd64 "$ROOT/sys/src/cmd/build/build.exe" -D "$ROOT/$m" > "$LOG" 2>&1 ||
		{ tail -20 "$LOG"; rm -f "$LOG"; exit 1; }
done
rm -f "$LOG"
mkdir -p "$1"
W=$ROOT/sys/src/cmd/wlsys
"$CROSS/$TRIPLE-gcc" -std=gnu11 -O2 -Wall -o "$1/wlsys" \
	"$W"/*.c \
	-lwayland-server -lffi
