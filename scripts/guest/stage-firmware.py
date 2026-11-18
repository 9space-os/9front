#!/usr/bin/env python3
"""stage-firmware.py DIR: the firmware of firmware.list into DIR (the CD's
/lib/firmware), unpacked from linux-firmware (FWDIR, default
/usr/lib/firmware: .zst, .xz or plain), each file checked against its
sha256; a file that differs (another linux-firmware) stops it.

stage-firmware.py -u: firmware.list made anew from firmware.spec and FWDIR
(the globs expanded, the sha256 of each file's contents, the version of
linux-firmware where pacman knows it).

Licences are looked for in FWDIR, then in /usr/share/licenses/linux-firmware*
(Arch keeps them there)."""
import glob, hashlib, lzma, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = os.path.join(HERE, 'firmware.spec')
LIST = os.path.join(HERE, 'firmware.list')
FWDIR = os.environ.get('FWDIR', '/usr/lib/firmware')
LICDIRS = sorted(glob.glob('/usr/share/licenses/linux-firmware*'))
PACKED = ('.zst', '.xz', '')


def die(msg):
	sys.exit('stage-firmware: ' + msg)


def find(src):
	"""the file for a name in linux-firmware, packed or not"""
	for d in [FWDIR] + LICDIRS:
		for z in PACKED:
			p = os.path.join(d, src + z)
			if os.path.isfile(p):
				return p
	return None


def contents(path):
	path = os.path.realpath(path)	# linux-firmware's links (zstd takes no link)
	if path.endswith('.zst'):
		return subprocess.run(['zstd', '-q', '-d', '-c', path], check=True,
			stdout=subprocess.PIPE).stdout
	if path.endswith('.xz'):
		with lzma.open(path) as f:
			return f.read()
	with open(path, 'rb') as f:
		return f.read()


def unpacked(name):
	for z in ('.zst', '.xz'):
		if name.endswith(z):
			return name[:-len(z)]
	return name


def spec():
	for line in open(SPEC):
		line = line.split('#', 1)[0].strip()
		if line:
			src, dst = line.split()
			yield src, dst


def update():
	out = []
	for src, dst in spec():
		if any(c in src for c in '*?['):
			paths = sorted(glob.glob(os.path.join(FWDIR, src)))
			if not paths:
				die('nothing for %s in %s' % (src, FWDIR))
			for p in paths:
				if not os.path.isfile(p):
					continue
				rel = unpacked(os.path.relpath(p, FWDIR))
				name = dst + os.path.basename(rel) if dst.endswith('/') else dst
				out.append((rel, name, hashlib.sha256(contents(p)).hexdigest()))
		else:
			p = find(src)
			if p is None:
				die('no %s in %s or %s' % (src, FWDIR, ' '.join(LICDIRS)))
			out.append((src, dst, hashlib.sha256(contents(p)).hexdigest()))
	try:
		ver = subprocess.run(['pacman', '-Q', 'linux-firmware'], check=True,
			stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout.split()[1]
	except Exception:
		ver = 'unknown'
	with open(LIST, 'w') as f:
		f.write('# made by stage-firmware.py -u from firmware.spec: linux-firmware %s\n' % ver)
		f.write('# source in linux-firmware, name under /lib/firmware, sha256 of the contents\n')
		for src, dst, sha in out:
			f.write('%s\t%s\t%s\n' % (src, dst, sha))
	print('stage-firmware: %d files, linux-firmware %s' % (len(out), ver))


def stage(outdir):
	n = size = 0
	bad = []
	for line in open(LIST):
		if not line.strip() or line.startswith('#'):
			continue
		src, dst, sha = line.rstrip('\n').split('\t')
		p = find(src)
		if p is None:
			bad.append('missing %s' % src)
			continue
		data = contents(p)
		if hashlib.sha256(data).hexdigest() != sha:
			bad.append('differs %s' % src)
			continue
		t = os.path.join(outdir, dst)
		os.makedirs(os.path.dirname(t), exist_ok=True)
		with open(t, 'wb') as f:
			f.write(data)
		n += 1
		size += len(data)
	if bad:
		die('%d of firmware.list not as listed (another linux-firmware? '
			'stage-firmware.py -u):\n\t%s' % (len(bad), '\n\t'.join(bad[:20])))
	print('stage-firmware: %d files, %.1f MB in %s' % (n, size/1e6, outdir), file=sys.stderr)


def main():
	if sys.argv[1:] == ['-u']:
		update()
	elif len(sys.argv) == 2:
		stage(sys.argv[1])
	else:
		sys.exit(__doc__)


main()
