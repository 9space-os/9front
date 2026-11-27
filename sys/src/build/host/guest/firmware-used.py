#!/usr/bin/env python3
"""firmware-used.py: firmware.used, the firmware the tree's GPU and WLAN drivers
from Linux and FreeBSD can load: the names their sources declare with
MODULE_FIRMWARE, as the build compiles them (the manifests' sources and flags:
sys/src/linux/build.json, sys/src/cmd/aux/rtw89/build.json), preprocessed with
MODULE_FIRMWARE kept as a marker (the LinuxKPI's linux/module.h, which empties it,
left out by its guard). rtw89 asks for its firmware's newest format and falls
back to the older ones (rtw8852c_fw-2.bin, -1, none): those are kept too.

stage-firmware.py -u keeps of firmware.spec's globs (amdgpu/*, i915/*, rtw89/*)
only the names listed here. Run it after the drivers' sources change, then
dist/bin/mkfirmware -u; commit both lists."""
import json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', '..'))
OUT = os.path.join(HERE, 'firmware.used')
MANIFESTS = ['sys/src/linux/build.json', 'sys/src/cmd/aux/rtw89/build.json']
MARK = re.compile(r'__P9FW__\s*\(((?:\s*"[^"]*")+)\s*\)')


def commands():
	for m in MANIFESTS:
		d = os.path.join(ROOT, os.path.dirname(m))
		for e in json.load(open(os.path.join(ROOT, m))):
			flags = e.get('Cflags') or []
			if not any(f.startswith('-DKBUILD_MODNAME') for f in flags):
				continue	# the LinuxKPI itself, not a driver
			for src in e.get('SourceFiles', []):
				yield d, ['gcc', '-E', '-w', '-D_KPI_LINUX_MODULE_H',
					'-DMODULE_FIRMWARE(s)=__P9FW__(s)'] + flags + [src]


def names(cmd):
	d, a = cmd
	r = subprocess.run(a, cwd=d, capture_output=True, text=True, errors='replace')
	if r.returncode != 0 and not r.stdout:
		sys.exit('firmware-used: %s: %s' % (a[-1], r.stderr.strip()[:300]))
	return {''.join(re.findall(r'"([^"]*)"', m.group(1))) for m in MARK.finditer(r.stdout)}


def fallbacks(name):
	"""rtw89's older formats of a firmware it asks for"""
	m = re.match(r'(rtw89/.*_fw)-(\d+)\.bin$', name)
	if not m:
		return {name}
	return {name, m.group(1) + '.bin'} | {'%s-%d.bin' % (m.group(1), k) for k in range(1, int(m.group(2)))}


def main():
	cmds = list(commands())
	used = set()
	with ThreadPoolExecutor(os.cpu_count() or 4) as ex:
		for n in ex.map(names, cmds):
			for x in n:
				used |= fallbacks(x)
	with open(OUT, 'w') as f:
		f.write('# made by firmware-used.py from the drivers\' MODULE_FIRMWARE (%d sources)\n' % len(cmds))
		for n in sorted(used):
			f.write(n + '\n')
	print('firmware-used: %d names from %d sources' % (len(used), len(cmds)))


if __name__ == '__main__':
	main()
