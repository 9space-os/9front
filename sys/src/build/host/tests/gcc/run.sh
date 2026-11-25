#!/bin/bash
# gcc: the native gcc and binutils on the install CD (/$objtype/gnu, from
# ../9front-gcc via sys/src/build/host/guest/boot/gnuproto) compile, link and run a
# POSIX and a native Plan 9 program, and gccmk builds hoc from its mkfile
# (sys/src/build/host/tests/gcc/gcc.py).
# Uses ISO, else the last CD sys/src/build/host/guest/mkiso.sh made.
. "$(dirname "$0")/../lib.sh"
IMG=${ISO:-$CLI_DIR/9front.$objtype.iso}
[ -f "$IMG" ] || { skip "gcc" "no CD $IMG, run objtype=$objtype sys/src/build/host/guest/mkiso.sh or set ISO"; done_tests; exit; }

python3 "$ROOT/sys/src/build/host/tests/gcc/gcc.py" "$IMG" "$TMPD/vm" $objtype > "$TMPD/results" 2>&1
while read -r r name; do
	case $r in
	ok)	pass "$name" ;;
	fail)	fail "$name" ;;
	*)	fail "gcc.py: $r $name" ;;
	esac
done < "$TMPD/results"
grep -q . "$TMPD/results" || fail "gcc.py printed nothing"
done_tests
