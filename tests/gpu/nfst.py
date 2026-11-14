#!/usr/bin/env python3
# nfst.py out.aml: an SSDT for QEMU's -acpitable, assembled here (no iasl):
#
#	Name (NFSB, Buffer (4) {0, 0, 0, 0})
#	Method (NFST, 0) {
#		Store (0x5A, Index (NFSB, One))
#		Return (NFSB)
#	}
#
# A store whose target is an Index into a named buffer changes the buffer
# itself (a ThinkBook's ATIF fills its reply ATIB so; libaml's store()
# dropped it). aux/i915 -T evaluates \NFST and wants 00 5a 00 00.
import struct
import sys

body = (b'\x08NFSB' + b'\x11\x07\x0a\x04' + bytes(4) +	# Name (NFSB, Buffer (4) {...})
	b'\x14\x15NFST\x00' +				# Method (NFST, 0), PkgLength 0x15
	b'\x70\x0a\x5a' + b'\x88NFSB\x01\x00' +		# Store (0x5A, Index (NFSB, One))
	b'\xa4NFSB')					# Return (NFSB)
n = 36 + len(body)
hdr = struct.pack('<4sIBB6s8sI4sI', b'SSDT', n, 2, 0, b'NFRONT', b'NFSTORE ', 1, b'NFRT', 1)
t = bytearray(hdr + body)
t[9] = (-sum(t)) & 0xff
open(sys.argv[1], 'wb').write(t)
