#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# find-gpl.py: GPL or LGPL code that 9space brought into the tree.
#
# The tree takes permissive code only (MIT, BSD, ISC, zlib, public
# domain; ../../../../../CLAUDE.md "Rules"): a file we added or changed
# against 9front (branch front) that carries a GPL or LGPL notice fails,
# unless the notice offers a permissive licence too (dual-licensed: the
# LinuxKPI's "GPL-2.0 OR MIT", FreeBSD's "Alternatively ... GPL"), it is
# a Bison parser (the skeleton's exception), or gpl-allowed.txt beside
# this script names it (the exceptions the user accepted, with why).
# 9front's own files (those branch front has, changed for the port or
# not: antiword, ghostscript, mp3dec, ...) are 9front's to judge: listed
# with -v only.
#
# usage: find-gpl.py [-v] [ROOT]    (ROOT: the tree, default: this checkout)
#   exit 0 clean, 1 with findings (one line each: path: the notice)
import fnmatch, os, re, subprocess, sys

GPL = re.compile(r'GNU (Lesser |Library )?General Public License|GNU L?GPL\b|'
                 r'\bL?GPL[- ]?v?[23]\b|SPDX-License-Identifier:[^\n]*\bL?GPL|'
                 r'Plan9License|UCB release of Plan 9', re.I)
# a permissive choice beside the GPL
DUAL = re.compile(r'GPL-2\.0[^\n]{0,40}\bOR\b[^\n]{0,20}(MIT|BSD|X11|Linux-OpenIB|Apache)|'
                  r'\b(MIT|BSD-[1-4]-Clause|X11|Apache-2\.0)\b[^\n]{0,20}\bOR\b[^\n]{0,30}GPL|'
                  r'alternatively,?\s+(this\s+(software|file|code)|the\s+contents\s+of\s+this\s+file)'
                  r'\s+may\s+be\s+(distributed|used)[^.]{0,80}GNU\s+(Lesser\s+)?General\s+Public|'
                  r'dual[- ](licensed|license|MIT/GPL|BSD/GPL)|\(?MIT/GPL\)?|BSD/GPL', re.I)
BISON = re.compile(r'As a special exception, you may create a larger work that contains\s+part or all of the Bison parser skeleton', re.I)


def git(root, *args):
    return subprocess.run(['git', '-C', root] + list(args), capture_output=True,
                          text=True, check=True).stdout


def allowed(root):
    pats = []
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gpl-allowed.txt')
    for line in open(p):
        line = line.split('#', 1)[0].strip()
        if line:
            pats.append(line.split()[0])
    return pats


def main():
    verbose = '-v' in sys.argv[1:]
    args = [a for a in sys.argv[1:] if a != '-v']
    root = args[0] if args else git(os.path.dirname(os.path.abspath(__file__)),
                                    'rev-parse', '--show-toplevel').strip()
    pats = allowed(root)
    # the files with a notice, then which of them are 9front's as they are
    hits = git(root, 'grep', '-l', '-I', '-i', '-E',
               r'General Public License|GNU L?GPL|L?GPL[- ]?v?[23]|SPDX-License-Identifier:.*GPL|Plan9License|UCB release of Plan 9',
               '--', '.').split('\n')
    hits = [h for h in hits if h]
    same = set()
    if git(root, 'rev-parse', '--verify', '-q', 'front').strip():
        infront = set(git(root, 'ls-tree', '-r', '--name-only', 'front').split('\n'))
        same = {h for h in hits if h in infront}
    bad = 0
    for h in hits:
        try:
            text = open(os.path.join(root, h), errors='replace').read()
        except OSError:
            continue
        m = GPL.search(text)
        if m is None:
            continue
        if h in same:
            if verbose:
                print('9front: %s' % h)
            continue
        if DUAL.search(text) or BISON.search(text):
            continue
        if any(fnmatch.fnmatch(h, p) for p in pats):
            continue
        line = text[:m.start()].count('\n') + 1
        print('%s:%d: %s' % (h, line, text.split('\n')[line-1].strip()[:100]))
        bad += 1
    if bad:
        print('find-gpl: %d file(s) with a GPL/LGPL notice and no permissive choice '
              '(remove them, or add an exception the user accepted to gpl-allowed.txt)' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
