#!/usr/bin/env python3
"""
Find volatile loads gcc made narrower than their type: a register of a
device read as a byte or a halfword where the source reads 32 bits (gcc 16
on x86 turns `mmio[R] & 0xFF' stored into a field into a byte load even
through a volatile pointer; xHCI wants dword access). gcc does not promise
the width of a volatile access, so device registers are read through
accessors that it cannot narrow (mmio32r and friends, port/portfns.h);
this finds the places that still are not.

Each C file of a kernel manifest (sys/src/9/*/build.json; its first
configuration) is compiled as
build.exe compiles it, into a temporary directory (the tree's objects stay
as they are), with -fdump-tree-optimized-lineno. The dump gives, for each
source line, its volatile loads and their types' widths; the object's
disassembly (objdump -dl) the memory reads at that line and theirs. A line
with fewer full-width reads than volatile loads, and narrower ones, is
reported.

x86 only (the disassembly's syntax); arm64's cross gcc 11 has not been seen
narrowing.

usage: sys/src/build/host/check/find-narrow-mmio.py [-j N] [manifest ...]
	(default sys/src/9/pc64/build.json and sys/src/9/pc/build.json; exit 1 if anything was found,
	2 if the manifest's generated files are missing: no build yet)
"""
import collections, concurrent.futures, json, os, re, shutil, subprocess, sys, tempfile

ROOT = subprocess.run(['git', '-C', os.path.dirname(os.path.abspath(__file__)), 'rev-parse', '--show-toplevel'],
	capture_output=True, text=True, check=True).stdout.strip()

# widths of the types volatile loads have in the dumps
WIDTH = {
	'unsigned char': 1, 'char': 1, 'signed char': 1, 'uchar': 1, 'u8int': 1, '_Bool': 1,
	'short unsigned int': 2, 'short int': 2, 'ushort': 2, 'u16int': 2, 'short': 2,
	'unsigned int': 4, 'int': 4, 'uint': 4, 'u32int': 4, 'ulong': 4, 's32int': 4,
	'long unsigned int': 8, 'long int': 8, 'u64int': 8, 'uvlong': 8, 'vlong': 8,
	'long long unsigned int': 8, 'long long int': 8, 'uintptr': 8, 's64int': 8,
}

DECL = re.compile(r'^\s+(?:volatile\s+)?([A-Za-z_][\w ]*?)\s+(_\d+);$')
VLOAD = re.compile(r'^\s+(?:\[[^\]]*\]\s*)?(_\d+)\s*=\{v\}\s*\[([^:\]]+):(\d+):\d+\]')
FUNC = re.compile(r'^;; Function (\S+)')

REG = {}
for r in ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp'):
	REG['%r' + r] = 8; REG['%e' + r] = 4; REG['%' + r] = 2
for r in ('al', 'bl', 'cl', 'dl', 'ah', 'bh', 'ch', 'dh', 'sil', 'dil', 'bpl', 'spl'):
	REG['%' + r] = 1
for n in range(8, 16):
	REG['%%r%d' % n] = 8; REG['%%r%dd' % n] = 4; REG['%%r%dw' % n] = 2; REG['%%r%db' % n] = 1
SUFFIX = {'b': 1, 'w': 2, 'l': 4, 'q': 8}
INSN = re.compile(r'^\s*[0-9a-f]+:\s+(\S+)\s+(.*)$')
LINE = re.compile(r'^(/\S+|\.\./\S+|[\w./-]+\.[ch]):(\d+)(?: \(discriminator \d+\))?$')


# -m32 (pc, 386): long and pointers have 4 bytes
WIDTH32 = dict(WIDTH, **{'long unsigned int': 4, 'long int': 4, 'uintptr': 4})


def vloads(dump, width=WIDTH):
	"""(file, line) -> Counter(width) of the volatile loads in a dump"""
	out = collections.defaultdict(collections.Counter)
	types = {}
	for l in open(dump, errors='replace'):
		if FUNC.match(l):
			types = {}
			continue
		m = DECL.match(l)
		if m:
			types[m.group(2)] = m.group(1).strip()
			continue
		m = VLOAD.match(l)
		if m:
			w = width.get(types.get(m.group(1), ''), 0)
			if w > 1:
				out[(os.path.basename(m.group(2)), int(m.group(3)))][w] += 1
	return out


def reads(obj):
	"""(file, line) -> list of widths of the memory reads in an object"""
	dis = subprocess.run(['objdump', '-dl', '--no-show-raw-insn', obj], capture_output=True, text=True).stdout
	out = collections.defaultdict(list)
	where = None
	for l in dis.splitlines():
		m = LINE.match(l.strip())
		if m:
			where = (os.path.basename(m.group(1)), int(m.group(2)))
			continue
		m = INSN.match(l)
		if not m or where is None or '(' not in m.group(2):
			continue
		op, args = m.group(1), m.group(2).split('#')[0].strip()
		ops = re.split(r',(?![^(]*\))', args)
		mem = [i for i, o in enumerate(ops) if '(' in o]
		if not mem:
			continue
		if op.startswith(('lea', 'call', 'jmp', 'nop', 'prefetch', 'clflush')):
			continue
		if op.startswith(('mov', 'cmov')) and mem[0] == len(ops) - 1 and len(ops) > 1:
			continue	# a store
		w = 0
		mz = re.match(r'^mov[sz]([bw])[wlq]?$', op)
		if mz:
			w = SUFFIX[mz.group(1)]
		elif op[-1] in SUFFIX and op[:-1] in ('mov', 'test', 'cmp', 'and', 'or', 'add', 'sub', 'xor'):
			w = SUFFIX[op[-1]]
		else:
			for o in ops:
				o = o.strip()
				if o in REG:
					w = REG[o]
		if w:
			out[where].append(w)
	return out


def manifest(path):
	d = json.load(open(path))
	for b in (d if isinstance(d, list) else [d]):
		if 'SourceFiles' in b and 'Cflags' in b:
			return os.path.dirname(os.path.abspath(path)), b
	sys.exit('%s: no kernel manifest' % path)


def check(dir, cflags, src, tmp):
	base = os.path.join(tmp, os.path.basename(src)[:-2])
	arch = '386' if '-m32' in cflags else 'amd64'
	cmd = ['gcc', '-std=c11', '-c', '-I', ROOT + '/' + arch + '/include', '-I', ROOT + '/sys/include', '-I', '.'] + \
		cflags + ['-fdump-tree-optimized-lineno', '-dumpdir', base + '-', '-o', base + '.o', src]
	r = subprocess.run(cmd, cwd=dir, capture_output=True, text=True)
	if r.returncode != 0:
		return src, None, r.stderr.strip().splitlines()[-1:]
	dumps = [f for f in os.listdir(tmp) if f.startswith(os.path.basename(base) + '-') and f.endswith('.optimized')]
	if not dumps:
		return src, None, ['no dump']
	vl = vloads(os.path.join(tmp, dumps[0]), WIDTH32 if arch == '386' else WIDTH)
	rd = reads(base + '.o')
	bad = []
	for (f, line), want in sorted(vl.items()):
		got = rd.get((f, line), [])
		for w, n in sorted(want.items()):
			full = sum(1 for x in got if x >= w)
			narrow = sorted(x for x in got if x < w)
			if full < n and narrow:
				bad.append('%s:%d: %d volatile %d-byte load(s), %d read(s) that wide, narrower: %s' %
					(f, line, n, w, full, ' '.join(map(str, narrow))))
	return src, bad, None


def main():
	args = sys.argv[1:]
	jobs = os.cpu_count() or 4
	if args[:1] == ['-j']:
		jobs = int(args[1]); args = args[2:]
	paths = args or [ROOT + '/sys/src/9/pc64/build.json', ROOT + '/sys/src/9/pc/build.json']
	found = 0
	for p in paths:
		dir, b = manifest(p)
		for c in b.get('Pre', []):	# pc64's copies of pc's sources, as the build makes them
			if c.startswith('cp '):
				subprocess.run(c.split(), cwd=dir)
		srcs = [s for s in b['SourceFiles'] if s.endswith('.c')]
		missing = [s for s in srcs if not os.path.exists(os.path.join(dir, s))]
		if missing:
			print('%s: no build yet (%s missing)' % (p, missing[0]))
			sys.exit(2)
		tmp = tempfile.mkdtemp(prefix='narrow-')
		try:
			with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
				for src, bad, err in ex.map(lambda s: check(dir, b['Cflags'], s, tmp), srcs):
					if err:
						print('%s: not compiled: %s' % (src, ' '.join(err)))
						found = 1
					for l in bad or []:
						print(l)
						found = 1
		finally:
			shutil.rmtree(tmp)
	sys.exit(found)


if __name__ == '__main__':
	main()
