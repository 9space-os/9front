#!/bin/bash
# iso: the install/live CD (sys/src/build/host/guest/mkiso.sh: 9front's %.amd64.iso, %.386.iso
# recipe run in the guest) boots the way the release's does -- as a CD
# (9bootiso, El Torito; bootx64.efi from efiboot.fat) and as a disk or
# USB stick (hybrid MBR: mbr, pbs, 9boothyb; the ESP's efi/boot), each
# with BIOS and UEFI (OVMF) -- up to the desktop (wsys where the CD has
# it, else rio), taking the default at each of its prompts
# (sys/src/build/host/tests/iso/drive.py); then as a disk behind another system's ISO (UEFI).
# Builds the CD unless ISO names one.
. "$(dirname "$0")/../lib.sh"
# objtype=386: the 386 CD, booted on an x86-64 PC as well (BIOS and
# 64-bit UEFI both start 9pc; there is no 32-bit OVMF here)
for f in $objtype/9$KERNEL 386/9bootiso 386/efiboot.fat; do
	[ -f "$ROOT/$f" ] || { skip "iso" "no $f, run $BUILDHOW and sys/src/build/host/guest/build-boot.sh"; done_tests; exit; }
done
# the UEFI firmware: OVMF_CODE, OVMF_VARS of paths.env (lib.sh)

IMG=${ISO:-$TMPD/9front.$objtype.iso}
if [ -z "$ISO" ]; then
	check "sys/src/build/host/guest/mkiso.sh builds the CD" bash "$ROOT/sys/src/build/host/guest/mkiso.sh" "$IMG" || { done_tests; exit; }
fi
check "ISO 9660 with El Torito boot record" bash -c "file '$IMG' | grep -q 'ISO 9660.*bootable'"
# a profile's CD names what the profile left out (/sys/lib/dist/omitted,
# stage-dist.sh): not missing there; a CD without it (older) is checked whole
omitx=
xorriso -osirrox on -indev "$IMG" -extract /sys/lib/dist/omitted "$TMPD/omitted" >/dev/null 2>&1 &&
	omitx="-x $TMPD/omitted"
check "the CD holds everything mk install installs (sys/src/build/step/check-install.py)" \
	bash -c "python3 '$ROOT/sys/src/build/step/check-install.py' -i '$IMG' $omitx > '$TMPD/check-install' || { grep -v '^ok' '$TMPD/check-install' | head; false; }"
if [ "${BROWSER:-0}" = 1 ]; then
	check "the browser on the CD (BROWSER=1): servoshell, webfs-rs, browser, its licences and source offer, the CA roots" bash -c "
		for f in /${objtype:-amd64}/bin/servoshell /${objtype:-amd64}/bin/webfs-rs /rc/bin/browser \
			/sys/lib/browser/LICENSES.md /sys/lib/browser/LICENSE.MPL-2.0 /sys/lib/browser/SOURCES /sys/lib/tls/ca.pem; do
			xorriso -osirrox on -indev '$IMG' -find \$f 2>/dev/null | grep -q . || { echo missing \$f; exit 1; }
		done"
fi
check "hybrid MBR: active ESP partition" python3 -c "
import sys
m = open('$IMG', 'rb').read(512)
e = m[446:462]
sys.exit(not (m[510:512] == b'\x55\xaa' and e[0] == 0x80 and e[4] == 0xef))"

# the desktop: wsys where the CD has it (the profiles ui and full), else rio
want=rio
xorriso -indev "$IMG" -find /$objtype/bin/wsys 2>/dev/null | grep -q "/$objtype/bin/wsys" && want=wsys
for media in cd disk; do
	for fw in bios uefi; do
		if [ $fw = uefi ] && [ ! -f "$OVMF_CODE" ]; then
			skip "$media, $fw" "no OVMF ($OVMF_CODE)"
			continue
		fi
		dev=/dev/sdD0/data
		[ $media = disk ] && dev=/dev/sdC0/data
		check "boot as $media with $fw up to the desktop ($want), root $dev" bash -c \
			"OVMF_CODE='$OVMF_CODE' OVMF_VARS='$OVMF_VARS' python3 '$ROOT/sys/src/build/host/tests/iso/drive.py' '$IMG' $media $fw '$TMPD/$media-$fw' > '$TMPD/$media-$fw.out'; grep -q '^$dev\$' '$TMPD/$media-$fw.out' && grep -q '^desktop $want\$' '$TMPD/$media-$fw.out' || { tail -5 '$TMPD/$media-$fw.out'; false; }"
	done
done
# another system's ISO before it (a second stick, sdC0; the image as sdC1):
# 9boot reads plan9.ini from the disk it was loaded from, not the first
# ISO, and bootrc offers the ISO with an init as the root. UEFI only: a
# BIOS starts the first disk alone (SeaBIOS: drive 0x80), the other ISO
mkdir -p "$TMPD/other"
echo other >"$TMPD/other/README"
check "another system's ISO (no plan9.ini, no init)" xorriso -as mkisofs -quiet -V OTHER -o "$TMPD/other.iso" "$TMPD/other"
for fw in uefi; do
	if [ ! -f "$OVMF_CODE" ]; then
		skip "disk after another ISO, $fw" "no OVMF ($OVMF_CODE)"
		continue
	fi
	check "boot as disk with $fw after another system's ISO up to the desktop ($want), root /dev/sdC1/data" bash -c \
		"QEMU_EXTRA='-drive file=$TMPD/other.iso,format=raw,if=ide,index=0,snapshot=on' DISK_INDEX=1 OVMF_CODE='$OVMF_CODE' OVMF_VARS='$OVMF_VARS' python3 '$ROOT/sys/src/build/host/tests/iso/drive.py' '$IMG' disk $fw '$TMPD/other-$fw' > '$TMPD/other-$fw.out'; grep -q '^/dev/sdC1/data\$' '$TMPD/other-$fw.out' && grep -q '^desktop $want\$' '$TMPD/other-$fw.out' || { tail -5 '$TMPD/other-$fw.out'; false; }"
done
done_tests
