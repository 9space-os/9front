#!/bin/bash
# a library for APE into the tree from its release (sys/src/<lib>/VENDOR.json):
# sys/src/build/ape/vendor.py with the cross compiler of toolchain/gcc (GCC_DIR in
# paths.env; its -MM finds
# what the sources include). Then build.exe -D builds it (sys/src/build/step/ape.sh).
# usage: sys/src/build/ape/vendor.sh lib
set -e
[ $# -eq 1 ] || { echo "usage: sys/src/build/ape/vendor.sh lib" >&2; exit 1; }
. "$(dirname "$0")/../mesa/common.sh"	# CROSS, TRIPLE: the cross gcc with the tree as sysroot
CC=$CROSS/$TRIPLE-gcc WORK=${APE_WORK:-$CACHE/ape} \
	python3 "$(dirname "$0")/vendor.py" "$ROOT" "$1"
