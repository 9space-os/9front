#!/usr/bin/env python3
# scripts/check/find-clobbered.py FILE...: kernel C where a local is assigned after a
# waserror() and read in its handler: after error()'s longjmp it is indeterminate
# unless volatile (C11 7.13.2.1), and gcc keeps it in a register (devmnt.c mntwalk
# leaked a chan per failed walk). A heuristic: it does not see poperror, review each
# heuristic: locals assigned after a waserror() and read in its handler block
import re, sys, os
def funcs(src):
    for m in re.finditer(r'\n(\w[\w \t\*]*\n)?(\w+)\(([^;{)]*)\)\n\{', src):
        start = m.end(); depth = 1; i = start
        while depth and i < len(src):
            depth += {'{':1,'}':-1}.get(src[i],0); i += 1
        yield m.group(2), m.start(), src[start:i]
for path in sys.argv[1:]:
    src = open(path, errors='replace').read()
    for name, off, body in funcs(src):
        decl = body.split('\n\n')[0]
        locs = set(re.findall(r'\b(\w+)\s*(?:\[[^\]]*\])?\s*[,;=]', decl)) - {'volatile','int','long','char','uchar','ulong','uint','vlong','uvlong','uintptr','void','static'}
        for w in re.finditer(r'if\s*\(\s*waserror\(\)\s*\)\s*\{', body):
            # handler block
            j = w.end(); depth = 1
            while depth and j < len(body):
                depth += {'{':1,'}':-1}.get(body[j],0); j += 1
            handler = body[w.end():j]
            rest = body[j:]
            used = set(re.findall(r'\b(\w+)\b', handler)) & locs
            for v in used:
                if re.search(r'(?<![=!<>])\b' + v + r'\s*(=(?!=)|\+\+|--|\+=|-=|\|=)', rest) and not re.search(r'volatile[^;]*\b' + v + r'\b', decl):
                    line = src[:off].count('\n') + body[:w.start()].count('\n') + 2
                    print('%s:%d: %s: %s' % (path, line, name, v))
