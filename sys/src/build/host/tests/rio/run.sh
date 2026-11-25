#!/bin/bash
# rio: mouse and keyboard in rio on the install CD (sys/src/build/host/tests/rio/rio.py):
# select in a window, type, New + sweep, type into the new window. On a
# CD whose desktop is wsys (ui, full) the same steps test wsys; the checks
# say which (rio itself: a minimal or gpu CD).
# Uses ISO, else the last CD sys/src/build/host/guest/mkiso.sh made; INSTANCE=1: the
# instance (dist/bin/instance; sys/src/build/host/tests/lib.sh cdimage).
. "$(dirname "$0")/../lib.sh"
cdimage "$CLI_DIR/9front.$objtype.iso" || { skip "rio" "no CD $IMG, run objtype=$objtype sys/src/build/host/guest/mkiso.sh or set ISO"; done_tests; exit; }

python3 "$ROOT/sys/src/build/host/tests/rio/rio.py" "$IMG" "$TMPD/vm" $objtype > "$TMPD/results" 2>&1
while read -r r name; do
	case $r in
	\#)	echo "  $name" ;;	# a note (the desktop)
	ok)	pass "$name" ;;
	fail)	fail "$name" ;;
	*)	fail "rio.py: $r $name" ;;
	esac
done < "$TMPD/results"
grep -q . "$TMPD/results" || fail "rio.py printed nothing"
done_tests
