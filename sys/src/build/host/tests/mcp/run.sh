#!/bin/bash
# mcp: the MCP server on the install CD (/$objtype/bin/9front-mcp, tools/mcp;
# dist/bin/mkcd -mcp, stage-dist.sh MCP=1): initialize, an rc command through it, its
# deny list refusing fshalt (sys/src/build/host/tests/mcp/mcp.py).
# Skips a CD without it. Uses ISO, else the last CD sys/src/build/host/guest/mkiso.sh made.
# INSTANCE=1: the instance (dist/bin/instance; sys/src/build/host/tests/lib.sh cdimage).
. "$(dirname "$0")/../lib.sh"
cdimage "$CLI_DIR/9front.$objtype.iso" || { skip "mcp" "no CD $IMG, run dist/bin/mkcd -mcp ... or set ISO"; done_tests; exit; }

python3 "$ROOT/sys/src/build/host/tests/mcp/mcp.py" "$IMG" "$TMPD/vm" $objtype > "$TMPD/results" 2>&1
while read -r r name; do
	case $r in
	ok)	pass "$name" ;;
	fail)	fail "$name" ;;
	skip)	skip "mcp" "$name" ;;
	*)	fail "mcp.py: $r $name" ;;
	esac
done < "$TMPD/results"
grep -q . "$TMPD/results" || fail "mcp.py printed nothing"
done_tests
