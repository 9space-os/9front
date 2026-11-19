#!/bin/bash
# Boot a real machine of the LAN from this host by PXE (tests/hw/README.md):
# the machine's UEFI asks for PXE, dnsmasq (proxy DHCP: the router keeps
# giving the addresses, dnsmasq answers this machine's PXE requests only)
# gives 9boot (bootx64.efi; a BIOS machine 9bootpxe) by TFTP, 9boot reads /cfg/pxe/<mac> (plan9.ini)
# and the kernel, and the kernel's root is served by ufs from this host
# (nobootprompt=tcp, fs=this host, fsport=), as for the QEMU guests.
#
# Host side, in $PXE_DIR ($CLI_DIR/pxe; not the QEMU guests' ufsroot):
#	tftp/		bootx64.efi, 9bootpxe.0, amd64/9pc64, cfg/pxe/<mac> (plan9.ini)
#	ufsroot/	the root (scripts/guest/gen-ufs-root.sh)
#	dnsmasq.conf	for the one command run as root (printed)
#	stick.img	or 9boot, plan9.ini and the kernel on a USB stick (BIOS and UEFI)
#
# The root also has the MCP server (MCP_DIR, tools/mcp: /amd64/bin/9front-mcp,
# /sys/lib/mcp/deny, the token in /usr/glenda/lib/mcptoken, kept in
# $PXE_DIR/mcptoken), started at boot (/rc/bin/termrc.local; MCP=0: not);
# scripts/guest/mcp.py reaches it
# (MCPURL=http://machine:8090/mcp MCPTOKEN=`cat $PXE_DIR/mcptoken`).
# And /rc/bin/termcpu (scripts/hw/termcpu.rc): the machine as its own auth
# and cpu server for drawterm; run once by hand (it asks for the keys),
# then termrc.local starts it at boot (the keys kept in $PXE_DIR/keys).
#
# usage: scripts/hw/pxe.sh mac [machine]	e.g. 80:ca:52:83:8a:6a thinkbook16g7
#   machine: tests/hw/machines/<machine>, plan9.ini lines the machine always
#   gets (e.g. *nonvme=1: its own disk is not to be touched; mouseport=,
#   monitor=, vgasize= there: not asked at boot), and lines
#   "firmware pattern [name]": the host's /usr/lib/firmware/pattern (FWDIR),
#   unpacked into the root's /lib/firmware (e.g. amdgpu/yellow_carp_*),
#   under name where the driver wants another (iwlwifi-7265-17.ucode.zst
#   iwm-7265-17: etheriwl's)
#   $PXE_DIR/wlan.conf ("SSID: name" and "PWD: passphrase" lines), if there:
#   the machine joins that WLAN at boot: a known network of wlan(8)
#   (/usr/glenda/lib/wlan), which termrc's wlan auto joins after the wired
#   network the root comes over
#   env:	IFACE	the LAN interface (default: the one of the default route)
#	HOSTIP	this host's address on it (default: IFACE's)
#	UFS_PORT	the root's port (5650)
#	PXE_DIR	the work dir (default $CLI_DIR/pxe): the root, keys, token
#		of this machine; a second machine at once gets its own
#		(e.g. PXE_DIR=$CLI_DIR/pxe-thinkbook UFS_PORT=5651), the TFTP
#		dir and dnsmasq.conf stay shared ($CLI_DIR/pxe): one dnsmasq
#		for all machines with a cfg there, each its own kernel file
#		(amd64/9pc64, another PXE_DIR's amd64/9pc64.<mac>)
#	KERNEL	the kernel to boot (default the tree's amd64/9pc64)
#	ROOTFROM	a root to start from instead of the tree's build
#		(another machine's: its programs, without its own files)
#	DNSMASQ	the dnsmasq in the printed command (default: dnsmasq)
#	PLAN9	the tree of the system to boot (default this one; e.g. a
#		worktree of another branch: ../tree-nix), built and with
#		its boot loaders (scripts/guest/build-boot.sh)
#	KARGS	more plan9.ini lines, space separated
#	QEMU	1: the same in QEMU instead (OVMF, QEMU's own DHCP and TFTP,
#		the root through guestfwd; no dnsmasq): a rehearsal; the
#		screen in $PXE_DIR/qemu.png after QEMUWAIT seconds (90);
#		QEMU_EXTRA: more QEMU options
# Stops with Ctrl-C (ufs; dnsmasq is stopped in its own terminal).
set -e
# HERE: this script's tree (the machines, termcpu, the root's generator);
# ROOT: the system's
# (PLAN9=, e.g. ../tree-nix: its kernel, loaders, root and tests/hw)
HERE=$(cd "$(dirname "$0")/../.." && pwd)
ROOT=${PLAN9:-$HERE}
. "$ROOT/scripts/lib/paths.sh"
die() { echo "pxe: $*" >&2; exit 1; }

[ $# = 1 ] || [ $# = 2 ] || die "usage: scripts/hw/pxe.sh mac [machine]"
MAC=$(echo "$1" | tr 'A-F' 'a-f')
MACHINE=
if [ $# = 2 ]; then
	MACHINE=$HERE/tests/hw/machines/$2
	[ -f "$MACHINE" ] || die "no $MACHINE"
fi
echo "$MAC" | grep -Eq '^([0-9a-f]{2}:){5}[0-9a-f]{2}$' || die "not a MAC: $1"
IFACE=${IFACE:-$(ip route show default | awk '{print $5; exit}')}
HOSTIP=${HOSTIP:-$(ip -4 -br addr show "$IFACE" | awk '{sub(/\/.*/, "", $3); print $3}')}
[ -n "$HOSTIP" ] || die "no address on $IFACE"
FSIP=$HOSTIP
[ "${QEMU:-0}" = 1 ] && FSIP=10.0.2.100	# QEMU forwards it to the host's ufs
UFS_PORT=${UFS_PORT:-5650}
UFS=${UFS:-$(command -v ufs 2>/dev/null || echo "$HOME/go/bin/ufs")}
[ -x "$UFS" ] || die "ufs missing (see scripts/guest/boot-cli.sh)"
KERNEL=${KERNEL:-$ROOT/amd64/9pc64}
for f in "$ROOT/386/bootx64.efi" "$ROOT/386/9bootpxe" "$KERNEL"; do
	[ -s "$f" ] || die "$f missing: the build and scripts/guest/build-boot.sh"
done

P=${PXE_DIR:-$CLI_DIR/pxe}
# the TFTP dir and dnsmasq's conf: one for every machine (one dnsmasq)
T=$CLI_DIR/pxe/tftp
mkdir -p "$P" "$T/amd64" "$T/cfg/pxe"
cp "$ROOT/386/bootx64.efi" "$T/bootx64.efi"
cp "$ROOT/386/9bootpxe" "$T/9bootpxe.0"	# the ROM adds the layer, .0
KNAME=9pc64
[ "$P" = "$CLI_DIR/pxe" ] || KNAME=9pc64.$(echo "$MAC" | tr -d :)
cp "$KERNEL" "$T/amd64/$KNAME.new" && mv "$T/amd64/$KNAME.new" "$T/amd64/$KNAME"
# 9boot asks for /cfg/pxe/ and the MAC in hex, without colons
{
	echo "bootfile=/amd64/$KNAME"
	echo "nobootprompt=tcp"
	echo "fs=$FSIP"
	echo "auth=$FSIP"
	echo "fsport=$UFS_PORT"
	echo "service=terminal"
	echo "user=glenda"
	# asked at boot unless the machine says (then it boots on by itself)
	for v in mouseport monitor vgasize; do
		[ -n "$MACHINE" ] && grep -q "^$v=" "$MACHINE" || echo "$v=ask"
	done
	[ -z "$MACHINE" ] || grep -v '^#' "$MACHINE" | grep -v '^$' | grep -v '^firmware '
	for a in $KARGS; do echo "$a"; done
} > "$T/cfg/pxe/$(echo "$MAC" | tr -d :)"

# the same as a USB stick, for a machine whose firmware's PXE will not do:
# 9boot and the kernel from the stick, the root still from this host over
# the wired network. Boots both ways, as 9front's disks (disk/mbr,
# disk/format -b pbs): BIOS -> 386/mbr -> the active partition's pbs ->
# 9bootfat; UEFI -> efi/boot/bootx64.efi of the same FAT
S=$P/stick.img
for f in mbr pbs 9bootfat bootx64.efi; do
	[ -s "$ROOT/386/$f" ] || die "386/$f missing: scripts/guest/build-boot.sh"
done
rm -f "$S"
truncate -s 64M "$S"
# one active FAT32 partition (LBA) of 62 MB from 1 MB
printf 'label: dos\nstart=2048, size=126976, type=c, bootable\n' | sfdisk -q "$S"
# -h: hidden sectors, where pbs finds its partition
mkfs.fat -F 32 -n 9SPACE -h 2048 --offset 2048 "$S" $((126976 / 2)) > /dev/null 2>&1 || die "mkfs.fat"
# the MBR's code (before the partition table at 446)
[ $(stat -c %s "$ROOT/386/mbr") -le 446 ] || die "386/mbr larger than 446 bytes"
dd if="$ROOT/386/mbr" of="$S" conv=notrunc status=none
# pbs into the partition's boot sector, as disk/format: its jump, then its
# code from the jump's target on; the BPB between stays mkfs.fat's
python3 - "$ROOT/386/pbs" "$S" <<'PY'
import sys
pbs = open(sys.argv[1], 'rb').read()
f = open(sys.argv[2], 'r+b'); f.seek(2048*512); b = bytearray(f.read(512))
n = pbs[1] + 2
assert pbs[0] == 0xEB and b[0] == 0xEB and n <= len(pbs) <= 510, 'pbs'
b[0:3] = pbs[0:3]; b[n:len(pbs)] = pbs[n:]
b[510:512] = b'\x55\xaa'
f.seek(2048*512); f.write(b)
PY
sed 's!^bootfile=.*!bootfile=9pc64!' "$T/cfg/pxe/$(echo "$MAC" | tr -d :)" > "$P/plan9.ini"
mcopy -i "$S@@1M" "$ROOT/386/9bootfat" ::/9bootfat
mmd -i "$S@@1M" ::/efi ::/efi/boot
mcopy -i "$S@@1M" "$ROOT/386/bootx64.efi" ::/efi/boot/bootx64.efi
mcopy -i "$S@@1M" "$P/plan9.ini" ::/plan9.ini
mcopy -i "$S@@1M" "$KERNEL" ::/9pc64

# the MCP server, its deny list and token
X=$P/extra
rm -rf "$X"
mkdir -p "$X/amd64/bin" "$X/sys/lib/mcp" "$X/usr/glenda/lib"
GO=$GO_DIR/go/bin/go
GOOS=plan9 GOARCH=amd64 CGO_ENABLED=0 GOCACHE=$P/gocache \
	"$GO" build -C "$MCP_DIR" -o "$X/amd64/bin/9front-mcp" . || die "9front-mcp did not build"
cp "$MCP_DIR/deny.example" "$X/sys/lib/mcp/deny"
# tests/hw's categories, run over MCP (rc /sys/lib/hwtest/cat/<step>.rc);
# not disk (it reads the internal disks) and not hangme
mkdir -p "$X/sys/lib/hwtest/cat" "$X/tests"
for c in "$ROOT"/tests/hw/cat/*; do
	case $c in */disk.rc|*/hangme.*) continue ;; esac
	cp "$c" "$X/sys/lib/hwtest/cat/"
done
# the programs of each test dir that wants them on the stick (hwstick:
# tests/hw's hwfreq, nix's nixtest, ...), as scripts/hw/mkstick.sh
for t in "$ROOT"/tests/*/build.json; do
	[ -z "$ROOTFROM" ] || break	# the tests come with that root
	td=$(dirname "$t")
	case $td in */guest) continue ;; esac
	[ -f "$td/hwstick" ] || continue
	(cd "$td" && env PLAN9="$ROOT" ARCH=amd64 CC=gcc "$ROOT/sys/src/cmd/build/build.exe" build.json) \
		> "$P/hwbuild.log" 2>&1 || die "$td did not build ($P/hwbuild.log)"
	[ -d "$td/root/tests" ] && cp -r "$td/root/tests/." "$X/tests/"
done
# drawterm's way in, for testing (scripts/hw/termcpu.rc)
mkdir -p "$X/rc/bin"
cp "$HERE/scripts/hw/termcpu.rc" "$X/rc/bin/termcpu"
chmod +x "$X/rc/bin/termcpu"
# its keys, once termcpu asked for them on the machine (the root's
# /usr/glenda/lib/nvram, keys): kept in $PXE_DIR/keys, the root made anew
# (gen-ufs-root.sh's rsync --delete) would lose them
mkdir -p "$P/keys" "$X/usr/glenda/lib"
for k in nvram keys; do
	[ -s "$P/ufsroot/usr/glenda/lib/$k" ] && cp -p "$P/ufsroot/usr/glenda/lib/$k" "$P/keys/$k"
	[ -s "$P/keys/$k" ] && cp -p "$P/keys/$k" "$X/usr/glenda/lib/$k"
done
# the machine's firmware (linux-firmware's, as the host has it: .zst, .xz or plain)
FWDIR=${FWDIR:-/usr/lib/firmware}
[ -z "$MACHINE" ] || grep '^firmware ' "$MACHINE" | while read -r _ pat name; do
	found=
	for z in $FWDIR/$pat; do
		[ -f "$z" ] || continue
		found=1
		d=$X/lib/firmware/$(dirname "${z#$FWDIR/}")
		b=$(basename "$z")
		b=${b%.zst}; b=${b%.xz}
		[ -z "$name" ] || { d=$X/lib/firmware; b=$name; }
		mkdir -p "$d"
		case $z in
		*.zst)	zstd -q -d -c < "$z" > "$d/$b" ;;
		*.xz)	xz -d -c < "$z" > "$d/$b" ;;
		*)	cp "$z" "$d/$b" ;;
		esac
	done
	[ -n "$found" ] || die "no firmware $FWDIR/$pat"
done
[ -s "$P/mcptoken" ] || head -c 18 /dev/urandom | base64 | tr -d '/+=' > "$P/mcptoken"
cp "$P/mcptoken" "$X/usr/glenda/lib/mcptoken"
# the MCP server started at boot (MCP=0: not): termrc reads termrc.local
# before the network is up, so it waits for ndb/dns (after ipconfig)
if [ "${MCP:-1}" = 1 ]; then
	mkdir -p "$X/rc/bin"
	cat > "$X/rc/bin/termrc.local" <<'EOF2'
# scripts/hw/pxe.sh: the MCP server, once the network is up
# (rfork s, not n: the name space stays termrc's, which binds the network later)
@{
	rfork s
	n=0
	while(! test -e /net/dns && test $n -lt 120){
		sleep 1
		n=`{echo $n+1 | hoc}
	}
	# drawterm's way in, once termcpu has the keys (asked the first time)
	if(test -s /usr/glenda/lib/nvram && test -s /usr/glenda/lib/keys)
		termcpu >/tmp/termcpu.log >[2=1]
	9front-mcp -http 'tcp!*!8090' -tokenfile /usr/glenda/lib/mcptoken -deny /sys/lib/mcp/deny -dir /usr/glenda >[2]/tmp/mcp.log
} </dev/null >/dev/null >[2]/dev/null &
EOF2
fi
# the WLAN of wlan.conf: a known network of wlan(8) (/usr/glenda/lib/wlan,
# essid<tab>passphrase), joined by termrc's wlan auto after the wired network
# (the root's: its subnet route stays the first)
if [ -s "$P/wlan.conf" ]; then
	ssid=$(sed -n 's/^SSID: *//p' "$P/wlan.conf")
	pwd=$(sed -n 's/^PWD: *//p' "$P/wlan.conf")
	[ -n "$ssid" ] && [ -n "$pwd" ] || die "$P/wlan.conf: no SSID: or PWD: line"
	printf '%s\t%s\n' "$ssid" "$pwd" > "$X/usr/glenda/lib/wlan"
	chmod 600 "$X/usr/glenda/lib/wlan"	# glenda's: ufs -user=glenda below
fi

echo "pxe: the root in $P/ufsroot"
if [ -n "$ROOTFROM" ]; then
	# another machine's root, without what is that machine's own
	[ -d "$ROOTFROM/amd64/bin" ] || die "ROOTFROM $ROOTFROM: not a root"
	mkdir -p "$P/ufsroot"
	rsync -a --delete \
		--exclude=/usr/glenda/lib/keys --exclude=/usr/glenda/lib/nvram \
		--exclude=/usr/glenda/lib/mcptoken --exclude=/usr/glenda/lib/wlan \
		--exclude=/usr/glenda/lib/webcookies --exclude=/usr/glenda/lib/L.webcookies \
		--exclude=/usr/glenda/soak --exclude=/usr/glenda/tmp --exclude=/adm/keyseed \
		--exclude=/rc/bin/termrc.local --exclude='*.prev' \
		"$ROOTFROM/" "$P/ufsroot/"
	mkdir -p "$P/ufsroot/usr/glenda/tmp"
	rsync -a "$X/" "$P/ufsroot/"
else
	# this tree's generator (with extra dirs), the system's files
	PLAN9="$ROOT" bash "$HERE/scripts/guest/gen-ufs-root.sh" "$P/ufsroot" "$X" > /dev/null
fi

# dnsmasq: every machine with a cfg in the TFTP dir
macs=$(ls "$T/cfg/pxe" | grep -E '^[0-9a-f]{12}$' | sed 's/../&:/g; s/:$//')
D=$CLI_DIR/pxe/dnsmasq.conf

{
cat <<EOF
# scripts/hw/pxe.sh: proxy DHCP and TFTP for the machines of $T/cfg/pxe, no DNS
port=0
interface=$IFACE
bind-interfaces
user=$(id -un)
dhcp-range=$HOSTIP,proxy
EOF
for m in $macs; do echo "dhcp-host=$m,set:9space"; done
cat <<EOF
dhcp-match=set:efi64,option:client-arch,7
dhcp-match=set:efi64,option:client-arch,9
# dnsmasq offers only the services of the client's arch; without one the
# ROM stops ("bad or missing multicast discovery address"): x86-64 UEFI
# (RFC 4578: 7 and 9) and BIOS (0, x86PC: a machine in legacy mode, as the
# X1 Carbon came; 9front's 9bootpxe, which reads the same /cfg/pxe/<mac>)
pxe-service=tag:9space,tag:efi64,X86-64_EFI,"9space",bootx64.efi
pxe-service=tag:9space,tag:efi64,BC_EFI,"9space",bootx64.efi
pxe-service=tag:9space,x86PC,"9space",9bootpxe
enable-tftp
tftp-root=$T
log-dhcp
log-facility=-
EOF
} > "$D"

if [ "${QEMU:-0}" = 1 ]; then
	"$UFS" -root="$P/ufsroot" -user=glenda -addr="127.0.0.1:$UFS_PORT" > "$P/ufs.log" 2>&1 &
	ufspid=$!
	trap 'kill $ufspid $qpid 2>/dev/null' EXIT
	cp /usr/share/edk2/x64/OVMF_VARS.4m.fd "$P/vars.fd"
	sock=$(mktemp -u /tmp/pxe.XXXXXX.sock)
	qemu-system-x86_64 -m 2048 -accel kvm -cpu qemu64,+rdrand -display none -no-reboot \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
		-drive if=pflash,format=raw,file="$P/vars.fd" \
		-netdev "user,id=n0,tftp=$T,bootfile=bootx64.efi,guestfwd=tcp:$FSIP:$UFS_PORT-tcp:127.0.0.1:$UFS_PORT" \
		-device "e1000,netdev=n0,mac=$MAC,bootindex=0" \
		-serial file:"$P/serial.log" -qmp unix:$sock,server,nowait $QEMU_EXTRA > "$P/qemu.log" 2>&1 &
	qpid=$!
	sleep ${QEMUWAIT:-90}
	python3 - "$sock" "$P/qemu.ppm" <<'PY'
import json, socket, sys
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); f = s.makefile('rw'); f.readline()
for c in ({'execute': 'qmp_capabilities'}, {'execute': 'screendump', 'arguments': {'filename': sys.argv[2]}}):
	f.write(json.dumps(c) + '\n'); f.flush(); f.readline()
PY
	rm -f "$sock"
	python3 -c "from PIL import Image; Image.open('$P/qemu.ppm').save('$P/qemu.png')"
	echo "pxe: the screen in $P/qemu.png"
	exit 0
fi

# dnsmasq up to 2.93 (fixed after it: its commit cdbfb97, "Fix logic error
# in TFTP caching") may send a transfer's first block from the transfer
# before when the new one gets its memory: 9bootpxe (BIOS), which asks for
# the kernel right after plan9.ini and without TFTP options, then reads
# plan9.ini's text as the kernel ("bad magic"). UEFI's TFTP asks with
# options (the OACK clears that cache) and is not hit.
# DNSMASQ: the dnsmasq to run, e.g. one built from its git (2.93-38 and
# later have the fix; git describe's 2.93-N-g...)
DNSMASQ=${DNSMASQ:-dnsmasq}
dv=$("$DNSMASQ" --version 2>/dev/null | sed -n '1s/^Dnsmasq version \([0-9.]*\)\(-\([0-9]*\)\)\{0,1\}.*/\1 \3/ip')
read -r dvr dvn <<< "$dv"
if [ -n "$dvr" ] && [ "$(printf '%s\n2.93\n' "$dvr" | sort -V | tail -1)" = 2.93 ] \
&& ! { [ "$dvr" = 2.93 ] && [ "${dvn:-0}" -ge 38 ]; }; then
	echo "pxe: dnsmasq $dvr${dvn:+-$dvn}: BIOS machines (9bootpxe) may get plan9.ini as the kernel's first block" >&2
	echo "pxe: (dnsmasq's TFTP cache, fixed after 2.93: cdbfb97; DNSMASQ= one with it); UEFI machines are fine" >&2
fi
cat <<EOF
pxe: run in another terminal (as root: ports 67, 69, 4011):
	sudo $DNSMASQ --no-daemon --conf-file=$D
(one dnsmasq for $(echo $macs): restart it when a machine is new)
then let $MAC boot from the network (UEFI IPv4).
pxe: or boot it from a USB stick (wired network still needed):
	sudo dd if=$S of=/dev/sdX bs=4M conv=fsync status=progress
pxe: ufs serves the root on $HOSTIP:$UFS_PORT (Ctrl-C ends it)
EOF
# the root's files glenda's, the machine's user (ufs' default owner is harvey)
exec "$UFS" -root="$P/ufsroot" -user=glenda -addr="$HOSTIP:$UFS_PORT"
