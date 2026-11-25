#!/bin/bash
# ui's start and its latency from input to pixels, measured (uilat.c) in
# QEMU: once in wsys on a VESA screen (the processor composes), once in
# wsys -g on venus (virtio-gpu-gl: the GPU composes, ui draws on the GPU).
# Each prints uilat's lines: the start (ready, first frame, its pixels
# composed) and per click the times to the client's event, to ui's frame
# and to the pixels as wsys composed them, then median and most.
# The root is sys/src/build/host/tests/uiserver/run.sh's (ui, wsys, uilat built there).
#
# usage: sys/src/build/host/tests/uiserver/measure.sh [clicks]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../../.." && pwd)
. "$ROOT/sys/src/build/lib/paths.sh"
n=${1:-20}
[ -x "$HERE/root/tests/uilat" ] && [ -x "$HERE/root/tests/ui" ] && [ -x "$HERE/root/tests/wsys" ] ||
	{ echo "measure: no uilat/ui/wsys in $HERE/root/tests: sys/src/build/host/tests/uiserver/run.sh first" >&2; exit 1; }
M="timeout 400 python3 $ROOT/sys/src/build/host/guest/mcp.py"
run() { # name wsys-flags: one guest, uilat in its wsys
	$M wait 600 >/dev/null || { echo "measure $1: no guest"; return; }
	$M rc "bind /tests/ui /bin/ui; /tests/wsys $2 -x -i '/tests/uilat -n $n' >/tmp/lat.log >[2=1] </dev/null; cat /tmp/lat.log" 300 |
		grep -E '^(start|click|latency|uilat)' | sed "s/^/$1: /"
}

# the processor composing, a VESA screen
UFS_EXTRA=$HERE/root MEM=1024 KARGS="vgasize=1024x768x32 monitor=vesa mouseport=ps2" \
	setsid bash "$ROOT/sys/src/build/host/guest/mcp-guest.sh" 'aux/realemu' 'aux/vga -m vesa -l 1024x768x32' \
	"bind -a '#i' /dev" "bind -a '#m' /dev" >"$CLI_DIR/measureguest.log" 2>&1 &
g=$!
trap 'kill -TERM -- -$g 2>/dev/null || true' EXIT
run vesa ""
kill -TERM -- -$g 2>/dev/null || true
wait $g 2>/dev/null || true

# the GPU composing: venus
CPU=host MEM=1024 UFS_EXTRA=$HERE/root \
QEMU_EXTRA="-vga none -display egl-headless -device virtio-gpu-gl-pci,blob=true,venus=true,hostmem=256M -object memory-backend-memfd,id=mem1,size=1024M,share=on -machine memory-backend=mem1" \
	setsid bash "$ROOT/sys/src/build/host/guest/mcp-guest.sh" 'aux/virtiogpu' \
	"bind -a '#i' /dev" "bind -a '#m' /dev" >"$CLI_DIR/measureguest.log" 2>&1 &
g=$!
run venus -g
