#!/bin/bash
# hid: nusb/kb's hid.c (shared with aux/i2chid) compiled on the host
# (inc/: Plan 9's types) and fed a precision touchpad's reports
# (hidtest.c): the mode set, one finger's motion, taps, two fingers'
# scrolling, the pad's button, a palm, hybrid reports.
. "$(dirname "$0")/../lib.sh"
HERE=$(cd "$(dirname "$0")" && pwd)
KB=$ROOT/sys/src/cmd/nusb/kb

check "hid.c and hidtest.c compile on the host" \
	cc -std=gnu17 -fplan9-extensions -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-value \
		-I "$HERE/inc" -I "$KB" -o "$TMPD/hidtest" "$HERE/hidtest.c" "$KB/hid.c"
if [ -x "$TMPD/hidtest" ]; then
	"$TMPD/hidtest" > "$TMPD/res" 2>&1
	sed 's/^/       /' "$TMPD/res" | grep -v '^       ok' | head -40
	while IFS= read -r l; do
		case $l in
		"ok   "*) pass "${l#ok   }" ;;
		"FAIL "*) fail "${l#FAIL }" ;;
		esac
	done < "$TMPD/res"
fi
done_tests
