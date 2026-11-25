#!/bin/bash
# uiserver: ui (sys/src/cmd/ui: a window of widgets
# a client describes through files) in a window of wsys on a VESA screen (and
# uirestart.rc: a ui program outlives its wsys), a
# client in rc (sys/src/build/host/tests/uiserver/uiserver.rc) and one in C through libui
# (uiclient.c), uitodo (sys/src/cmd/uidemo: Rust, uiview) and gocount (Go,
# sys/src/libui/go), the widget demos (widgets.rc: Rust, Go, C++, rc), in a
# guest of sys/src/build/host/guest/mcp-guest.sh; then, if the host has a render node,
# ui across its GPU driver's restart (uilost.rc, venus) in a second guest.
# usage: sys/src/build/host/tests/uiserver/run.sh   (needs the stage-1 rustc of toolchain/rust (RUST_DIR), a build: wsys)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../../.." && pwd)
. "$ROOT/sys/src/build/host/tests/lib.sh"	# objtype, skipall, atexit (the guest's cleanup)
[ "$objtype" = amd64 ] || skipall uiserver "amd64 only"
. "$ROOT/sys/src/build/mesa/common.sh"	# CROSS, TRIPLE, BUILD
RUSTC=${RUSTC:-$RUST_DIR/rust/build/host/stage1/bin/rustc}
[ -x "$CROSS/$TRIPLE-gcc" ] || skipall uiserver "no cross compiler $TRIPLE-gcc (toolchain/gcc, GCC_DIR)"
[ -x "$RUSTC" ] || skipall uiserver "no stage-1 rustc of toolchain/rust ($RUSTC)"
command -v cargo >/dev/null || skipall uiserver "no cargo on the host"
[ -x "$ROOT/amd64/bin/wsys" ] || skipall uiserver "no amd64/bin/wsys (a build)"
T=x86_64-unknown-plan9
. "$ROOT/sys/src/build/step/cargostd.sh"
cargostd "$BUILD/cargo/rust" $T
(cd "$ROOT/sys/src/rust" && env RUSTC="$RUSTC" CARGO_TARGET_DIR="$BUILD/cargo/rust" \
	CARGO_TARGET_X86_64_UNKNOWN_PLAN9_LINKER="$CROSS/$TRIPLE-gcc" \
	cargo build --offline --release --target $T --quiet -p ui -p uidemo)
mkdir -p "$HERE/root/tests" "$HERE/root/lib/font/ttf"
(cd "$ROOT/sys/src/build/host/tests/ui" && env PLAN9="$ROOT" ARCH=amd64 CC=gcc "$ROOT/sys/src/cmd/build/build.exe" build.json >/dev/null)
(cd "$HERE" && env PLAN9="$ROOT" ARCH=amd64 CC=gcc "$ROOT/sys/src/cmd/build/build.exe" build.json >/dev/null)
for p in ui uitodo uitick uicount uiwidgets; do
	"$CROSS/$TRIPLE-strip" -o "$HERE/root/tests/$p" "$BUILD/cargo/rust/$T/release/$p"
done
# the clients in Go, with toolchain/go (GO_DIR) when it is there (a build cache of its
# own: the toolchain is rebuilt under the same version)
GO=${GO:-$GO_DIR/go/bin/go}
rm -f "$HERE/root/tests/gocount" "$HERE/root/tests/gowidgets"
if [ -x "$GO" ]; then
	for c in gocount gowidgets; do
		(cd "$ROOT/sys/src/libui/go" && GOOS=plan9 GOARCH=amd64 CGO_ENABLED=0 GOCACHE="$BUILD/gocache" \
			"$GO" build -o "$HERE/root/tests/$c" ./cmd/$c)
	done
fi
cp "$ROOT/amd64/bin/wsys" "$ROOT/sys/src/build/host/tests/ui/root/tests/uipeek" "$HERE/root/tests/"
cp "$ROOT/lib/font/ttf/"*.ttf "$HERE/root/lib/font/ttf/"
cp "$HERE/uiserver.rc" "$HERE/root/tests/uiserver"
# the widget demos: C++'s with the cross g++ of toolchain/gcc (APE, ui.hh); rc's from the
# tree; Go's and Rust's built above
"$CROSS/$TRIPLE-g++" -std=gnu++17 -O2 -Wall -o "$BUILD/cxxwidgets" "$ROOT/sys/src/libui/cxx/cxxwidgets.cc" -l9
"$CROSS/$TRIPLE-strip" -o "$HERE/root/tests/cxxwidgets" "$BUILD/cxxwidgets"
cp "$HERE/widgets.rc" "$HERE/root/tests/widgets"
cp "$ROOT/rc/bin/rcwidgets" "$HERE/root/tests/rcwidgets"
chmod +x "$HERE/root/tests/widgets" "$HERE/root/tests/rcwidgets"
cp "$HERE/uirestart.rc" "$HERE/root/tests/uirestart"
cp "$HERE/uilost.rc" "$HERE/root/tests/uilost"
chmod +x "$HERE/root/tests/uilost"
# a plumber before wsys (its windows, ui among them, find it): plumbtest... to testport
cp "$ROOT/sys/src/build/host/tests/ui/plumbrules" "$HERE/root/tests/plumbrules"
chmod +x "$HERE/root/tests/uiserver" "$HERE/root/tests/uirestart"

UFS_EXTRA=$HERE/root MEM=1024 KARGS="vgasize=1024x768x32 monitor=vesa mouseport=ps2" \
	deskguest "$CLI_DIR/uiserverguest.log" 'aux/realemu' 'aux/vga -m vesa -l 1024x768x32' \
	"bind -a '#i' /dev" "bind -a '#m' /dev" || { echo "FAIL no guest ($CLI_DIR/uiserverguest.log)"; exit 1; }
M="timeout 1000 python3 $ROOT/sys/src/build/host/guest/mcp.py"
$M wait 600 >/dev/null
$M rc 'bind /tests/ui /bin/ui; plumber -p /tests/plumbrules >/dev/null >[2=1] </dev/null; /tests/wsys -x -i /tests/uiserver >/tmp/wsys.log >[2=1] </dev/null' 900 >"$CLI_DIR/uiserver.out" || true
out=$($M rc 'cat /tmp/wsys.log' 20) || true
echo "$out" | grep -v -e '/srv/wsys'
fail=0
echo "$out" | grep -q '^uiserver: all passed' || fail=1
# a ui program outlives its wsys, its state too (from the console: not in a wsys)
ur=$($M rc '/tests/uirestart' 200 2>&1) || true
echo "$ur" | grep -E '^(ok|FAIL)'
echo "$ur" | grep -q '^uirestart: all passed' && echo "ok a ui program outlives its wsys, its count kept" || { echo "FAIL a ui program outlives its wsys"; fail=1; }
deskguest_stop

# ui across its GPU driver's restart (uilost.rc): venus (virtio-gpu-gl), wsys -g;
# the host's render node is needed (none while the card is VFIO's)
if ls /dev/dri/renderD* >/dev/null 2>&1; then
	CPU=host MEM=1024 UFS_EXTRA=$HERE/root \
	QEMU_EXTRA="-vga none -display egl-headless -device virtio-gpu-gl-pci,blob=true,venus=true,hostmem=256M -object memory-backend-memfd,id=mem1,size=1024M,share=on -machine memory-backend=mem1" \
		setsid bash "$ROOT/sys/src/build/host/guest/mcp-guest.sh" 'aux/virtiogpu' "bind -a '#i' /dev" "bind -a '#m' /dev" \
		>"$CLI_DIR/uilostguest.log" 2>&1 &
	guest=$!
	$M wait 600 >/dev/null
	lo=$($M rc 'bind /tests/ui /bin/ui; /tests/wsys -g -x -i /tests/uilost >/tmp/lost.log >[2=1] </dev/null; cat /tmp/lost.log' 300 2>&1) || true
	echo "$lo" | grep -E '^lost: ' | sed 's/^lost: //'
	echo "$lo" | grep -q '^lost: ok the GPU again' && echo "$lo" | grep -q '^lost: ok a click counts after it' &&
		! echo "$lo" | grep -q '^lost: FAIL' && echo "ok ui across its GPU driver's restart: on the GPU again" ||
		{ echo "FAIL ui across its GPU driver's restart"; fail=1; }
	kill -TERM -- -$guest 2>/dev/null || kill $guest 2>/dev/null
	wait $guest 2>/dev/null || true
else
	echo "skip ui across its GPU driver's restart: no render node on the host"
fi
exit $fail
