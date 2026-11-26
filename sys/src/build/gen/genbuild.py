#!/usr/bin/env python3
"""
The top-level manifests of the build tool: build.json (everything for
$ARCH) and build.<objtype>.json for 386, amd64 and arm64, so that

	CC=gcc ARCH=amd64 sys/src/cmd/build/build.exe

builds what the system needs, in dependency order: libc, the libraries
(sys/src/build/gen/genlibs.py --list), the kernel's klib*.a, the programs
(sys/src/build/gen/gencmds.py --list: sys/src/mkfile's $SUBSYS $CMDS; APE's musl
part after libap's, sys/src/build/gen/genmusl.py --list), the files
the kernel embeds (sys/src/build/step/gen-*.sh: rebootcode.i, apbootstrap.i, ...,
initcode, /boot/boot and $CONF.bootfs.paq), the kernel, its Plan 9
a.out where 9boot loads it (sys/src/build/step/elf2aout.py), and the check that
everything mk install installs is there (sys/src/build/step/check-install.py).
The build tool sets objtype, O, SUFFIX, CONF and TOOLPREFIX from $ARCH.
9front's boot loaders are built apart, in the system just made, with
its own compilers (sys/src/build/host/guest/build-boot.sh).

Run after a mkfile change, with the other generators (sys/src/build/host/tests/static
checks that the checked-in files are what this writes).

usage: sys/src/build/gen/genbuild.py
"""
import json, os, subprocess, sys

import genclean

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..'))

# per objtype: the kernel's generators (as pc64/mkfile, pc/mkfile,
# arm64/mkfile make them before the kernel objects), its manifest and
# the ELF kernel and a.out
KERNEL = {
	'amd64': (['bash /sys/src/build/step/gen-rebootcode-i.sh', 'bash /sys/src/build/step/gen-apbootstrap-i.sh',
		'bash /sys/src/build/step/gen-etherm10g-i.sh', 'bash /sys/src/build/step/gen-sd53c8xx-i.sh',
		'bash /sys/src/build/step/gen-initcode-i.sh', 'bash /sys/src/build/step/gen-bootfs.sh pc64'],
		'/sys/src/9/pc64/build.json', '/amd64/bin/9pc64', '/amd64/9pc64'),
	'386': (['bash /sys/src/build/step/gen-rebootcode-pc-i.sh', 'bash /sys/src/build/step/gen-apbootstrap-i.sh pc',
		'bash /sys/src/build/step/gen-sd53c8xx-i.sh', 'bash /sys/src/build/step/gen-initcode-i.sh',
		'bash /sys/src/build/step/gen-bootfs.sh pc'],
		'/sys/src/9/pc/build.json', '/386/bin/9pc', '/386/9pc'),
	'arm64': (['bash /sys/src/build/step/gen-rebootcode-i.sh arm64', 'bash /sys/src/build/step/gen-initcode-i.sh',
		'bash /sys/src/build/step/gen-bootfs.sh qemu arm64'],
		'/sys/src/9/arm64/build.json', '/arm64/bin/9qemu', '/arm64/9qemu'),
}

def listing(script, objtype):
	env = dict(os.environ, objtype=objtype)
	r = subprocess.run([sys.executable, os.path.join(ROOT, 'sys', 'src', 'build', 'gen', script), '--list'],
		cwd=ROOT, env=env, capture_output=True, text=True)
	if r.returncode != 0:
		sys.exit('genbuild: %s --list: %s' % (script, r.stderr.strip()))
	return r.stdout.split()

def write(path, builds):
	s = json.dumps(builds, indent='\t') + '\n'
	p = os.path.join(ROOT, path)
	if not os.path.exists(p) or open(p).read() != s:
		open(p, 'w').write(s)
		print(path)

def main():
	klibs = sorted('/sys/src/%s/klib.json' % d for d in os.listdir(os.path.join(ROOT, 'sys/src'))
		if d.startswith('lib') and os.path.exists(os.path.join(ROOT, 'sys/src', d, 'klib.json')))
	for objtype, (gens, kern, elf, aout) in KERNEL.items():
		sfx = '' if objtype == 'amd64' else '.' + objtype
		libs = ['/sys/src/%s/%s%s.json' % (l, l, sfx) for l in listing('genlibs.py', objtype)]
		cmds = ['/' + c for c in listing('gencmds.py', objtype)]
		# APE's musl part (gcc only, no mkfile): appended to libap after
		# ape/lib/ap/gnu, before the programs that link libap
		gnu = [i for i, c in enumerate(cmds) if c.startswith('/sys/src/ape/lib/ap/gnu/')]
		cmds[gnu[-1] + 1:gnu[-1] + 1] = ['/' + c for c in listing('genmusl.py', objtype)]
		write('sys/src/build/build.%s.json' % objtype, [
			{'Name': 'prep', 'Pre': ['bash /sys/src/build/step/prep-objtype.sh']},
			{'Name': 'libc', 'Projects': ['/sys/src/libc/libc%s.json' % sfx]},
			{'Name': 'libs', 'Projects': libs},
			{'Name': 'klibs', 'Projects': klibs},
			{'Name': 'cmds', 'Projects': cmds},
			# Clean relative to this manifest's directory (sys/src/build), as the
			# build tool reads it: there it keeps a file's time when a step writes
			# it the same again (else -D compiled the kernel's users of the .i
			# files every time), and -clean finds them
			{'Name': 'kernelgen', 'Pre': gens, 'Clean': [os.path.relpath(os.path.join(ROOT, f), os.path.join(ROOT, 'sys/src/build'))
				for f in genclean.clean(gens, ROOT)]},
			{'Name': 'kernel', 'Projects': [kern]},
			# the build tool itself, for the installed system (sys/src/build/step/build-tool.sh)
			{'Name': 'install', 'Post': ['python3 /sys/src/build/step/elf2aout.py %s %s' % (elf, aout)]
				+ (['bash /sys/src/build/step/build-tool.sh'] if objtype in ('amd64', '386', 'arm64') else [])
				+ ['python3 /sys/src/build/step/check-install.py -r']},
		])
	write('sys/src/build/build.json', [{'Name': 'all', 'Projects': ['/sys/src/build/build.$ARCH.json']}])

if __name__ == '__main__':
	main()
