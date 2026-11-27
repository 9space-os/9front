#!/bin/bash
# Make an SD card image for the StarFive VisionFive 2 (JH7110; kernel 9vf,
# sys/src/9/riscv64/socvf.c): stage 1, a boot partition only.
# StarFive's u-boot in the board's flash (2021.10; boot switches on flash)
# looks on the sd card (mmc 1) only at partition 3: it fatloads
# vf2_uEnv.txt from there, imports it and runs boot2. So the card has
# StarFive's layout: partitions 1 and 2 empty (their loader slots), 3 the
# FAT boot partition (bootable) with vf2_uEnv.txt (boot2: plan9.ini to
# CONFADDR, 9vf.u, bootm with u-boot's device tree, as
# sys/src/boot/vf/boot.txt does), plan9.ini, 9vf.u and boot.scr
# (sys/src/boot/vf: for a u-boot that runs boot scripts, mainline's
# distro boot). The kernel comes up on the serial console (uart0: the
# 40-pin header's pins 6 GND, 8 TX, 10 RX; 115200 8N1, 3.3 V) and asks
# for the root.
#
# usage: sys/src/build/host/hw/mksd-vf.sh [image]	(default $CLI_DIR/9space-vf2-boot.img)
#   env:	MB	image size in MB (default 128)
#	KARGS	more plan9.ini lines (name=value, separated by spaces)
#	KERNEL	another u-boot image of 9vf instead of riscv64/9vf.u (a test kernel)
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
U=${KERNEL:-$ROOT/riscv64/9vf.u}
S=$ROOT/sys/src/boot/vf/boot.scr
[ -s "$S" ] || die "no sys/src/boot/vf/boot.scr, run sys/src/build/host/guest/build-boot.sh"
if [ -n "$KERNEL" ]; then
	[ -s "$U" ] || die "no $U"
else
	[ -s "$K" ] || die "no riscv64/9vf, build riscv64 first (CC=gcc ARCH=riscv64 sys/src/cmd/build/build.exe)"
	[ -s "$U" ] || die "no riscv64/9vf.u, run sys/src/build/host/guest/build-boot.sh"
	# 9vf.u is made from 9vf by build-boot.sh: an older one would boot the
	# kernel of an earlier build, silently
	[ ! "$K" -nt "$U" ] || die "riscv64/9vf.u is older than riscv64/9vf, run sys/src/build/host/guest/build-boot.sh"
fi
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
# boot2 for StarFive's u-boot, with its bootdev, devnum, bootpart (mmc 1:3):
# plan9.ini to CONFADDR (riscv64/mem.h, cleared first), the image to the
# loadaddr (not kernel_addr_r, the load address itself: bootm "overlap")
echo 'boot2=mw.b 0x40100000 0x0 0x10000; fatload ${bootdev} ${devnum}:${bootpart} 0x40100000 plan9.ini; fatload ${bootdev} ${devnum}:${bootpart} 0x60000000 9vf.u; bootm 0x60000000 - ${fdtcontroladdr}' \
	> "$TMP/vf2_uEnv.txt"
cp "$U" "$TMP/9vf.u"

rm -f "$OUT"
truncate -s ${MB}M "$OUT"
# at 1, 2 and 4 MB: 1 and 2 empty (StarFive's loader slots), 3 FAT, bootable
printf '%s\n' 'start=2048, size=2048, type=da' 'start=4096, size=4096, type=da' \
	'start=8192, type=c, bootable' | sfdisk -q "$OUT" > /dev/null
mformat -i "$OUT@@4M" -F -v 9SPACE :: > /dev/null
mcopy -i "$OUT@@4M" "$TMP/vf2_uEnv.txt" "$S" "$TMP/plan9.ini" "$TMP/9vf.u" ::
mdir -i "$OUT@@4M" -b ::
echo "plan9.ini:"; sed 's/^/	/' "$TMP/plan9.ini"
echo "ok: $OUT (kernel ${KERNEL:-riscv64/9vf.u of $(git -C "$ROOT" log -1 --format=%h)}; write it with dd to the card)"
