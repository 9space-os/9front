#!/bin/bash
# The tree's parts for APE (POSIX on 9front) made with the cross compiler of
# toolchain/gcc (GCC_DIR in paths.env), or with CC=clang toolchain/clang's
# (sys/src/build/mesa/common.sh: XCC & co.): build.exe -D on the libraries'
# compatibility manifests (sys/src/build/gen/apeprogs.py's LIBS: sys/src/mesa:
# libvulkan.a, the Vulkan drivers and the static loader, EGL and GLES on zink;
# sys/src/libffi, sys/src/wayland: libwayland; sys/src/sdl3; sys/src/glfw; the
# vendored freetype ... foot: into /$objtype/lib.gcc/ape), then the programs of
# them in /$objtype/bin (apeprogs.PROGS: wlsys by sys/src/build/wayland/build.sh, foot
# from its library, cxxwidgets in C++). build.exe's step
# ape, before rust (wsys links libvulkan.a). Without the cross compiler it says
# so and builds nothing (check-install.py: nocross). A program that comes out
# the same keeps its time (build.exe -D).
# usage: [objtype=amd64] sys/src/build/step/ape.sh
set -e
ROOT=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
objtype=${objtype:-amd64}
if ! python3 -c "import sys; sys.path.insert(0, '$ROOT/sys/src/build/gen'); import apeprogs; sys.exit(not apeprogs.cross('$objtype'))"; then
	echo "ape: no cross compiler for $objtype (toolchain/gcc, GCC_DIR; with CC=clang toolchain/clang, CLANG_DIR, and its runtimes): Mesa, libwayland, SDL3, GLFW, wlsys not built"
	exit 0
fi
. "$ROOT/sys/src/build/mesa/common.sh"
LOG=$BUILD/ape.log
mkdir -p "$BUILD"
for d in $(python3 -c "import sys; sys.path.insert(0, '$ROOT/sys/src/build/gen'); import apeprogs; print(' '.join(apeprogs.LIBS))"); do
	echo "ape: $d"
	CC=$XCC ARCH=$objtype "$ROOT/sys/src/cmd/build/build.exe" -D "$ROOT/$d/build.json" > "$LOG" 2>&1 ||
		{ tail -20 "$LOG"; echo "ape: $d failed (all of it in $LOG)" >&2; exit 1; }
done
# the programs, into /$objtype/bin when they changed
OUT=$BUILD/ape-bin
mkdir -p "$OUT" "$ROOT/$objtype/bin"
bash "$ROOT/sys/src/build/wayland/build.sh" "$OUT"
# programs of a vendored release, from its library (apeprogs.LINKS: foot)
python3 -c "import sys; sys.path.insert(0, '$ROOT/sys/src/build/gen'); import apeprogs
for p, a in sorted(apeprogs.LINKS.items()): print(p, ' '.join(a))" | while read -r p args; do
	"$XCC" -o "$OUT/$p" $args > "$LOG" 2>&1 ||
		{ tail -20 "$LOG"; echo "ape: linking $p failed" >&2; exit 1; }
done
# programs in C++ (apeprogs.CXX: cxxwidgets, ui.hh's)
python3 -c "import sys; sys.path.insert(0, '$ROOT/sys/src/build/gen'); import apeprogs
for p, a in sorted(apeprogs.CXX.items()): print(p, ' '.join(a))" | while read -r p src args; do
	"$XCXX" -std=gnu++17 -O2 -Wall -o "$OUT/$p" "$ROOT/$src" $args > "$LOG" 2>&1 ||
		{ tail -20 "$LOG"; echo "ape: building $p failed" >&2; exit 1; }
	"$XSTRIP" "$OUT/$p"
done
for p in $(python3 -c "import sys; sys.path.insert(0, '$ROOT/sys/src/build/gen'); import apeprogs; print(' '.join(apeprogs.PROGS))"); do
	cmp -s "$OUT/$p" "$ROOT/$objtype/bin/$p" && continue
	cp "$OUT/$p" "$ROOT/$objtype/bin/$p.new"
	mv "$ROOT/$objtype/bin/$p.new" "$ROOT/$objtype/bin/$p"
	echo "ape: /$objtype/bin/$p"
done
