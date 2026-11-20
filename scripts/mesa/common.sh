# shared settings of scripts/mesa/*.sh (sourced)
ROOT=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
. "$ROOT/scripts/lib/paths.sh"	# GCC_DIR, RUST_DIR, GO_DIR, MCP_DIR, DISTFILES (the distribution's paths.env)
MESA=$ROOT/sys/src/mesa
field(){ awk -v k="$1" '$1 == k { print $2 }' "$MESA/VERSION"; }
MESA_VERSION=$(field version)
MESA_URL=$(field url)
MESA_SHA256=$(field sha256)
MESA_KEY=$(field key)
# work directory: downloads, the source tree (a git repository, tag
# pristine = the tarball), the venv with meson and Mesa's python
# modules, the build directories
WORK=${MESA_WORK:-$CACHE/mesa}
SRC=$WORK/src/mesa-$MESA_VERSION
VENV=$WORK/venv
# a meson build directory set up elsewhere (WORK moved: its build.ninja calls the
# old venv's meson by its absolute path, and ninja's regenerate step fails), or
# for another source tree (a new Mesa release in VERSION: it built the old one's)
moved(){
	[ -f "$1/build.ninja" ] || return 1
	! grep -q -F "$VENV/bin/meson" "$1/build.ninja" ||
	! grep -q -E "\"source\": *\"$SRC\"" "$1/meson-info/meson-info.json" 2>/dev/null
}
objtype=${objtype:-amd64}
case $objtype in
amd64)	TRIPLE=x86_64-pc-plan9 CPU=x86_64 ;;
386)	TRIPLE=i686-pc-plan9 CPU=x86 ;;
# (no Mesa there yet: the sysroot view and cross tools for scripts/build/cargo.sh)
arm64)	TRIPLE=aarch64-pc-plan9 CPU=aarch64 ;;
*)	echo "mesa: objtype=$objtype not supported" >&2; exit 1 ;;
esac
# the cross compilers of toolchain/gcc (GCC_DIR in paths.env)
GCC_CROSS=$GCC_DIR/build/$objtype/cross/bin
BUILD=$WORK/build-$objtype
# their sysroot is a view of one tree ($GCC_DIR/sysroot.sh, the PLAN9 of its last
# build: the gcc branch's worktree tree-gcc, TREE_GCC); against this tree instead, its headers
# (APE's errno.h with ENODATA) and libraries (libvulkan.a): $CROSS holds the cross
# tools with gcc and g++ given --sysroot= a view of $ROOT (made again when WORK moved:
# the wrappers name the sysroot by its absolute path)
SYSROOT=$BUILD/sysroot
CROSS=$BUILD/cross/bin
if [ -x "$GCC_CROSS/$TRIPLE-gcc" ] &&
	{ [ "$(readlink "$SYSROOT/sys/include")" != "$ROOT/sys/include" ] || [ ! -x "$CROSS/$TRIPLE-g++" ] ||
	  ! grep -qF -- "--sysroot=\"$SYSROOT\"" "$CROSS/$TRIPLE-gcc"; }; then
	mkdir -p "$BUILD"
	PLAN9=$ROOT objtype=$objtype bash "$GCC_DIR/sysroot.sh" "$SYSROOT"
	rm -rf "$CROSS"; mkdir -p "$CROSS"
	for t in "$GCC_CROSS"/$TRIPLE-*; do
		ln -s "$t" "$CROSS/${t##*/}"
	done
	for t in gcc g++ c++ cpp; do
		rm "$CROSS/$TRIPLE-$t"
		printf '#!/bin/sh\nexec "%s" --sysroot="%s" "$@"\n' "$GCC_CROSS/$TRIPLE-$t" "$SYSROOT" > "$CROSS/$TRIPLE-$t"
		chmod +x "$CROSS/$TRIPLE-$t"
	done
fi
