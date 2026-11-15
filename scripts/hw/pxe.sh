#!/bin/bash
# Boot a real machine of the LAN from this host by PXE (tests/hw/README.md):
# the machine's UEFI asks for PXE, dnsmasq (proxy DHCP: the router keeps
# giving the addresses, dnsmasq answers this machine's PXE requests only)
# gives 9boot (bootx64.efi) by TFTP, 9boot reads /cfg/pxe/<mac> (plan9.ini)
# and the kernel, and the kernel's root is served by ufs from this host
# (nobootprompt=tcp, fs=this host, fsport=), as for the QEMU guests.
#
# Host side, in $PXE_DIR ($CLI_DIR/pxe; not the QEMU guests' ufsroot):
#	tftp/		bootx64.efi, amd64/9pc64, cfg/pxe/<mac> (plan9.ini)
#	ufsroot/	the root (scripts/guest/gen-ufs-root.sh)
#	dnsmasq.conf	for the one command run as root (printed)
#
# The root also has the MCP server (MCP_DIR, tools/mcp: /amd64/bin/9front-mcp,
# /sys/lib/mcp/deny, the token in /usr/glenda/lib/mcptoken, kept in
# $PXE_DIR/mcptoken), started at boot (/rc/bin/termrc.local; MCP=0: not);
# scripts/guest/mcp.py reaches it
# (MCPURL=http://machine:8090/mcp MCPTOKEN=`cat $PXE_DIR/mcptoken`).
#
# usage: scripts/hw/pxe.sh mac [machine]	e.g. 80:ca:52:83:8a:6a thinkbook16g7
#   machine: tests/hw/machines/<machine>, plan9.ini lines the machine always
#   gets (e.g. *nonvme=1: its own disk is not to be touched; mouseport=,
#   monitor=, vgasize= there: not asked at boot), and lines
#   "firmware pattern": the host's /usr/lib/firmware/pattern (FWDIR),
#   unpacked into the root's /lib/firmware (e.g. amdgpu/yellow_carp_*)
#   env:	IFACE	the LAN interface (default: the one of the default route)
#	HOSTIP	this host's address on it (default: IFACE's)
#	UFS_PORT	the root's port (5650)
#	PXE_DIR	the work dir (default $CLI_DIR/pxe)
#	KARGS	more plan9.ini lines, space separated
#	QEMU	1: the same in QEMU instead (OVMF, QEMU's own DHCP and TFTP,
#		the root through guestfwd; no dnsmasq): a rehearsal; the
#		screen in $PXE_DIR/qemu.png after QEMUWAIT seconds (90);
#		QEMU_EXTRA: more QEMU options
# Stops with Ctrl-C (ufs; dnsmasq is stopped in its own terminal).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$ROOT/scripts/lib/paths.sh"
die() { echo "pxe: $*" >&2; exit 1; }

[ $# = 1 ] || [ $# = 2 ] || die "usage: scripts/hw/pxe.sh mac [machine]"
MAC=$(echo "$1" | tr 'A-F' 'a-f')
MACHINE=
if [ $# = 2 ]; then
	MACHINE=$ROOT/tests/hw/machines/$2
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
KERNEL=$ROOT/amd64/9pc64
for f in "$ROOT/386/bootx64.efi" "$KERNEL"; do
	[ -s "$f" ] || die "$f missing: the build and scripts/guest/build-boot.sh"
done

P=${PXE_DIR:-$CLI_DIR/pxe}
T=$P/tftp
mkdir -p "$T/amd64" "$T/cfg/pxe"
cp "$ROOT/386/bootx64.efi" "$T/bootx64.efi"
cp "$KERNEL" "$T/amd64/9pc64"
# 9boot asks for /cfg/pxe/ and the MAC in hex, without colons
{
	echo "bootfile=/amd64/9pc64"
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

# the MCP server, its deny list and token
X=$P/extra
rm -rf "$X"
mkdir -p "$X/amd64/bin" "$X/sys/lib/mcp" "$X/usr/glenda/lib"
GO=$GO_DIR/go/bin/go
GOOS=plan9 GOARCH=amd64 CGO_ENABLED=0 GOCACHE=$P/gocache \
	"$GO" build -C "$MCP_DIR" -o "$X/amd64/bin/9front-mcp" . || die "9front-mcp did not build"
cp "$MCP_DIR/deny.example" "$X/sys/lib/mcp/deny"
# the machine's firmware (linux-firmware's, as the host has it: .zst, .xz or plain)
FWDIR=${FWDIR:-/usr/lib/firmware}
[ -z "$MACHINE" ] || grep '^firmware ' "$MACHINE" | while read -r _ pat; do
	found=
	for z in $FWDIR/$pat; do
		[ -f "$z" ] || continue
		found=1
		d=$X/lib/firmware/$(dirname "${z#$FWDIR/}")
		b=$(basename "$z")
		mkdir -p "$d"
		case $z in
		*.zst)	zstd -q -d -c < "$z" > "$d/${b%.zst}" ;;
		*.xz)	xz -d -c < "$z" > "$d/${b%.xz}" ;;
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
@{
	rfork ns
	n=0
	while(! test -e /net/dns && test $n -lt 120){
		sleep 1
		n=`{echo $n+1 | hoc}
	}
	9front-mcp -http 'tcp!*!8090' -tokenfile /usr/glenda/lib/mcptoken -deny /sys/lib/mcp/deny -dir /usr/glenda >[2]/tmp/mcp.log
} </dev/null >/dev/null >[2]/dev/null &
EOF2
fi

echo "pxe: the root in $P/ufsroot"
bash "$ROOT/scripts/guest/gen-ufs-root.sh" "$P/ufsroot" "$X" > /dev/null

cat > "$P/dnsmasq.conf" <<EOF
# scripts/hw/pxe.sh: proxy DHCP and TFTP for $MAC only, no DNS
port=0
interface=$IFACE
bind-interfaces
user=$(id -un)
dhcp-range=$HOSTIP,proxy
dhcp-host=$MAC,set:nfront
dhcp-match=set:efi64,option:client-arch,7
dhcp-match=set:efi64,option:client-arch,9
pxe-service=tag:nfront,tag:efi64,X86-64_EFI,"nfront",bootx64.efi
enable-tftp
tftp-root=$T
log-dhcp
log-facility=-
EOF

if [ "${QEMU:-0}" = 1 ]; then
	"$UFS" -root="$P/ufsroot" -addr="127.0.0.1:$UFS_PORT" > "$P/ufs.log" 2>&1 &
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

cat <<EOF
pxe: run in another terminal (as root: ports 67, 69, 4011):
	sudo dnsmasq --no-daemon --conf-file=$P/dnsmasq.conf
then let $MAC boot from the network (UEFI IPv4).
pxe: ufs serves the root on $HOSTIP:$UFS_PORT (Ctrl-C ends it)
EOF
exec "$UFS" -root="$P/ufsroot" -addr="$HOSTIP:$UFS_PORT"
