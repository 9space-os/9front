#!/bin/bash
# Make an SD card image for the StarFive VisionFive 2 (JH7110; kernel 9vf,
# sys/src/9/riscv64/socvf.c): stage 1, a boot partition only. One FAT
# partition (MBR, bootable) with boot.scr (sys/src/boot/vf), plan9.ini and
# 9vf.u; the u-boot in the board's flash finds boot.scr there (its distro
# boot: mmc 1, the sd slot) and boots the kernel to bootfs's root prompt
# on the serial console (uart0: the 40-pin header's pins 6 GND, 8 TX,
# 10 RX; 115200 8N1, 3.3 V). No root file system yet: at the prompt the
# root can come over the network (tcp) or nothing at all (the bootfs's
# rc: type !rc).
#
# usage: sys/src/build/host/hw/mksd-vf.sh [image]	(default $CLI_DIR/9space-vf2-boot.img)
#   env:	MB	image size in MB (default 128)
#	KARGS	more plan9.ini lines (name=value, separated by spaces)
# Needs the riscv64 kernel (CC=gcc ARCH=riscv64 sys/src/cmd/build/build.exe:
# riscv64/9vf) and sys/src/build/host/guest/build-boot.sh (riscv64/9vf.u,
# sys/src/boot/vf/boot.scr), mtools and sfdisk. Write it to a card with dd
# (oflag=direct conv=fsync): everything on the card is lost.
set -e
ROOT=${PLAN9:-$(git -C "$(dirname "$0")" rev-parse --show-toplevel)}
. "$ROOT/sys/src/build/lib/paths.sh"	# CLI_DIR (dist/paths.env)
OUT=${1:-$CLI_DIR/9space-vf2-boot.img}
MB=${MB:-128}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
die(){ echo "mksd-vf: $*" >&2; exit 1; }

K=$ROOT/riscv64/9vf
U=$ROOT/riscv64/9vf.u
S=$ROOT/sys/src/boot/vf/boot.scr
[ -s "$K" ] || die "no riscv64/9vf, build riscv64 first (CC=gcc ARCH=riscv64 sys/src/cmd/build/build.exe)"
for f in "$U" "$S"; do
	[ -s "$f" ] || die "no ${f#$ROOT/}, run sys/src/build/host/guest/build-boot.sh"
done
# 9vf.u is made from 9vf by build-boot.sh: an older one would boot the
# kernel of an earlier build, silently
[ ! "$K" -nt "$U" ] || die "riscv64/9vf.u is older than riscv64/9vf, run sys/src/build/host/guest/build-boot.sh"
for c in sfdisk mformat mcopy; do
	command -v $c > /dev/null || die "$c missing (util-linux, mtools)"
done

# plan9.ini: the console on uart0; no nobootprompt, so bootfs asks for the root
{
	echo console=0
	for a in $KARGS; do
		case $a in *=*) echo "$a" ;; *) die "KARGS: $a is not name=value" ;; esac
	done
} > "$TMP/plan9.ini"

rm -f "$OUT"
truncate -s ${MB}M "$OUT"
echo 'start=2048, type=c, bootable' | sfdisk -q "$OUT" > /dev/null
mformat -i "$OUT@@1M" -F -v 9SPACE :: > /dev/null
mcopy -i "$OUT@@1M" "$S" "$TMP/plan9.ini" "$U" ::
mdir -i "$OUT@@1M" -b ::
echo "plan9.ini:"; sed 's/^/	/' "$TMP/plan9.ini"
echo "ok: $OUT (kernel $(git -C "$ROOT" log -1 --format=%h), write it with dd to the card)"
