#!/bin/bash
# Build the Vulkan tests of sys/src/build/host/tests/gpu (POSIX, APE) into root/tests, against
# Mesa as the tree has it: build.exe makes /$objtype/lib.gcc/ape/libvulkan.a
# from sys/src/mesa/build.json (the vendored drivers: venus, hasvk, RADV; the static loader,
# libmesaplan9; sys/src/build/mesa/vendor.sh), the headers are in /sys/include/ape.
# Programs link -lvulkan, APE's libz (Mesa's zlib is the tree's), and APE's
# libdraw and lib9 for the plan9 WSI.
# vktest and vkdraw call the driver's vk_icdGetInstanceProcAddr, the others
# are ordinary Vulkan programs; vktri's shaders are compiled with glslc.
# The OpenGL tests (gltest, gldesk, glwin, glage: zink, EGL) link the same library.
# Each program is linked into $BUILD/tests (with its debug information:
# addr2line, gdb) and goes stripped into root/tests, replaced there only
# when it changed (putstripped.sh: the guest's root is kept up to date by
# rsync, an unchanged program is not copied again).  root/tests/vkprogs
# lists the programs put there: a program the last run put and this one did
# not is removed at the end, and run.sh's novk removes them all when this
# script fails, so that no stale program runs.
set -e
. "$(dirname "$0")/../../../mesa/common.sh"
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$HERE/root/tests" "$BUILD/tests"
O=$BUILD/tests
CC=$XCC ARCH=$objtype "$ROOT/sys/src/cmd/build/build.exe" -D "$ROOT/sys/src/mesa/build.json" \
	> "$O/libvulkan.log" 2>&1 || { tail -20 "$O/libvulkan.log"; exit 1; }
libs="-Wl,--gc-sections -lvulkan -lz -ldraw -l9 -lfmt -lutf"
L=$HERE/root/tests/vkprogs
: > "$L.new"
put(){
	local n=${2:-${1##*/}}
	bash "$HERE/putstripped.sh" "$XSTRIP" "$1" "$HERE/root/tests/$n"
	echo "$n" >> "$L.new"
}
"$XCC" -std=c11 -O2 -Wall -c -o "$O/btassert.o" "$HERE/btassert.c"
"$XCC" -std=c11 -O2 -Wall -D_PLAN9_SOURCE -I"$ROOT/sys/src/ape/lib/9" \
	-c -o "$O/screenpeek.o" "$HERE/screenpeek.c"
for s in tri.vert tri.frag; do
	glslc -O -mfmt=c -o "$O/$s.inc" "$HERE/shaders/$s"
done
# futexstress: Mesa's futex for 9front, the vendored util/futex.c compiled in (the
# tree's file, as libvulkan.a has it; mesa.json's -DHAVE_STRUCT_TIMESPEC), no Vulkan
V=$ROOT/sys/src/mesa/vendor/src
"$XCC" -std=gnu11 -O2 -Wall -DHAVE_STRUCT_TIMESPEC -I"$V" \
	-o "$O/futexstress" "$HERE/futexstress.c" "$V/util/futex.c"
put "$O/futexstress"
for p in vktest vkdraw vktri vkspeed vkshare radvtest vktimeline gltest gldesk glwin glage; do
	extra=
	case $p in
	vkdraw|vktri)	extra="$O/btassert.o $O/screenpeek.o" ;;
	vktest)		extra=$O/btassert.o ;;
	esac
	"$XCC" -std=gnu11 -O2 -Wall -I"$O" -c -o "$O/$p.o" "$HERE/$p.c"
	"$XCXX" -o "$O/$p" "$O/$p.o" $extra $libs
	put "$O/$p"
done
# sdlvk: SDL3's Vulkan renderer (sys/src/build/host/tests/sdl/sdlvk.c), if libSDL3.a is built
# (sys/src/sdl3); SDL refers to Vulkan weakly: -u pulls the loader in
if [ -f "$ROOT/$objtype/lib.gcc/ape/libSDL3.a" ]; then
	"$XCC" -std=gnu11 -O2 -Wall -c -o "$O/sdlvk.o" "$ROOT/sys/src/build/host/tests/sdl/sdlvk.c"
	"$XCXX" -o "$O/sdlvk" "$O/sdlvk.o" -Wl,-u,vkGetInstanceProcAddr -lSDL3 $libs -lm
	put "$O/sdlvk"
	# sdlgl: SDL3's OpenGL (EGL's plan9 platform); SDL refers to EGL weakly too
	"$XCC" -std=gnu11 -O2 -Wall -c -o "$O/sdlgl.o" "$ROOT/sys/src/build/host/tests/sdl/sdlgl.c"
	"$XCXX" -o "$O/sdlgl" "$O/sdlgl.o" -Wl,-u,eglGetDisplay -lSDL3 $libs -lm
	put "$O/sdlgl"
	# sdlgpu: SDL3's GPU API (SDL_gpu on Vulkan)
	"$XCC" -std=gnu11 -O2 -Wall -c -o "$O/sdlgpu.o" "$ROOT/sys/src/build/host/tests/sdl/sdlgpu.c"
	"$XCXX" -o "$O/sdlgpu" "$O/sdlgpu.o" -Wl,-u,vkGetInstanceProcAddr -lSDL3 $libs -lm
	put "$O/sdlgpu"
fi
# glfwtest: GLFW (sys/src/build/host/tests/glfw/glfwtest.c, sys/src/glfw), with -g its OpenGL and Vulkan;
# GLFW refers to EGL and Vulkan weakly (plan9_module.c): -u pulls them in
if [ -f "$ROOT/$objtype/lib.gcc/ape/libglfw3.a" ]; then
	"$XCC" -std=gnu11 -O2 -Wall -c -o "$O/glfwtest.o" "$ROOT/sys/src/build/host/tests/glfw/glfwtest.c"
	"$XCXX" -o "$O/glfwtest" "$O/glfwtest.o" \
		-Wl,-u,eglGetDisplay -Wl,-u,vkGetInstanceProcAddr -lglfw3 $libs -lm
	put "$O/glfwtest"
fi
# rsvk: vktri in Rust (ash), built by cargo with the stage-1 rustc of toolchain/rust
# (RUST_DIR in paths.env) for x86_64-unknown-plan9 and linked by the cross C++ driver (XCXX: g++ or
# clang++, which adds its C++ library for libvulkan.a) above (the view of this
# tree: libp9, APE, libvulkan.a); the crates are in rsvk/vendor (no network). Skipped
# without that rustc.
RUSTC=${RUSTC:-$RUST_DIR/rust/build/host/stage1/bin/rustc}
if [ "$objtype" = amd64 ] && [ -x "$RUSTC" ] && command -v cargo >/dev/null; then
	for s in tri.vert tri.frag; do
		glslc -O -o "$O/$s.spv" "$HERE/shaders/$s"
	done
	"$XAR" rcs "$O/libscreenpeek.a" "$O/screenpeek.o"
	T=x86_64-unknown-plan9
	# crates built with another std than the sysroot's go (sys/src/build/step/cargostd.sh)
	. "$ROOT/sys/src/build/step/cargostd.sh"
	cargostd "$O/rsvk" $T
	cargostd "$BUILD/cargo/eframetest" $T
	# cargo does not know the C libraries it links (libvulkan.a, APE's): a
	# program older than one of them goes, and cargo links it again (as
	# sys/src/build/step/cargo.sh does)
	for p in "$O/rsvk/$T/release/rsvk" "$BUILD/cargo/eframetest/$T/release/eframetest"; do
		if [ -e "$p" ] && [ -n "$(find "$ROOT/$objtype/lib.gcc" -name '*.a' -newer "$p" -print -quit 2>/dev/null)" ]; then
			rm -f "$p" "$(dirname "$p")/deps/$(basename "$p")"-*
		fi
	done
	(cd "$HERE/rsvk" &&
	 RUSTC="$RUSTC" RSVK_SPV="$O" CARGO_TARGET_DIR="$O/rsvk" \
	 CARGO_TARGET_X86_64_UNKNOWN_PLAN9_LINKER="$XCXX" \
	 RUSTFLAGS="-L $O -l screenpeek -l draw -l 9 -l fmt -l utf -C link-arg=-Wl,--gc-sections" \
	 cargo build --offline --release --target $T)
	put "$O/rsvk/$T/release/rsvk"
	# eframetest: eframe on wgpu's Vulkan, RUST_DIR's (tests/crates/eframetest:
	# crates.io's crates, its patched winit and wgpu-*: cargo-plan9.toml)
	if [ -d "$RUST_DIR/tests/crates/eframetest" ]; then
		(cd "$RUST_DIR/tests/crates/eframetest" &&
		 RUSTC="$RUSTC" CARGO_TARGET_DIR="$BUILD/cargo/eframetest" \
		 CARGO_TARGET_X86_64_UNKNOWN_PLAN9_LINKER="$XCXX" \
		 RUSTFLAGS="-l draw -l 9 -l fmt -l utf -C link-arg=-Wl,--gc-sections" \
		 cargo --config "$RUST_DIR/cargo-plan9.toml" build --release --target $T --quiet)
		put "$BUILD/cargo/eframetest/$T/release/eframetest"
	fi
else
	echo "rsvk: not built (objtype=$objtype, or no stage-1 rustc $RUSTC or cargo)" >&2
fi
# Khronos' vkcube and vulkaninfo, ported (sys/src/build/mesa/vktools.sh)
"$ROOT/sys/src/build/mesa/vktools.sh" build
put "$BUILD/vktools/vkcube"
put "$BUILD/vktools/vulkaninfo"
# what the last run put and this one did not goes; the list is this run's
if [ -f "$L" ]; then
	sort -u "$L.new" | comm -13 - <(sort -u "$L") | while read -r n; do rm -f "$HERE/root/tests/$n"; done
fi
mv "$L.new" "$L"
(cd "$HERE/root/tests" && ls -la $(cat "$L"))
