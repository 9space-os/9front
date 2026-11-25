#!/usr/bin/env python3
"""
find-promotions.py [dir...]: where a uchar or ushort, shifted left, comes to a
64-bit type through an int: kencc promotes uchar and ushort to an unsigned
int, gcc (as ISO C) to an int, so that b<<24 (a ushort's <<16) with the top
bit set is negative in gcc and sign-extends into a vlong, uvlong or uintptr,
where 9front's compilers zero-extend it.  (b[0]<<24)|(b[1]<<16)... into a vlong
is the usual case; into a 32-bit type, or a long (32 bits in 9front: negative
there as well), nothing differs.  Shifted on into the upper half (GBIT64's
(vlong)GBIT32(p+4)<<32) the sign bits go: such a place is reported, harmless.

clang-query's AST matcher on every C file of a build (amd64): the files and
their directories from the objects' .d files (gcc -MMD; APE's by their headers),
clang with -fms-extensions for Plan 9's unnamed members (a file clang cannot
parse is reported, not matched).  Prints file:line:col and the expression's
line, one per place; exit 1 if there are any.

usage: sys/src/build/host/check/find-promotions.py [sys/src/9 sys/src/libc ...]   (default sys/src)
"""
import json, os, re, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
# 64 bits where 9front has them too, or unsigned: a long is 32 bits in 9front,
# where kencc's 0x80000000 is negative as gcc's sign-extended int is
WIDE = 'anyOf(asString("unsigned long"), asString("long long"), asString("unsigned long long"))'
def lit(lo):	# a shift by lo..31: the top bit can be reached
	return 'integerLiteral(anyOf(%s))' % ', '.join('equals(%d)' % n for n in range(lo, 32))
def small(t):
	return 'ignoringParenImpCasts(expr(hasType(qualType(hasCanonicalType(asString("%s"))))))' % t
SHIFT = ('binaryOperator(hasOperatorName("<<"), anyOf(allOf(hasLHS(%s), hasRHS(%s)), allOf(hasLHS(%s), hasRHS(%s))))'
	% (small('unsigned char'), lit(24), small('unsigned short'), lit(16)))
# not where the 64 bits are then shifted left by 32 or more: the sign bits go
UP = 'binaryOperator(hasOperatorName("<<"), hasRHS(integerLiteral(anyOf(%s))))' % ', '.join('equals(%d)' % n for n in range(32, 64))
MATCH = ('castExpr(hasType(qualType(hasCanonicalType(%s))), hasSourceExpression(ignoringParens(expr('
	'hasType(qualType(hasCanonicalType(asString("int")))), anyOf(%s, hasDescendant(%s))))), '
	'unless(anyOf(hasParent(%s), hasParent(parenExpr(hasParent(%s))))))' % (WIDE, SHIFT, SHIFT, UP, UP))

def sources(dirs):
	"""(directory, source, ape) of each object a build left (its .d)"""
	out = []
	for d in dirs:
		for dp, _, fs in os.walk(os.path.join(ROOT, d)):
			for f in fs:
				if not f.endswith('.d'):
					continue
				try:
					t = open(os.path.join(dp, f), errors='replace').read()
				except OSError:
					continue
				deps = t.replace('\\\n', ' ').split(':', 1)[-1].split()
				if not deps or not deps[0].endswith('.c'):
					continue
				ape = any('/include/ape/' in x for x in deps)
				out.append((dp, deps[0], ape))
	return out

def main():
	dirs = sys.argv[1:] or ['sys/src']
	srcs = sources(dirs)
	seen, cmds = set(), []
	for dp, src, ape in srcs:
		path = os.path.normpath(os.path.join(dp, src))
		if path in seen or not os.path.exists(path):
			continue
		seen.add(path)
		inc = ['-I', ROOT + '/amd64/include/ape', '-I', ROOT + '/sys/include/ape'] if ape else \
			['-I', ROOT + '/amd64/include', '-I', ROOT + '/sys/include']
		args = ['clang', '-std=gnu17', '-fms-extensions', '-ffreestanding', '-nostdinc', '-w',
			'-D__plan9__', '-D_POSIX_SOURCE', '-D_PLAN9_SOURCE', '-D_BSD_EXTENSION', '-D_C99_SNPRINTF_EXTENSION',
			'-I', '.'] + inc + ['-c', path]
		cmds.append({'directory': dp, 'file': path, 'arguments': args})
	tmp = tempfile.mkdtemp()
	json.dump(cmds, open(os.path.join(tmp, 'compile_commands.json'), 'w'))
	q = os.path.join(tmp, 'q')
	open(q, 'w').write('set output diag\nmatch ' + MATCH + '\n')
	places = set()
	for i in range(0, len(cmds), 200):
		files = [c['file'] for c in cmds[i:i+200]]
		r = subprocess.run(['clang-query', '-p', tmp, '-f', q] + files, capture_output=True, text=True, errors='replace')
		for l in r.stdout.splitlines():
			m = re.match(r'^(/\S+?):(\d+):(\d+): note: "root" binds here', l)
			if m:
				places.add((os.path.relpath(m.group(1), ROOT), int(m.group(2)), int(m.group(3))))
	for f, line, col in sorted(places):
		try:
			text = open(os.path.join(ROOT, f), errors='replace').read().split('\n')[line-1].strip()
		except (OSError, IndexError):
			text = ''
		print('%s:%d:%d: %s' % (f, line, col, text))
	print('%d places in %d files (%d C files looked at)' % (len(places), len({p[0] for p in places}), len(cmds)), file=sys.stderr)
	sys.exit(1 if places else 0)

if __name__ == '__main__':
	main()
