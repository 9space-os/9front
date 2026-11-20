#!/usr/bin/env python3
"""kstack.py [-e kernel.elf] [log]: resolve the kernel stacks a guest printed
with tests/gpu/kstacks.rc ("kstack PID NAME STATE" and acid's lines of
addresses) against the ELF kernel it ran (default amd64/bin/9pc64 of the
tree): each address in the kernel's text becomes "function file:line"
(addr2line), the others are left out.  Reads the log (a console file of
tests/gpu, default stdin), prints each process's frames."""
import os, re, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..'))

def textrange(elf):
	"""the kernel text's start and end: its lowest text symbol, etext (nm)"""
	lo = hi = None
	for l in subprocess.run(['nm', elf], capture_output=True, text=True).stdout.splitlines():
		f = l.split()
		if len(f) != 3:
			continue
		if f[1] in 'tT':
			a = int(f[0], 16)
			lo = a if lo is None else min(lo, a)
		if f[2] == 'etext':
			hi = int(f[0], 16)
	return lo, hi

def resolve(elf, addrs):
	if not addrs:
		return []
	out = subprocess.run(['addr2line', '-f', '-e', elf] + ['%#x' % a for a in addrs],
		capture_output=True, text=True).stdout.splitlines()
	res = []
	for i, a in enumerate(addrs):
		fn, loc = out[2*i], out[2*i+1]
		if fn != '??':
			res.append('%#x %s %s' % (a, fn, os.path.relpath(loc.split(' ')[0], ROOT)))
	return res

def main():
	args = sys.argv[1:]
	elf = os.path.join(ROOT, 'amd64/bin/9pc64')
	if args[:1] == ['-e']:
		elf = args[1]
		args = args[2:]
	text = open(args[0], errors='replace').read() if args else sys.stdin.read()
	lo, hi = textrange(elf)
	cur = None
	addrs = []
	def flush():
		if cur is not None:
			print(cur)
			for r in resolve(elf, addrs):
				print('\t' + r)
	for line in text.replace('\r', '').splitlines():
		if line.startswith('kstack '):
			flush()
			cur, addrs = line, []
		elif cur is not None and line.startswith('acid: 0x'):
			for w in re.findall(r'0x[0-9a-f]+', line):
				a = int(w, 16)
				if lo is not None and hi is not None and lo <= a < hi:
					addrs.append(a)
	flush()

if __name__ == '__main__':
	main()
