#!/bin/sh
# linux-rtw89-dump.sh: what Linux's rtw89 does with a Realtek RTL8852CE
# (10ec:c852), to compare with 9space's aux/rtw89 (rtw89(8)).  Run as root
# from a Linux live system (a USB stick); it reads, it does not write a
# disk: it mounts no file system but debugfs, touches no block device, writes only its report
# (a file in the current directory, or $OUT).
#
# It records: the system (kernel, command line, IOMMU), the card and the
# bridge above it (lspci, configuration space), the rtw89 driver loaded
# again with its firmware debug messages (debug_mask=0x1000), and the
# card's registers after the firmware download (BAR2 by
# /sys/bus/pci/devices/*/resource2, read only, python3; under lockdown by
# rtw89's debugfs, read_reg, mounting debugfs if it is not).
#
# usage: sudo sh linux-rtw89-dump.sh	(then once more after booting
#        with amd_iommu=off on the kernel's command line)
set -u
if [ "$(id -u)" != 0 ]; then
	echo "linux-rtw89-dump: run as root: sudo sh $0" >&2
	exit 1
fi
OUT=${OUT:-rtw89-$(uname -r)-$(date +%Y%m%d-%H%M%S)$(grep -q 'iommu=off' /proc/cmdline && echo -nommu).txt}
exec >"$OUT" 2>&1

BDF=$(lspci -D -n -d 10ec:c852 | awk 'NR==1 {print $1}')
section(){ echo; echo "=== $*"; }

section system
uname -a
cat /proc/cmdline
grep -m1 'model name' /proc/cpuinfo
free -m

section iommu
ls /sys/class/iommu 2>/dev/null
dmesg | grep -iE 'AMD-Vi|iommu|DMAR' | head -40
[ -n "$BDF" ] && readlink /sys/bus/pci/devices/$BDF/iommu_group

if [ -z "$BDF" ]; then
	section error
	echo "no 10ec:c852 device"
	exit 1
fi

section card $BDF
lspci -vvv -nn -s "$BDF"
lspci -xxxx -s "$BDF"
BRIDGE=$(basename "$(dirname "$(readlink -f /sys/bus/pci/devices/$BDF)")")
section bridge $BRIDGE
lspci -vvv -nn -s "$BRIDGE"
lspci -tv

section driver
modinfo rtw89_8852ce 2>/dev/null | head -20
ls -l /lib/firmware/rtw89/rtw8852c* 2>/dev/null

section reload with debug_mask=0x1000
dmesg -n 8 2>/dev/null
for m in rtw89_8852ce rtw89_8852c rtw89_pci rtw89_core; do modprobe -r $m 2>/dev/null; done
sleep 1
MARK="rtw89-dump-$(date +%s)"
echo "$MARK" >/dev/kmsg
modprobe rtw89_core debug_mask=0x1000
modprobe rtw89_pci
modprobe rtw89_8852ce
sleep 5
dmesg | sed -n "/$MARK/,\$p" | grep -iE 'rtw89|firmware|MSS|fwdl|section' | head -200

section registers after the download
grep -q debugfs /proc/mounts || mount -t debugfs none /sys/kernel/debug 2>/dev/null
python3 - "$BDF" <<'EOF'
import mmap, os, struct, sys
bdf = sys.argv[1]
regs = [
	(0x0004, "SYS_PW_CTRL"), (0x0008, "SYS_CLK_CTRL"), (0x0088, "PLATFORM_ENABLE"),
	(0x0160, "HALT_H2C_CTRL"), (0x01E0, "WCPU_FW_CTRL"), (0x03F0, "IC_PWR_STATE"),
	(0x1000, "HAXI_INIT_CFG1"), (0x1010, "HAXI_DMA_STOP1"), (0x101C, "HAXI_DMA_BUSY1"),
	(0x1038, "CH12_TXBD_NUM"), (0x1080, "CH12_TXBD_IDX"), (0x1160, "CH12_TXBD_DESA_L"),
	(0x1280, "CH12_TXBD_DESA_L_V1"), (0x1284, "CH12_TXBD_DESA_H_V1"), (0x1204, "EXP_CTRL"),
	(0x8380, "HCI_FUNC_EN"), (0x83F0, "BOOT_DBG"), (0x8400, "DMAC_FUNC_EN"),
	(0x8404, "DMAC_CLK_EN"), (0x8410, "LTR_CTRL_0"),
]
def bar():
	fd = os.open("/sys/bus/pci/devices/%s/resource2" % bdf, os.O_RDONLY)
	m = mmap.mmap(fd, 0x10000, mmap.MAP_SHARED, mmap.PROT_READ)
	return lambda off: struct.unpack_from("<I", m, off)[0]

def debugfs():
	# rtw89's debugfs: write "addr len" to read_reg, read the value back
	import glob
	d = glob.glob("/sys/kernel/debug/ieee80211/phy*/rtw89")
	if not d:
		raise OSError("no rtw89 debugfs")
	def rd(off):
		open(d[0] + "/read_reg", "w").write("%#x 4" % off)
		t = open(d[0] + "/read_reg").read()
		return int(t.split()[-1], 16)
	return rd

for how in (bar, debugfs):
	try:
		rd = how()
		print("by", how.__name__)
		for off, name in regs:
			print("%#06x %-22s %08x" % (off, name, rd(off)))
		break
	except Exception as e:
		print(how.__name__, "failed:", e)
EOF

section interface
ip -br link 2>/dev/null | grep -i wl
echo
echo "report: $OUT" >&2
