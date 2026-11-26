#!/bin/bash
# The first step of the build for $objtype (build.$objtype.json, run by
# sys/src/cmd/build/build.exe): the install directories the manifests
# expect, and the compiler's runtime library (64-bit division on 386, ...:
# 8c had vlrt.c), linked after the libraries (/$objtype/cflags.json) as
# /$objtype/lib.gcc/$CCRT (build.exe's: libgcc.a for gcc; for clang,
# TOOLSET=clang, libclang_rt.builtins.a: toolchain/clang's compiler-rt for
# $objtype, CLANG_DIR/out, the rest below is gcc's).
#
# 386: the runtime library of toolchain/gcc's i686-pc-plan9 (GCC_DIR in
# paths.env; GCC_OUT, default GCC_DIR/out/386, as
# sys/src/build/host/guest/stage-dist.sh), built for the tree's -march=pentium. gcc
# -m32's is the x86-64 Linux multilib's: SSE2 and cmov in its 64-bit
# division and friends, which the 386 programs and the kernel link (a
# Pentium has neither, the kernel does not save the SSE registers). Without
# that libgcc.a the host's is used, with a warning, and libgcc.a.host beside
# it, which sys/src/build/step/check-install.py reports at the end ("hostlib").
#
# arm64: the libgcc.a of ${TOOLPREFIX}gcc (default aarch64-linux-musl-);
# amd64: the host gcc's.
#
# usage: objtype=386|amd64|arm64 sys/src/build/step/prep-objtype.sh   (build.exe sets objtype)
set -e
ROOT=${PLAN9:-$(git -C "$(dirname "$0")" rev-parse --show-toplevel)}
. "$ROOT/sys/src/build/lib/paths.sh"	# GCC_DIR, RUST_DIR, GO_DIR, MCP_DIR, DISTFILES (the distribution's paths.env)
objtype=${objtype:?objtype not set}
cd "$ROOT"
mkdir -p $objtype/bin/aux $objtype/lib.gcc
# copied only when it differs: every program links it, a new time would
# make them all out of date for build.exe -D
copy(){ cmp -s "$1" "$2" || cp "$1" "$2"; }
# a build.exe older than CCRT (2026-11-26) leaves the manifests'
# /$objtype/lib.gcc/$CCRT empty, and the links fail late with ld's "read
# in flex scanner failed": say so first
[ -n "$CCRT" ] || { echo "prep-objtype.sh: CCRT not set: build.exe is older than the manifests (cd sys/src/cmd/build && GO111MODULE=off go build -o build.exe .)" >&2; exit 1; }
if [ "${TOOLSET:-gcc}" = clang ]; then
	lib=$(ls "$CLANG_DIR"/out/$objtype/$objtype/clang/lib/clang/*/lib/plan9/libclang_rt.builtins-*.a 2>/dev/null | tail -1)
	[ -n "$lib" ] || { echo "prep-objtype.sh: no compiler-rt for $objtype in $CLANG_DIR/out (toolchain/clang: objtype=$objtype ./build.sh runtimes)" >&2; exit 1; }
	copy "$lib" $objtype/lib.gcc/libclang_rt.builtins.a
else
case $objtype in
386)
	GCC_OUT=${GCC_OUT:-$GCC_DIR/out/386}
	lib=$(ls "$GCC_OUT"/386/gnu/lib/gcc/i686-pc-plan9/*/libgcc.a 2>/dev/null | tail -1)
	if [ -z "$lib" ]; then
		lib=$(gcc -m32 -print-libgcc-file-name)
		echo "prep-objtype.sh: warning: no $GCC_OUT/386/gnu/lib/gcc/i686-pc-plan9/*/libgcc.a" \
			"(GCC_DIR: toolchain/gcc, objtype=386 build.sh cross native); the host's $lib" \
			"needs SSE2 and cmov (not a Pentium)" >&2
		echo "$lib" >386/lib.gcc/libgcc.a.host
	else
		rm -f 386/lib.gcc/libgcc.a.host
	fi
	copy "$lib" 386/lib.gcc/libgcc.a
	;;
arm64)	copy "$(${TOOLPREFIX-aarch64-linux-musl-}gcc -print-libgcc-file-name)" arm64/lib.gcc/libgcc.a ;;
# the Linux drivers (sys/src/linux) need gcc's helpers (128-bit division)
amd64)	copy "$(gcc -print-libgcc-file-name)" amd64/lib.gcc/libgcc.a ;;
esac
fi
# POSIX's names of libraries whose functions are all in libap (gcc's APE,
# as musl): empty archives, for builds that name them (-lm -lpthread -lrt
# -ldl: LLVM, autoconf)
mkdir -p $objtype/lib.gcc/ape
for l in m pthread rt dl; do
	[ -f $objtype/lib.gcc/ape/lib$l.a ] || printf '!<arch>\n' > $objtype/lib.gcc/ape/lib$l.a
done
