#!/bin/bash
# Build the u-boot firmware 9front's arm64 QEMU image boots from
# (sys/lib/dist/mkfile, %.arm64.qcow2: boot.scr, plan9.ini and 9qemu.u
# on the FAT partition; u-boot runs boot.scr, sys/src/boot/qemu/boot.txt).
# u-boot is not part of 9front: the official release, an archive of the
# distribution (DISTFILES, its sha256 in dist/distfiles.list; dist/bin/fetch
# gets it from ftp.denx.de), is built for qemu_arm64 with the port's arm64
# cross compiler.
#
# usage: sys/src/build/host/guest/build-uboot.sh	-> $CLI_DIR/uboot/u-boot.bin
#   env:  UBOOT	release (default 2026.07)
#         CROSS	cross compiler prefix (default aarch64-linux-musl-)
#   needs: make, gcc, bison, flex, python3 (host side of u-boot's build)
set -e
ROOT=${PLAN9:-$(git -C "$(dirname "$0")" rev-parse --show-toplevel)}
. "$ROOT/sys/src/build/lib/paths.sh"	# CLI_DIR, DISTFILES, DIST (the distribution's paths.env)
UBOOT=${UBOOT:-2026.07}
CROSS=${CROSS:-aarch64-linux-musl-}
D=$CLI_DIR/uboot
OUT=$D/u-boot.bin
if [ -s "$OUT" ] && [ "$(cat "$D/version" 2>/dev/null)" = "$UBOOT" ]; then
	echo "ok: $OUT (u-boot $UBOOT)"
	exit 0
fi
# the archive as dist/distfiles.list names it, checked by its sha256
A=u-boot-$UBOOT.tar.bz2
T=$DISTFILES/$A
SUM=$(awk -v n="$A" '$1 == n { print $2 }' "$DIST/distfiles.list")
[ -n "$SUM" ] || { echo "build-uboot: $A is not in $DIST/distfiles.list" >&2; exit 1; }
[ -s "$T" ] || { echo "build-uboot: no $T (dist/bin/fetch)" >&2; exit 1; }
echo "$SUM  $T" | sha256sum -c --quiet - || { echo "build-uboot: $T: wrong sha256" >&2; exit 1; }
mkdir -p "$D/src"
rm -rf "$D/src/u-boot-$UBOOT"
tar -xjf "$T" -C "$D/src"
cd "$D/src/u-boot-$UBOOT"
make CROSS_COMPILE=$CROSS qemu_arm64_defconfig > "$D/build.log" 2>&1
make CROSS_COMPILE=$CROSS -j"$(nproc)" >> "$D/build.log" 2>&1 \
	|| { tail -20 "$D/build.log"; exit 1; }
cp u-boot.bin "$OUT"
echo "$UBOOT" > "$D/version"
echo "ok: $OUT (u-boot $UBOOT)"
