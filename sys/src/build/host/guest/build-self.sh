#!/bin/bash
# The system builds itself: boots the gcc-built system with the native
# gcc and binutils of toolchain/gcc (GCC_DIR in paths.env; /$objtype/gnu),
# or with CC a clang the native clang, lld and LLVM binutils of
# toolchain/clang (CLANG_DIR; /$objtype/clang), and runs the build
# tool there (sys/src/build/host/guest/boot/mkself.rc) on a copy of the tree's
# sources ($CLI_DIR/self.$objtype: git ls-files) -- the build of
# CC=gcc ARCH=$objtype sys/src/cmd/build/build.exe, on 9front (amd64:
# 9pc64, 386: 9pc with /386/gnu, i686-pc-plan9). The build
# tool for Plan 9 is built here (GOOS=plan9), the build steps are
# sys/src/build/step/rc/*.rc, the expected files check-install.py -l's.
# The guest gets the copy as an ext4 disk ($CLI_DIR/self.$objtype.img,
# made by mkfs.ext4 -d, served by 9front's ext4srv: much faster than ufs
# over the network); afterwards the image's tree goes back to the copy
# (debugfs rdump). The result stays there ($objtype/bin, the kernel);
# its objects are compared with this tree's (the Linux build's, if built
# for $objtype).
#
# usage: [objtype=386] [CC=clang] sys/src/build/host/guest/build-self.sh [buildfile...]
#        (needs the build for objtype with that toolset, the native
#        toolchain: GCC_DIR's out/$objtype, GCC_OUT; with CC=...clang
#        CLANG_DIR's out/$objtype, CLANG_OUT)
#        buildfile: only these manifests (relative to the tree, e.g.
#        sys/src/ape/cmd/cmds.json), else the whole build.json
#   env: CC       the toolset, as build.exe reads it: gcc (default) or one
#                 with clang in its name (x86_64-unknown-plan9-clang, clang)
#        WAITMAX  seconds at most for the build in the guest (28800)
#        SMP      CPUs of the guest (4)
#        MEM      its memory in MB (amd64 4096, 386 3072)
#        ROOM     MB of the image beyond the copy, for what the build writes
#                 (16384: amd64's objects, LinuxKPI and Mesa included; the image
#                 is sparse, the host gives only what is written)
#        KEEP     1: build in the copy of the last run (sys/src/build, the
#                 build tool and the manifests renewed), e.g. after a fix
set -e
ROOT=${PLAN9:-$(git -C "$(dirname "$0")" rev-parse --show-toplevel)}
. "$ROOT/sys/src/build/lib/paths.sh"	# GCC_DIR, CLANG_DIR, CLI_DIR (the distribution's paths.env)
A=${objtype:-amd64}
case $A in
amd64)	mem=4096 ;;
386)	mem=3072 ;;	# 9pc: below the PCI hole
*)	echo "build-self: objtype=$A: no native toolchain" >&2; exit 1 ;;
esac
die(){ echo "build-self: $*" >&2; exit 1; }
# the toolset by CC's name, as build.exe's (build.go: toolset): the native
# toolchain the guest gets ($TC: /$objtype/$TC) and the CC mkself gives the
# build tool there
case $(basename "${CC:-gcc}") in
*clang*)
	TC=clang SELFCC=clang
	TC_OUT=${CLANG_OUT:-$CLANG_DIR/out/$A}
	[ -x "$TC_OUT/$A/clang/bin/clang" ] && [ -x "$TC_OUT/$A/clang/bin/llvm-ar" ] ||
		die "no $TC_OUT/$A/clang/bin/clang and llvm-ar (CLANG_DIR: toolchain/clang, objtype=$A build.sh native)"
	[ -s "$ROOT/$A/lib.gcc/libclang_rt.builtins.a" ] ||
		die "no $A/lib.gcc/libclang_rt.builtins.a: the tree is not built with clang, run CC=<target>-clang ARCH=$A sys/src/cmd/build/build.exe"
	;;
*)
	TC=gnu SELFCC=gcc
	TC_OUT=${GCC_OUT:-$GCC_DIR/out/$A}
	[ -x "$TC_OUT/$A/gnu/bin/gcc" ] || die "no $TC_OUT/$A/gnu/bin/gcc (GCC_DIR: toolchain/gcc, build.sh native)"
	;;
esac
[ -s "$ROOT/$A/lib.gcc/libc.a" ] || die "no $A/lib.gcc/libc.a, run CC=${CC:-gcc} ARCH=$A sys/src/cmd/build/build.exe"

S=$CLI_DIR/self.$A
# the copy is the checkout's HEAD (git archive), not its working tree: the
# same commit builds the same, and uncommitted work (another session's)
# stays out
if [ "$KEEP" = 1 ] && [ -d "$S/sys/src" ]; then
	git -C "$ROOT" archive HEAD -- sys/src/build ':(glob)**/*.json' | tar -xf - -C "$S"
else
	rm -rf "$S"
	mkdir -p "$S"
	git -C "$ROOT" archive HEAD | tar -xf - -C "$S"
fi
(cd "$ROOT/sys/src/cmd/build" && GOOS=plan9 GOARCH=$A GO111MODULE=off go build -o "$S/sys/src/cmd/build/build.exe" .)
objtype=$A python3 "$ROOT/sys/src/build/step/check-install.py" -l > "$S/sys/src/build/check-install.$A"

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
# what the guest's root lacks: the native toolchain, the installed
# libraries and the mkfile of /$objtype (helper programs, mk)
mkdir -p "$T/extra/rc/bin" "$T/extra/$A"
cp -r "$TC_OUT/$A/$TC" "$T/extra/$A/"
cp -r "$ROOT/$A/lib.gcc" "$ROOT/$A/mkfile" "$ROOT/$A/plan9.ld" "$T/extra/$A/"
cp "$ROOT/sys/src/build/host/guest/boot/mkself.rc" "$T/extra/rc/bin/mkself"

# the copy as an ext4 file system with room for the build (virtio disk:
# sdF0 in the guest). No journal, orphan file or metadata checksums:
# ext4srv (lwext4) need not know them, the image is scratch
IMG=$CLI_DIR/self.$A.img
mb=$(( $(du -sm "$S" | cut -f1) + ${ROOM:-16384} ))
rm -f "$IMG"
mkfs.ext4 -q -F -L src9 -O ^has_journal,^orphan_file,^metadata_csum,^metadata_csum_seed \
	-E root_owner=$(id -u):$(id -g) -d "$S" "$IMG" ${mb}M || die "mkfs.ext4 $IMG"
# on the disk before QEMU opens it: else the host's writeback of the copy
# (GBs) holds QEMU in its open past boot-cli's wait for the serial line
sync "$IMG" || die "sync $IMG"

LOG=$CLI_DIR/mkself.$A.log
objtype=$A MEM=${MEM:-$mem} UFS_EXTRA=$T/extra \
	QEMU_EXTRA="-smp ${SMP:-4} -drive file=$IMG,format=raw,if=virtio,cache=unsafe $QEMU_EXTRA" \
	WAITFOR='mkself: (done|failed:)' WAITMAX=${WAITMAX:-28800} CMDWAIT=5 \
	bash "$ROOT/sys/src/build/host/guest/boot-cli.sh" "CC=$SELFCC mkself $*" > "$LOG" 2>&1 || true
tr -d '\r' < "$LOG" > "$LOG.txt"

# the tree as the guest left it back to $S (chown fails, not root)
rm -rf "$S.new"
mkdir -p "$S.new"
debugfs -R "rdump / $S.new" "$IMG" 2>&1 | grep -vE '^debugfs [0-9]|changing ownership' | grep . && die "debugfs rdump $IMG"
rm -rf "$S.new/lost+found" "$S"
mv "$S.new" "$S"
grep -qE '^mkself: (done|failed:)' "$LOG.txt" || { tail -30 "$LOG.txt"; die "did not finish (log: $LOG.txt)"; }
grep -E '^mkself: status|files expected' "$LOG.txt"
# the whole build ends with check-install's verdict; single manifests
# with the build tool's status (empty: none failed)
if [ $# -eq 0 ]; then
	grep -qE 'files expected, ok' "$LOG.txt"
else
	grep -qx 'mkself: status *' "$LOG.txt"
fi || { grep -B5 -E 'exit status|failed' "$LOG.txt" | tail -40; die "build failed (log: $LOG.txt)"; }
echo "ok: the system built itself ($S)"

# the same code as the build on Linux: every object of the tree, without
# debug information, .comment (the compilers' version strings), notes
# and unwind tables (the Linux gcc for i386 makes them, i686-pc-plan9
# not), symbols no relocation needs
STRIP="--strip-unneeded -R .comment -R .note.gnu.property -R .eh_frame -R .rel.eh_frame -R .rela.eh_frame"
same=0 diff=0
: > "$CLI_DIR/self.$A.diff"
while IFS= read -r o; do
	[ -f "$ROOT/$o" ] || continue
	objcopy $STRIP "$S/$o" "$T/a.o" 2>/dev/null || continue
	objcopy $STRIP "$ROOT/$o" "$T/b.o" 2>/dev/null || continue
	if cmp -s "$T/a.o" "$T/b.o"; then
		same=$((same+1))
	else
		diff=$((diff+1))
		echo "$o" >> "$CLI_DIR/self.$A.diff"
	fi
done < <(cd "$S" && find sys/src acme -name '*.o' | sort)
echo "objects as on Linux: $same, different: $diff ($CLI_DIR/self.$A.diff)"
