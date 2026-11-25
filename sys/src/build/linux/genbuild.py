#!/usr/bin/env python3
"""genbuild.py: the build.exe manifests of the LinuxKPI and its drivers.

  genbuild.py [-n]
	writes sys/src/linux/build.json (amd64): libkpi9.a (the Plan 9
	side, kpi/plan9), libkpi.a (the Linux side, kpi/linux and the
	OpenBSD kernel API under it, kpi/obsd), libkpitest.a (Linux's
	dma-buf selftests, SELFTESTS), liblinux.a (what BUILD names, from
	vendor/ or our replace/) and
	libamdgpu.a (of it drivers/gpu/drm/amd), one
	build object per directory (its own include path and KBUILD_MODNAME,
	as Kbuild has them); sys/src/cmd/aux/gpu/i915/build.json,
	sys/src/cmd/aux/gpu/amdgpu/build.json and sys/src/cmd/aux/rtw89/build.json
	(rtw89 from FreeBSD's copy, RTW89FILES).
	-n: only compare, exit 1 if a manifest differs (sys/src/build/host/tests/static).

The files and flags are census.py's (kconfig, BUILD, OMIT), compiled
with -O2 as Linux is (BUILD_BUG_ON needs the optimiser)."""
import glob, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, 'census.py')).read().replace('main()\n', '')
g = {'__name__': 'x', '__file__': os.path.join(HERE, 'census.py')}
exec(src, g)
D = g['D']
TREE = os.path.normpath(os.path.join(D, '../../..'))

# as Linux compiles (Makefile: KBUILD_CFLAGS), without what is the kernel's own
LINUXFLAGS = ['-std=gnu11', '-O2', '-g', '-fno-strict-aliasing', '-fno-strict-overflow',
	'-fno-delete-null-pointer-checks', '-fno-common', '-fno-pie', '-fno-stack-protector',
	'-fno-omit-frame-pointer', '-mcmodel=small', '-fshort-wchar', '-funsigned-char',
	'-Wno-unused', '-Wno-pointer-sign', '-Wno-address-of-packed-member', '-Wno-format-truncation']

# Kbuild's module names, by directory
MODNAME = [('drivers/gpu/drm/i915', 'i915'), ('drivers/gpu/drm/amd', 'amdgpu'), ('drivers/gpu/drm/scheduler', 'gpu_sched'), ('drivers/gpu/drm/display', 'drm_display_helper'),
	('drivers/gpu/drm/ttm', 'ttm'), ('drivers/gpu/drm', 'drm'), ('drivers/dma-buf', 'dma_buf'),
	('drivers/video', 'hdmi'), ('lib', 'scatterlist')]

def rel(p, base):
	return os.path.relpath(p, base)

def linuxflags(c, base, modname):
	"""census.py's flags for file c, for a manifest in base"""
	out = []
	fl = g['flags'](c)
	i = 0
	while i < len(fl):
		f = fl[i]
		if f in ('-fsyntax-only',) or f.startswith('-fmax-errors') or f.startswith('-std='):
			pass
		elif f.startswith('-DKBUILD_MODNAME='):
			out.append('-DKBUILD_MODNAME="%s"' % modname)
		elif f == '-I' and not os.path.isdir(fl[i + 1]):
			i += 1
		elif f in ('-I', '-include'):
			out.append(f + rel(fl[i + 1], base) if f == '-I' else f)
			if f == '-include':
				out.append(rel(fl[i + 1], base))
			i += 1
		else:
			out.append(f)
		i += 1
	return LINUXFLAGS + [x for x in out if x not in LINUXFLAGS]

def modname(d):
	for p, m in MODNAME:
		if d == p or d.startswith(p + '/'):
			return m
	return os.path.basename(d)

def linuxfiles():
	cfg = g['kbuild'].config()
	files = []
	for kind, path in g['build']():
		files += g['kbuild'].objects(g['V'], path, cfg) if kind == 'dir' else [path]
	om = g['omitted']()
	return [f for f in files if f not in om]

def libs():
	out = []
	out.append({
		'Name': 'linux-kpi9',
		'Include': ['/$ARCH/cflags.json'],
		'Install': '/$ARCH/lib.gcc/',
		'Library': 'libkpi9.a',
		'SourceFiles': sorted(rel(f, D) for f in glob.glob(os.path.join(D, 'kpi/plan9/*.c'))),
	})
	kpi = sorted(rel(f, D) for f in glob.glob(os.path.join(D, 'kpi/linux/*.c')) + glob.glob(os.path.join(D, 'kpi/obsd/*.c')))
	out.append({
		'Name': 'linux-kpi',
		'Noinc': True,
		'Cflags': linuxflags('kpi/linux/x.c', D, 'kpi'),
		'Install': '/$ARCH/lib.gcc/',
		'Library': 'libkpi.a',
		'SourceFiles': kpi,
	})
	bydir = {}
	for c in linuxfiles():
		bydir.setdefault(os.path.dirname(c), []).append(c)
	# Linux's own selftests of what the layer stands in for (MIT): aux/i915 -T
	out.append({
		'Name': 'linux-selftests',
		'Noinc': True,
		'Cflags': linuxflags('drivers/dma-buf/selftest.c', D, 'dmabuf_selftests'),
		'Install': '/$ARCH/lib.gcc/',
		'Library': 'libkpitest.a',
		'SourceFiles': [rel(g['source'](c), D) for c in SELFTESTS],
	})
	# amdgpu (drivers/gpu/drm/amd) in an archive of its own, the rest
	# (DRM's core and helpers, TTM, the scheduler, i915) in liblinux.a
	first = {}
	for d in sorted(bydir):
		lib = 'libamdgpu.a' if d.startswith('drivers/gpu/drm/amd/') else 'liblinux.a'
		o = {
			'Name': 'linux-' + d.replace('/', '-'),
			'Noinc': True,
			'Cflags': linuxflags(bydir[d][0], D, modname(d)),
			'Install': '/$ARCH/lib.gcc/',
			'Library': lib,
			'SourceFiles': [rel(g['source'](c), D) for c in sorted(bydir[d])],
		}
		if lib not in first:
			o['Shared'] = True
			first[lib] = True
		else:
			o['Append'] = True
		out.append(o)
	return out

SELFTESTS = ['drivers/dma-buf/selftest.c', 'drivers/dma-buf/st-dma-fence.c', 'drivers/dma-buf/st-dma-fence-chain.c',
	'drivers/dma-buf/st-dma-fence-unwrap.c', 'drivers/dma-buf/st-dma-resv.c']

P9LIBS = ['lib9p.a', 'libthread.a', 'libauth.a', 'libc.a']

def i915():
	base = os.path.join(TREE, 'sys/src/cmd/aux/gpu/i915')
	return [{
		'Name': 'aux-i915-linux',
		'Noinc': True,
		'Cflags': linuxflags('drivers/gpu/drm/i915/x.c', base, 'i915'),
		'Install': '/$ARCH/lib.gcc/',
		'Library': 'libi915.a',
		'SourceFiles': sorted('linux/' + os.path.basename(f) for f in glob.glob(os.path.join(base, 'linux', '*.c'))),
	}, {
		'Name': 'aux-i915',
		'Include': ['/$ARCH/cflags.json'],
		'Cflags': ['-I', rel(os.path.join(D, 'kpi/include'), base)],
		'Install': '/$ARCH/bin/aux/',
		'Oflags': ['--entry=_main'],
		'Libs': ['--start-group'] + ['/$ARCH/lib.gcc/' + l for l in
			['libi915.a', 'libkpitest.a', 'liblinux.a', 'libkpi.a', 'libkpi9.a'] + P9LIBS + ['libgcc.a']] + ['--end-group'],
		'Program': 'i915',
		'SourceFiles': sorted(os.path.basename(f) for f in glob.glob(os.path.join(base, '*.c'))),
	}, {
		# the procs' call chains for a log (rc/bin/i915run)
		'Name': 'aux-i915stk',
		'Include': ['/$ARCH/cflags.json'],
		'Install': '/$ARCH/bin/aux/',
		'Oflags': ['--entry=_main'],
		'Libs': ['/$ARCH/lib.gcc/libc.a'],
		'Program': 'i915stk',
		'SourceFiles': ['stk/i915stk.c'],
	}]

def amdgpu():
	base = os.path.join(TREE, 'sys/src/cmd/aux/gpu/amdgpu')
	return [{
		'Name': 'aux-amdgpu-linux',
		'Noinc': True,
		'Cflags': linuxflags('drivers/gpu/drm/amd/amdgpu/x.c', base, 'amdgpu'),
		'Install': '/$ARCH/lib.gcc/',
		'Library': 'libamdgpuaux.a',
		'SourceFiles': sorted('linux/' + os.path.basename(f) for f in glob.glob(os.path.join(base, 'linux', '*.c'))),
	}, {
		'Name': 'aux-amdgpu',
		'Include': ['/$ARCH/cflags.json'],
		'Cflags': ['-I', rel(os.path.join(D, 'kpi/include'), base)],
		'Install': '/$ARCH/bin/aux/',
		'Oflags': ['--entry=_main'],
		'Libs': ['--start-group'] + ['/$ARCH/lib.gcc/' + l for l in
			['libamdgpuaux.a', 'libamdgpu.a', 'liblinux.a', 'libkpi.a', 'libkpi9.a'] + P9LIBS + ['libgcc.a']] + ['--end-group'],
		'Program': 'amdgpu',
		'SourceFiles': sorted(os.path.basename(f) for f in glob.glob(os.path.join(base, '*.c'))),
	}]

# rtw89 for the RTL8852CE (FreeBSD's copy, its BSD-3-Clause choice): what
# Linux's Makefile builds for CONFIG_RTW89_8852CE (rtw89_core with wow,
# debug; rtw89_pci; rtw89_8852c; rtw89_8852ce).  The Makefile itself is GPL,
# not imported: the list is written here.
RTW89 = 'freebsd/freebsd/sys/contrib/dev/rtw89'
RTW89FILES = ['core.c', 'mac80211.c', 'mac.c', 'mac_be.c', 'phy.c', 'phy_be.c', 'fw.c', 'cam.c',
	'efuse.c', 'efuse_be.c', 'regd.c', 'sar.c', 'coex.c', 'ps.c', 'chan.c', 'ser.c', 'acpi.c',
	'util.c', 'wow.c', 'debug.c', 'pci.c', 'pci_be.c',
	'rtw8852c.c', 'rtw8852c_table.c', 'rtw8852c_rfk.c', 'rtw8852c_rfk_table.c', 'rtw8852ce.c']

def rtw89():
	"""aux/rtw89: rtw89 and our Linux side in librtw89.a, the program"""
	base = os.path.join(TREE, 'sys/src/cmd/aux/rtw89')
	src = os.path.join(D, RTW89)
	flags = linuxflags('drivers/net/wireless/realtek/rtw89/x.c', base, 'rtw89')
	# its debug messages (rtw89_debug, by the module parameter debug_mask); no debugfs
	flags += ['-DCONFIG_RTW89_DEBUG=1', '-DCONFIG_RTW89_DEBUGMSG=1', '-I' + rel(src, base)]
	return [{
		'Name': 'aux-rtw89-linux',
		'Noinc': True,
		'Cflags': flags,
		'Install': '/$ARCH/lib.gcc/',
		'Library': 'librtw89.a',
		'SourceFiles': [rel(os.path.join(src, f), base) for f in RTW89FILES] +
			sorted('linux/' + os.path.basename(f) for f in glob.glob(os.path.join(base, 'linux', '*.c'))),
	}, {
		'Name': 'aux-rtw89',
		'Include': ['/$ARCH/cflags.json'],
		'Cflags': ['-I', rel(os.path.join(D, 'kpi/include'), base)],
		'Install': '/$ARCH/bin/aux/',
		'Oflags': ['--entry=_main'],
		'Libs': ['--start-group'] + ['/$ARCH/lib.gcc/' + l for l in
			['librtw89.a', 'libkpi.a', 'libkpi9.a'] + P9LIBS + ['libgcc.a']] + ['--end-group'],
		'Program': 'rtw89',
		'SourceFiles': sorted(os.path.basename(f) for f in glob.glob(os.path.join(base, '*.c'))),
	}]

def dump(o):
	return json.dumps(o, indent='\t') + '\n'

def main():
	check = sys.argv[1:] == ['-n']
	bad = 0
	for path, o in ((os.path.join(D, 'build.json'), libs()),
			(os.path.join(TREE, 'sys/src/cmd/aux/gpu/i915/build.json'), i915()),
			(os.path.join(TREE, 'sys/src/cmd/aux/gpu/amdgpu/build.json'), amdgpu()),
			(os.path.join(TREE, 'sys/src/cmd/aux/rtw89/build.json'), rtw89())):
		s = dump(o)
		old = open(path).read() if os.path.exists(path) else None
		if check:
			if old != s:
				print('%s: differs from sys/src/build/linux/genbuild.py' % rel(path, TREE))
				bad = 1
		elif old != s:
			open(path, 'w').write(s)
			print('wrote', rel(path, TREE))
	sys.exit(bad)

main()
