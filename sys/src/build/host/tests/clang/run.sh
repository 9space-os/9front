#!/bin/bash
# clang: clang and lld on the install CD (/$objtype/clang, from toolchain/clang
# (CLANG_DIR in paths.env) via sys/src/build/host/guest/boot/clangproto, mkcd -dev
# clang): its version, an APE program, a native Plan 9 program and Plan 9 C's
# unnamed members (-fplan9-extensions), C++ with libc++ (iostream, exceptions),
# the other machines' programs with objtype= (ELF of that machine), and a
# clang program linked with an object of the CD's gcc
# (sys/src/build/host/tests/clang/clang.py). Skips a CD without clang.
# Uses ISO, else the last CD sys/src/build/host/guest/mkiso.sh made; INSTANCE=1: the
# instance (dist/bin/instance; sys/src/build/host/tests/lib.sh cdimage).
. "$(dirname "$0")/../lib.sh"
cdimage "$CLI_DIR/9front.$objtype.iso" || { skip "clang" "no CD $IMG, run DEV=clang objtype=$objtype sys/src/build/host/guest/mkiso.sh or set ISO"; done_tests; exit; }
# the version the CD's clang must say: its resource directory's name in CLANG_OUT
# (default CLANG_DIR/out/$objtype), lib/clang/N
v=$(basename "$(ls -d "${CLANG_OUT:-$CLANG_DIR/out/$objtype}/$objtype/clang/lib/clang/"* 2>/dev/null | head -1)")

python3 "$ROOT/sys/src/build/host/tests/clang/clang.py" "$IMG" "$TMPD/vm" $objtype $v > "$TMPD/results" 2>&1
while read -r r name; do
	case $r in
	ok)	pass "$name" ;;
	fail)	fail "$name" ;;
	skip)	skip "clang" "$name" ;;
	*)	fail "clang.py: $r $name" ;;
	esac
done < "$TMPD/results"
grep -q . "$TMPD/results" || fail "clang.py printed nothing"
done_tests
