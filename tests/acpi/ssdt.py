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
#
# tests/acpi/run.sh evaluates \NFBU and \NFLI through aux/acpi's aml file:
# their Notify must come out of acpievents as "key brightnessup" and
# "lid open"; aux/acpi's backlight sets LEVL through _BCM, and
# aux/acpikeys (no GPU driver here) moves it on the key.
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
body = lcd + lid + nfbu + nfli
n = 36 + len(body)
hdr = struct.pack('<4sIBB6s8sI4sI', b'SSDT', n, 2, 0, b'NFRONT', b'NFEVENTS', 1, b'NFRT', 1)
t = bytearray(hdr + body)
t[9] = (-sum(t)) & 0xff
open(sys.argv[1], 'wb').write(t)
