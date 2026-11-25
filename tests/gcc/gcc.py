#!/usr/bin/env python3
"""
The native gcc on the install CD (/$objtype/gnu, toolchain/gcc: GCC_DIR in
paths.env): boot to the desktop, then over the serial rc: gcc runs, compiles and links a POSIX program
over APE, and a native Plan 9 program (/sys/src/cmd/echo.c with libc.a
from /$objtype/lib.gcc); both run; gccmk builds hoc from its mkfile.

usage: tests/gcc/gcc.py iso workdir objtype
   prints one line per check: ok|fail <name>
"""
import os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../scripts/lib'))
from vm import machine
import paths

def gccversion():
	"""the version toolchain/gcc builds (its sources.txt: gcc-X.Y.Z)"""
	f = os.path.join(paths.get('GCC_DIR'), 'sources.txt')
	m = re.search(r'gcc-(\d+\.\d+\.\d+)', open(f).read())
	if m is None:
		sys.exit('gcc.py: no gcc version in %s' % f)
	return m.group(1)

iso, work, objtype = sys.argv[1:4]
vm = machine(iso, work, objtype)
result = vm.result	# ok|fail lines; a failed one keeps the work directory

def run(line, marker, secs):
	"""line on the serial rc, then echo marker; output until it shows"""
	out = vm.serial(line + "; echo %s $status" % marker, 1)
	end = time.time() + secs
	while marker not in out and time.time() < end:
		out += vm.read(2)
	return out

try:
	if not result(vm.boot(), 'boot to rio, serial rc'):
		sys.exit(0)
	vm.serial('bind -b /%s/gnu/bin /bin' % objtype, 2)
	W = vm.workdir('gcc')
	out = run('gcc --version', 'gcc-version-done', 60)
	want = gccversion()
	result(want in out, 'gcc --version is %s (GCC_DIR)' % want)

	vm.serial("echo '#include <stdio.h>' > hello.c", 1)
	vm.serial("echo 'int main(void){ printf(\"hello from gcc\\n\"); return 0; }' >> hello.c", 1)
	out = run('gcc -O2 -o hello hello.c && ./hello', 'ape-done', 300)
	result('hello from gcc' in out, 'APE program: compile, link, run')

	p9 = ('gcc -O2 -nostdinc -I/%s/include -I/sys/include -fplan9-extensions -fno-builtin'
		' -c -o echo.o /sys/src/cmd/echo.c && gcc -nostdlib -o echo9 echo.o'
		' -L/%s/lib.gcc -lc -lgcc && ./echo9 native plan9 echo') % (objtype, objtype)
	out = run(p9, 'p9-done', 300)
	result('native plan9 echo' in out.replace(p9, ''), 'native Plan 9 program: compile, link, run')

	# 9front's mkfile with gcc (the CD's /sys/src is read-only: a copy)
	O = {'amd64': '6', '386': '8'}[objtype]
	mk = ("mkdir %s/hoc && dircp /sys/src/cmd/hoc %s/hoc && cd %s/hoc && gccmk >%s/hoc.log"
		" && echo '6*7' | ./%s.out | sed 's/^/hoc says /'") % (W, W, W, W, O)
	out = run(mk, 'mk-done', 600)
	result('hoc says 42' in out, 'gccmk: hoc from its mkfile, runs')
finally:
	vm.close()
