#!/usr/bin/env python3
"""
The tree's parts built with the cross compiler of toolchain/gcc (GCC_DIR in
paths.env) for APE (POSIX on 9front): the libraries of sys/src/mesa
(libvulkan.a), sys/src/libffi, sys/src/wayland, sys/src/sdl3, sys/src/glfw,
sys/src/freetype, sys/src/pixman, sys/src/expat, sys/src/xkbcommon,
sys/src/fontconfig, sys/src/fcft, sys/src/foot (their build.json, compatibility
manifests, by build.exe -D; the last seven vendored by sys/src/build/ape/vendor.sh
from their VENDOR.json) and the programs made of them (wlsys, foot,
cxxwidgets), which sys/src/build/step/ape.sh installs in /$objtype/bin.
sys/src/build/gen/genbuild.py makes the build step, sys/src/build/step/check-install.py
expects the programs.

usage: sys/src/build/gen/apeprogs.py	lists the programs: path source
"""
import os, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'sys', 'src', 'build', 'lib'))
import paths	# GCC_DIR, RUST_DIR, ... (the distribution's paths.env)

# the architectures ape.sh builds for (sys/src/build/mesa/common.sh: its sysroot view)
OBJTYPES = ['amd64']

# the libraries' manifests, in the order they depend on each other
LIBS = ['sys/src/mesa', 'sys/src/libffi', 'sys/src/wayland', 'sys/src/sdl3', 'sys/src/glfw',
	'sys/src/freetype', 'sys/src/pixman', 'sys/src/expat', 'sys/src/xkbcommon', 'sys/src/fontconfig', 'sys/src/fcft', 'sys/src/foot']

# program: its source directory
PROGS = {'wlsys': 'sys/src/cmd/wlsys', 'foot': 'sys/src/foot', 'cxxwidgets': 'sys/src/libui/cxx'}

# programs in C++ (GCC_DIR's g++ and libstdc++, or toolchain/clang's clang++ and
# libc++): their sources and link
# arguments (sys/src/build/step/ape.sh)
CXX = {
	'cxxwidgets': ['sys/src/libui/cxx/cxxwidgets.cc', '-l9'],
}

# programs of a vendored release, linked from its library (LIBS made it): their
# link arguments (sys/src/build/step/ape.sh); the others sys/src/build/wayland/build.sh makes
LINKS = {
	'foot': ['-Wl,-u,main', '-lfoot', '-lfcft', '-lfontconfig', '-lfreetype', '-lexpat',
		'-lpixman-1', '-lxkbcommon', '-lwayland-cursor', '-lwayland-client', '-lffi', '-lm'],
}

TRIPLES = {'amd64': 'x86_64-pc-plan9', '386': 'i686-pc-plan9'}

def toolset():
	"""CC's family: gcc, or clang (build.exe's TOOLSET, else CC's name)"""
	t = os.environ.get('TOOLSET')
	if t:
		return t
	return 'clang' if 'clang' in os.path.basename(os.environ.get('CC', '')) else 'gcc'

def cross(objtype):
	"""is there the cross compiler for objtype: toolchain/gcc's (GCC_DIR), or
	with CC a clang toolchain/clang's (CLANG_DIR: its cross clang and the
	runtimes for objtype)?"""
	if objtype not in TRIPLES:
		return False
	if toolset() == 'clang':
		d = paths.get('CLANG_DIR')
		return os.access(os.path.join(d, 'build', 'host', 'bin', 'clang'), os.X_OK) and \
			os.path.isfile(os.path.join(d, 'out', objtype, objtype, 'clang', 'lib', 'libc++.a'))
	d = paths.get('GCC_DIR')
	return os.access(os.path.join(d, 'build', objtype, 'cross', 'bin', TRIPLES[objtype] + '-gcc'), os.X_OK)

if __name__ == '__main__':
	for p, d in sorted(PROGS.items()):
		print(p, d)
