#!/usr/bin/env python3
"""
Find where the kernels drop volatile from a device register's pointer.

vmap gives device registers as volatile void* (GCC port, pc/fns.h): kencc
never moved, merged, narrowed or dropped a memory access, gcc does all of
them to plain memory. A register pointer stays volatile wherever it goes
(struct members, locals, parameters); memory mapped by vmap (ACPI and MP
tables, the BIOS, frame buffers) comes from vmapmem, plain; block copies
to a card's memory go through memmovetoio, memmovefromio, memsetio
(port/portfns.h), which drop it by name.

Each C file of the kernel manifests (every configuration's) is compiled as build.exe compiles it,
-fsyntax-only, with -Wdiscarded-qualifiers (an assignment or argument
losing volatile: the kernels' -Werror=discarded-qualifiers stops those in
the build too) and -Wcast-qual (a cast losing it, which gcc otherwise lets
by); the warnings about volatile are reported.

usage: sys/src/build/host/check/find-dropped-volatile.py [-j N] [kernel dir ...]
	(default every one of pc64 pc arm64 riscv64 the tree has, sys/src/9/$dir/build.json; exit 1 if anything
	was found, 2 if a kernel's generated files are missing: no build yet)
"""
import concurrent.futures, json, os, subprocess, sys

ROOT = subprocess.run(['git', '-C', os.path.dirname(os.path.abspath(__file__)), 'rev-parse', '--show-toplevel'],
	capture_output=True, text=True, check=True).stdout.strip()
ARCH = {'pc': '386', 'pc64': 'amd64', 'arm64': 'arm64', 'riscv64': 'riscv64'}
CC = {'arm64': os.environ.get('TOOLPREFIX', 'aarch64-linux-musl-') + 'gcc',
	'riscv64': os.environ.get('TOOLPREFIX', 'riscv64-linux-gnu-') + 'gcc'}


def main():
	args = sys.argv[1:]
	jobs = os.cpu_count() or 4
	if args[:1] == ['-j']:
		jobs = int(args[1]); args = args[2:]
	found = 0
	for kdir in args or [k for k in ARCH if os.path.isdir(os.path.join(ROOT, 'sys/src/9', k))]:
		d = os.path.join(ROOT, 'sys/src/9', kdir)
		m = json.load(open(os.path.join(d, 'build.json')))
		# every configuration of the kernel (riscv64: one per board)
		confs = [x for x in (m if isinstance(m, list) else [m]) if 'SourceFiles' in x]
		for b in confs:
			for c in b.get('Pre', []):	# pc64's copies of pc's sources, as the build makes them
				if c.startswith('cp '):
					subprocess.run(c.split(), cwd=d)
		arch = ARCH[kdir]
		cc = CC.get(arch, 'gcc')
		work = []	# (source, flags), each once
		for b in confs:
			for src in b['SourceFiles']:
				if src.endswith('.c') and (src, tuple(b['Cflags'])) not in work:
					work.append((src, tuple(b['Cflags'])))
		missing = [s for s, _ in work if not os.path.exists(os.path.join(d, s))]
		if missing:
			print('%s: no build yet (%s missing)' % (kdir, missing[0]))
			sys.exit(2)

		def one(w):
			src, cflags = w
			cmd = [cc, '-std=c11', '-fsyntax-only', '-I', ROOT + '/' + arch + '/include', '-I', ROOT + '/sys/include',
				'-I', '.'] + list(cflags) + ['-Wdiscarded-qualifiers', '-Wcast-qual', src]
			r = subprocess.run(cmd, cwd=d, capture_output=True, text=True, env=dict(os.environ, LC_ALL='C'))
			out = [l for l in r.stderr.splitlines()
				if ('discarded-qualifiers' in l or 'cast-qual' in l) and 'volatile' in l]
			if r.returncode != 0:
				out += [l for l in r.stderr.splitlines() if ' error: ' in l]
			return out

		seen = set()
		with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
			for out in ex.map(one, work):
				for l in out:
					if l in seen:
						continue
					seen.add(l)
					print('%s: %s' % (kdir, l))
					found = 1
	sys.exit(found)


if __name__ == '__main__':
	main()
