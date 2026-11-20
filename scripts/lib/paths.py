"""scripts/lib/paths.py: where 9space's other parts are, as
scripts/lib/paths.sh finds them: the environment first, then the
distribution's paths.env ($NINESPACE_PATHS, else ROOT/../dist/paths.env).
paths.env is the only place with paths (CLAUDE.md): without it, importing
this module stops with an error, it does not guess.

	import paths; paths.get('GCC_DIR')"""
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..'))

def _load():
	vals = {}
	for f in (os.environ.get('NINESPACE_PATHS'), os.path.join(ROOT, '../dist/paths.env')):
		if f and os.path.isfile(f):
			top = os.path.dirname(os.path.dirname(os.path.realpath(f)))	# physical: through a symlink too
			for line in open(f):
				line = line.strip()
				if line and not line.startswith('#') and '=' in line:
					k, v = line.split('=', 1)
					vals[k] = os.path.normpath(v if v.startswith('/') else os.path.join(top, v))
			return vals
	sys.exit('paths.py: no dist/paths.env found ($NINESPACE_PATHS, %s): the tree belongs in 9space beside dist/ (dist/bin/repos)'
		% os.path.join(ROOT, '../dist/paths.env'))

_vals = _load()

def get(name):
	"""the path name stands for: the environment's, else paths.env's"""
	return os.environ.get(name) or _vals.get(name)

def archive(root, name):
	"""an archive as VERSION or VENDOR.json names it: $DISTFILES/x (the
	distribution's archives), else a path from the tree's root"""
	if name.startswith('$DISTFILES/'):
		return os.path.join(get('DISTFILES'), name[len('$DISTFILES/'):])
	return os.path.normpath(os.path.join(root, name))
