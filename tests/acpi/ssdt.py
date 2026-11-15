#!/usr/bin/env python3
# ssdt.py out.aml: an SSDT for QEMU's -acpitable, assembled here (no iasl):
#
#	Device (\_SB.NFLC) {			a display output (a flat panel):
#		Name (_ADR, 0x0400)		brightness levels 10, 40, 70, 100
#		Name (LEVL, 40)
#		Name (_BCL, Package () { 100, 40, 10, 40, 70, 100 })
#		Method (_BCM, 1) { Store (Arg0, LEVL) }
#		Method (_BQC) { Return (LEVL) }
#	}
#	Device (\_SB.NFLD) {			a lid
#		Name (_HID, EisaId ("PNP0C0D"))
#		Method (_LID) { Return (One) }
#	}
#	Method (\NFBU) { Notify (\_SB.NFLC, 0x86) }	the brightness up key
#	Method (\NFLI) { Notify (\_SB.NFLD, 0x80) }	the lid
#	Device (\_SB.NFTP) {			a touchpad on I2C (HID over I2C)
#		Name (_HID, "NFRT0001")
#		Name (_CID, EisaId ("PNP0C50"))
#		Name (_CRS, ResourceTemplate () {
#			I2cSerialBusV2 (0x15, , 400000, , "\_SB.I2CB")
#			GpioInt (Level, ActiveLow, , , , "\_SB.GPIO") { 9 }
#		})
#		Method (_DSM, 4) { Return (0x20) }	the HID descriptor's register
#	}
#	Device (\_SB.NFBT) {			a battery with _BIX only (mW),
#		Name (_HID, EisaId ("PNP0C0A"))	discharging: 75 %, 3 h left
#		Method (_STA) { Return (0x1f) }
#		Name (_BIX, Package () { 0, 0, 50000, 40000, 1, 11550, 2000,
#			1000, 10, 100000, 0, 0, 0, 0, 100, 100, "NF", "1",
#			"LION", "NFRONT" })
#		Name (_BST, Package () { 1, 10000, 30000, 11800 })
#	}
#	Device (\_SB.NFB2) {			a battery with _BIF only (mA),
#		Name (_HID, EisaId ("PNP0C0A"))	idle: 50 %
#		Method (_STA) { Return (0x1f) }
#		Name (_BIF, Package () { 1, 5000, 4000, 1, 12000, 400, 200, 10,
#			10, "NF2", "2", "LION", "NFRONT" })
#		Name (_BST, Package () { 0, 0, 2000, 12100 })
#	}
#	Device (\_SB.NFAC) {			an AC adapter, online
#		Name (_HID, "ACPI0003")
#		Method (_PSR) { Return (One) }
#	}
#
# tests/acpi/run.sh evaluates \NFBU and \NFLI through aux/acpi's aml file:
# their Notify must come out of acpievents as "key brightnessup" and
# "lid open"; aux/acpi's backlight sets LEVL through _BCM, and
# aux/acpikeys (no GPU driver here) moves it on the key; aux/i2chid -d
# finds NFTP and its bus, address, pin (QEMU has no such bus: no more);
# aux/acpi's battery and ac files show NFBT, NFB2 and NFAC.
import struct
import sys


def pkg(body):
	"""PkgLength followed by body (lengths below 0x3f or 0xfff)"""
	n = len(body) + 1
	if n < 0x40:
		return bytes([n]) + body
	n += 1
	return bytes([0x40 | (n & 0xf), n >> 4]) + body


def name(seg):
	return seg.encode().ljust(4, b'_')


def path(*segs):
	# \ followed by a multi-name path
	return b'\\' + b'\x2f' + bytes([len(segs)]) + b''.join(name(s) for s in segs)


def eisaid(s):
	v = ((ord(s[0]) - 0x40) << 26 | (ord(s[1]) - 0x40) << 21 | (ord(s[2]) - 0x40) << 16 |
		int(s[3:], 16))
	return struct.pack('>I', v)


levels = [100, 40, 10, 40, 70, 100]
lcd = (b'\x5b\x82' + pkg(path('_SB', 'NFLC') +
	b'\x08' + name('_ADR') + b'\x0b' + struct.pack('<H', 0x400) +
	b'\x08' + name('LEVL') + b'\x0a' + bytes([40]) +
	b'\x08' + name('_BCL') + b'\x12' + pkg(bytes([len(levels)]) + b''.join(b'\x0a' + bytes([v]) for v in levels)) +
	b'\x14' + pkg(name('_BCM') + b'\x01' + b'\x70\x68' + name('LEVL')) +
	b'\x14' + pkg(name('_BQC') + b'\x00' + b'\xa4' + name('LEVL'))))
lid = (b'\x5b\x82' + pkg(path('_SB', 'NFLD') +
	b'\x08' + name('_HID') + b'\x0c' + eisaid('PNP0C0D') +
	b'\x14' + pkg(name('_LID') + b'\x00' + b'\xa4\x01')))
nfbu = b'\x14' + pkg(b'\\' + name('NFBU') + b'\x00' + b'\x86' + path('_SB', 'NFLC') + b'\x0a\x86')
nfli = b'\x14' + pkg(b'\\' + name('NFLI') + b'\x00' + b'\x86' + path('_SB', 'NFLD') + b'\x0a\x80')
def i2cbus(addr, speed, src):
	src = src.encode() + b'\0'
	d = struct.pack('<BBBBHBH', 2, 0, 1, 2, 0, 1, 6) + struct.pack('<IH', speed, addr) + src
	return b'\x8e' + struct.pack('<H', len(d)) + d


def gpioint(pin, flags, src):
	src = src.encode() + b'\0'
	d = struct.pack('<BBHHBHHHBHHH', 1, 0, 0, flags, 0, 0, 0, 23, 0, 25, 25 + len(src), 0)
	d += struct.pack('<H', pin) + src
	return b'\x8c' + struct.pack('<H', len(d)) + d


crs = i2cbus(0x15, 400000, '\\_SB.I2CB') + gpioint(9, 1 << 1, '\\_SB.GPIO') + b'\x79\x00'
tp = (b'\x5b\x82' + pkg(path('_SB', 'NFTP') +
	b'\x08' + name('_HID') + b'\x0d' + b'NFRT0001\0' +
	b'\x08' + name('_CID') + b'\x0c' + eisaid('PNP0C50') +
	b'\x08' + name('_CRS') + b'\x11' + pkg(b'\x0a' + bytes([len(crs)]) + crs) +
	b'\x14' + pkg(name('_DSM') + b'\x04' + b'\xa4\x0a\x20')))
def integer(v):
	if v == 0:
		return b'\x00'
	if v == 1:
		return b'\x01'
	if v < 0x100:
		return b'\x0a' + bytes([v])
	if v < 0x10000:
		return b'\x0b' + struct.pack('<H', v)
	return b'\x0c' + struct.pack('<I', v)


def string(t):
	return b'\x0d' + t.encode() + b'\0'


def package(*elems):
	b = b''.join(integer(e) if isinstance(e, int) else string(e) for e in elems)
	return b'\x12' + pkg(bytes([len(elems)]) + b)


sta = b'\x14' + pkg(name('_STA') + b'\x00' + b'\xa4\x0a\x1f')
bat1 = (b'\x5b\x82' + pkg(path('_SB', 'NFBT') +
	b'\x08' + name('_HID') + b'\x0c' + eisaid('PNP0C0A') + sta +
	b'\x08' + name('_BIX') + package(0, 0, 50000, 40000, 1, 11550, 2000, 1000, 10, 100000,
		0, 0, 0, 0, 100, 100, 'NF', '1', 'LION', 'NFRONT') +
	b'\x08' + name('_BST') + package(1, 10000, 30000, 11800)))
bat2 = (b'\x5b\x82' + pkg(path('_SB', 'NFB2') +
	b'\x08' + name('_HID') + b'\x0c' + eisaid('PNP0C0A') + sta +
	b'\x08' + name('_BIF') + package(1, 5000, 4000, 1, 12000, 400, 200, 10, 10, 'NF2', '2', 'LION', 'NFRONT') +
	b'\x08' + name('_BST') + package(0, 0, 2000, 12100)))
ac = (b'\x5b\x82' + pkg(path('_SB', 'NFAC') +
	b'\x08' + name('_HID') + b'\x0d' + b'ACPI0003\0' +
	b'\x14' + pkg(name('_PSR') + b'\x00' + b'\xa4\x01')))
body = lcd + lid + nfbu + nfli + tp + bat1 + bat2 + ac
n = 36 + len(body)
hdr = struct.pack('<4sIBB6s8sI4sI', b'SSDT', n, 2, 0, b'NFRONT', b'NFEVENTS', 1, b'NFRT', 1)
t = bytearray(hdr + body)
t[9] = (-sum(t)) & 0xff
open(sys.argv[1], 'wb').write(t)
