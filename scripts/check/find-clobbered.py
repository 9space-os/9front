#!/usr/bin/env python3
"""
find-clobbered.py: locals a jump back to waserror() can clobber (gcc's
-Wclobbered), in a kernel manifest's sources (its first configuration).

waserror() is setlabel(), Plan 9's setjmp (portfns.h: returns_twice for
gcc).  A local changed between waserror() and the error() that comes back
to it, and read after that, is undefined unless volatile: gcc may keep it
in a register, which the jump restores to its value at waserror().  kencc
keeps such locals in memory, so 9front never needed volatile there.  This
is how port/sdmmc.c's retryproc spun forever (its counter came back 0).

Each source of the manifest is compiled as the build does (in the
manifest's directory, with its Cflags and the arch's includes), output to
/dev/null, with -Wclobbered added; the warnings are printed, sorted by
file, one per place.  Objects of the tree are not touched.

usage: scripts/check/find-clobbered.py [manifest]	(default sys/src/9/pc64/build.json)
       exit status 1 if there are warnings
"""
import concurrent.futures
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def main():
	manifest = sys.argv[1] if len(sys.argv) > 1 else 'sys/src/9/pc64/build.json'
	manifest = os.path.join(ROOT, manifest)
	d = json.load(open(manifest))
	m = d[0] if isinstance(d, list) else d
	cwd = os.path.dirname(manifest)
	arch = 'amd64' if '-m64' in m.get('Cflags', []) else os.environ.get('objtype', 'amd64')
	cc = os.environ.get('CC', 'gcc')
	base = [cc, '-std=c11', '-c', '-I', os.path.join(ROOT, arch, 'include'),
		'-I', os.path.join(ROOT, 'sys', 'include'), '-I', '.'] + m.get('Cflags', []) + ['-Wclobbered']
	srcs = [s for s in m.get('SourceFiles', []) if s.endswith('.c') and os.path.exists(os.path.join(cwd, s))]
	missing = [s for s in m.get('SourceFiles', []) if s.endswith('.c') and not os.path.exists(os.path.join(cwd, s))]
	if missing:
		print('find-clobbered: %d sources missing (generated or copied by the build: build first): %s'
			% (len(missing), ' '.join(missing[:5])), file=sys.stderr)

	def one(src):
		r = subprocess.run(base + [src, '-o', os.devnull], cwd=cwd, capture_output=True, text=True,
			env=dict(os.environ, LC_ALL='C'))
		return [l for l in r.stderr.splitlines() if '-Wclobbered' in l]

	found = set()
	with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
		for lines in ex.map(one, srcs):
			for l in lines:
				# file:line:col: warning: variable 'x' might be clobbered ... [-Wclobbered]
				mm = re.match(r'(.*?):(\d+):\d+: warning: (.*) \[-Wclobbered\]', l)
				if mm:
					found.add((os.path.normpath(os.path.join(os.path.relpath(cwd, ROOT), mm.group(1))),
						int(mm.group(2)), mm.group(3)))
	for f, n, w in sorted(found):
		print('%s:%d: %s' % (f, n, w))
	print('find-clobbered: %d sources, %d places' % (len(srcs), len(found)), file=sys.stderr)
	return 1 if found else 0


if __name__ == '__main__':
	sys.exit(main())
