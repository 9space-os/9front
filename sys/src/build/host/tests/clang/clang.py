#!/usr/bin/env python3
"""
clang and lld on the install CD (/$objtype/clang, toolchain/clang: CLANG_DIR in
paths.env): boot to the desktop, then over the serial rc: clang --version, an
APE program (the driver's defaults: APE's headers and libraries, ld.lld), a
native Plan 9 program (/sys/src/cmd/echo.c with libc.a from /$objtype/lib.gcc)
and one with Plan 9 C's unnamed members (-fplan9-extensions), C++ with libc++
(iostream, a vector, an exception thrown and caught), the other machines'
programs (objtype=386, arm64: C and C++, ELF of that machine; stage-dist.sh puts
their /X/clang, /X/lib.gcc, /X/plan9.ld on the CD), and an object of the CD's
gcc linked into a clang program (the two share their ABI).

usage: sys/src/build/host/tests/clang/clang.py iso workdir objtype [version]
   prints one line per check: ok|fail|skip <name>
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from vm import machine

iso, work, objtype = sys.argv[1:4]
want = sys.argv[4] if len(sys.argv) > 4 else ''
vm = machine(iso, work, objtype)
result = vm.result	# ok|fail lines; a failed one keeps the work directory

def run(line, marker, secs):
	"""line on the serial rc, then echo marker; output until it shows"""
	out = vm.serial(line + "; echo %s $status" % marker, 1)
	end = time.time() + secs
	while marker not in out and time.time() < end:
		out += vm.read(2)
	return out.replace('\r', '')

def write(name, lines):
	"""a file of these lines (no single quotes in them), by echo on the serial rc"""
	op = '>'
	for l in lines:
		vm.serial("echo '%s' %s %s" % (l, op, name), 1)
		op = '>>'

try:
	if not result(vm.boot(), 'boot to rio, serial rc'):
		sys.exit(0)
	out = run('test -x /%s/clang/bin/clang && echo has-clang' % objtype, 'has-done', 10)
	if 'has-clang\n' not in out:
		print('skip no /%s/clang on the CD (mkcd -dev clang)' % objtype, flush=True)
		sys.exit(0)
	vm.serial('bind -b /%s/clang/bin /bin' % objtype, 2)
	W = vm.workdir('clang')
	out = run('clang --version', 'version-done', 60)
	result('clang version %s.' % want in out and 'plan9' in out,
		'clang --version: %s, a plan9 target' % (want or '?'))

	write('hello.c', ['#include <stdio.h>',
		'int main(void){ printf("hello from clang\\n"); return 0; }'])
	out = run('clang -O2 -o hello hello.c && ./hello', 'ape-done', 300)
	result('hello from clang\n' in out, 'APE program: compile, link (ld.lld), run')

	p9flags = '-O2 -nostdinc -I/%s/include -I/sys/include -fplan9-extensions -ffreestanding -fno-builtin' % objtype
	p9link = 'clang -nostdlib -o %%s %%s -L/%s/lib.gcc -lc `{clang --print-libgcc-file-name}' % objtype
	p9 = ('clang %s -c -o echo.o /sys/src/cmd/echo.c && %s && ./echo9 native plan9 echo'
		% (p9flags, p9link % ('echo9', 'echo.o')))
	out = run(p9, 'p9-done', 300)
	result('native plan9 echo' in out.replace(p9, ''), 'native Plan 9 program: compile, link, run')

	# Plan 9 C: a struct's unnamed member, its fields used directly, the member
	# named by its type, and a pointer to the struct passed where one to the member goes
	write('ext.c', ['#include <u.h>', '#include <libc.h>',
		'typedef struct Count Count;', 'struct Count { int n; };',
		'typedef struct Box Box;', 'struct Box { char *name; Count; };',
		'static void inc(Count *c){ c->n += 2; }',
		'void main(void){ Box b; b.name = "box"; b.n = 40; inc(&b); print("ext says %d %d\\n", b.Count.n, b.n); exits(nil); }'])
	ext = 'clang %s -c -o ext.o ext.c && %s && ./ext9' % (p9flags, p9link % ('ext9', 'ext.o'))
	out = run(ext, 'ext-done', 300)
	result('ext says 42 42' in out, 'Plan 9 C unnamed members (-fplan9-extensions)')

	write('cxx.cc', ['#include <iostream>', '#include <stdexcept>', '#include <vector>',
		'int main(){ std::vector<int> v{40, 2}; try { throw std::runtime_error("caught"); }',
		' catch(const std::exception &e){ std::cout << "clang++ says " << v[0] + v[1] << " " << e.what() << std::endl; } }'])
	out = run('clang++ -O2 -o cxx cxx.cc && ./cxx', 'cxx-done', 600)
	result('clang++ says 42 caught' in out, 'C++ with libc++: iostream, an exception, runs')

	# the other machines (stage-dist.sh: /X/clang, /X/lib.gcc, /X/plan9.ld):
	# objtype=X builds their programs, ELF of that machine (they do not run here)
	for X, mach in (('386', 3), ('arm64', 0xb7), ('amd64', 0x3e)):
		if X == objtype:
			continue
		out = run('test -f /%s/clang/lib/libc++.a && test -f /%s/plan9.ld && echo has-%s' % (X, X, X), 'has-%s-done' % X, 10)
		if ('has-%s\n' % X) not in out:
			print('skip objtype=%s: no /%s/clang or /%s/plan9.ld on the CD' % (X, X, X), flush=True)
			continue
		for src, cc in (('hello.c', 'clang'), ('cxx.cc', 'clang++')):
			b = '%s.%s' % (src.split('.')[0], X)
			out = run('objtype=%s %s -O2 -o %s %s && dd -if %s -bs 20 -count 1 >[2]/dev/null | xd -1x'
				% (X, cc, b, src, b), '%s-done' % b, 600)
			# xd: an address, then the bytes in hex; ELF's magic, e_machine at 18
			hexes = [int(t, 16) for l in out.split('\n') if l[:7].isdigit()
				for t in l.split()[1:] if len(t) == 2]
			ok = len(hexes) >= 20 and hexes[:4] == [0x7f, 0x45, 0x4c, 0x46] and hexes[18] == mach
			result(ok, 'objtype=%s %s: ELF for %s' % (X, cc, X))

	# an object of the CD's gcc in a clang program
	out = run('test -x /%s/gnu/bin/gcc && echo has-gcc' % objtype, 'gcc-done', 10)
	if 'has-gcc\n' not in out:
		print('skip gcc and clang together: no /%s/gnu on the CD' % objtype, flush=True)
	else:
		write('add.c', ['int add(int a, int b){ return a + b; }'])
		write('main.c', ['#include <stdio.h>', 'int add(int, int);',
			'int main(void){ printf("together %d\\n", add(40, 2)); return 0; }'])
		mix = '/%s/gnu/bin/gcc -O2 -c add.c && clang -O2 -o mix main.c add.o && ./mix' % objtype
		out = run(mix, 'mix-done', 300)
		result('together 42' in out, "gcc's object linked by clang, runs")
finally:
	vm.close()
