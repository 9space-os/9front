#!/bin/bash
# workflow: the reference workflow (dist/docs/roadmap.md, phase 1) on the
# CD's desktop, keys and mouse from the host (tests/workflow/workflow.py):
# term, sam, paint (draw(3)), uitodo (ui(4)), foot (Wayland, wlsys), the
# session's files over 9P from the host; at the end all of them running,
# nothing Broken. Needs a CD of the profile ui or full (wsys): ISO, else
# the last one dist/bin/mkcd made of them, INSTANCE=1 the instance
# (dist/bin/instance; tests/lib.sh cdimage); skipped on rio.
. "$(dirname "$0")/../lib.sh"
[ "$objtype" = amd64 ] || { skip "workflow" "objtype=$objtype (wsys' desktop: amd64)"; done_tests; exit; }
newest=
for f in "$CLI_DIR"/9space-ui.$objtype.iso "$CLI_DIR"/9space-full*.$objtype.iso; do
	[ -f "$f" ] && { [ -z "$newest" ] || [ "$f" -nt "$newest" ]; } && newest=$f
done
cdimage "$newest" || { skip "workflow" "no CD with wsys: dist/bin/mkcd ui, or set ISO"; done_tests; exit; }
P9=$PLAN9PORT/bin/9p
[ -x "$P9" ] || { skip "workflow" "no plan9port 9p ($P9): its ./INSTALL"; done_tests; exit; }
echo "  CD: $IMG"

python3 "$ROOT/tests/workflow/workflow.py" "$IMG" "$TMPD/vm" $objtype "$P9" > "$TMPD/results" 2>&1
while read -r r name; do
	case $r in
	\#)	echo "  $name" ;;
	ok)	pass "$name" ;;
	fail)	fail "$name" ;;
	skip)	skip "$name" "" ;;
	*)	fail "workflow.py: $r $name" ;;
	esac
done < "$TMPD/results"
grep -q . "$TMPD/results" || fail "workflow.py printed nothing"
done_tests
