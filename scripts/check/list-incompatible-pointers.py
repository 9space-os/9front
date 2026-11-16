#!/usr/bin/env python3
"""
list-incompatible-pointers.py LOG: the places of gcc's
-Wincompatible-pointer-types in a build's log (build.exe's output, LC_ALL=C: gcc's messages in English),
grouped by kind, as scripts/check/incompatible-pointers.txt keeps them.
The warning stays a warning (cflags.json: -Wno-error=incompatible-pointer-types):
9front's code has idioms that are correct on its machines (qsort with typed
comparators, sunrpc's calls passed as SunCall* by their layout, void** for
T**, char** for execve's char *const[]); the list is what is known, a
build's new places show against it (diff).
"""
import re, sys

KINDS = [
	('qsort comparator with typed parameters', r"[‘']qsort[’']"),
	('exec argv/envp: char** for char *const[]', r"[‘'](execve|execvp|execv|_EXEC)[’']"),
	("sunrpc: a protocol's call passed as SunCall* (same layout)", r"[‘'](sunClientRpc|nfs3Call|mountCall|portCall|sunCall\w*|nfs3\w*)[’']"),
	('void** assigned to T**', r"from incompatible pointer type [‘']void \*\*[’']"),
	('pointer types differ in a conditional expression', r'conditional expression'),
]
places = {}
for l in open(sys.argv[1], errors='replace'):
	m = re.match(r'^(?:.*/)?([^/:\s]+):(\d+):\d+: warning: (.*)\[-Wincompatible-pointer-types\]', l)
	if not m:
		continue
	f, line, msg = m.groups()
	kind = next((k for k, p in KINDS if re.search(p, msg)), 'other: ' + msg.strip())
	places.setdefault(kind, set()).add('%s:%s' % (f, line))
for kind in sorted(places, key=lambda k: (k.startswith('other'), -len(places[k]))):
	print('# %s (%d)' % (kind, len(places[kind])))
	for p in sorted(places[kind]):
		print(p)
