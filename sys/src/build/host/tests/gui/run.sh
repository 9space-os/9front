#!/bin/bash
# gui: terminal boot with graphics (sys/src/build/host/guest/boot-cli.sh, service=terminal,
# vgasize/monitor/mouseport as plan9.ini would set them): screenrc runs
# aux/realemu and aux/vga -m vesa, the profile starts rio with riostart
# (desktop=rio: wsys is the default where it is installed; sys/src/build/host/tests/ui has it).
# The command is typed into rio's window; it writes its output to the
# kernel console ('#c/cons', the serial line) so it can be checked, once
# the programs the profile starts are there (/tests/guiready waits for
# them, not a fixed pause). Typed input reaches rio's window slowly
# (kbdfs), so the line is short.
. "$(dirname "$0")/../lib.sh"
[ -f "$ROOT/amd64/bin/9pc64" ] || { skip "gui" "no kernel, run $BUILDHOW"; done_tests; exit; }

mkdir -p "$TMPD/root/tests"
cat > "$TMPD/root/tests/guiready" <<'EOF2'
#!/bin/rc
# until rio, stats, plumber and webfs run (60 s at most), then the mode
# and the processes on the console
# (rc has no break: the rounds after it is there do nothing)
ready=0
for(i in `{seq 60}){
	if(~ $ready 0){
		if(~ `{ps | awk '$NF ~ /^(rio|stats|plumber|webfs)$/ {print $NF}' | sort -u | wc -l} 4)
			ready=1
		if not
			sleep 1
	}
}
{grep size /dev/vgactl; ps} > '#c/cons'
EOF2
chmod 755 "$TMPD/root/tests/guiready"
boot(){
	SERVICE=terminal KARGS="vgasize=1024x768x16 monitor=vesa mouseport=ps2 desktop=rio" \
	SCREENSHOT="$TMPD/screen.png" CMDWAIT=${CMDWAIT:-30} UFS_EXTRA="$TMPD/root" \
		"$ROOT/sys/src/build/host/guest/boot-cli.sh" \
		/tests/guiready \
		> "$TMPD/console" 2>&1
	tr -d '\r' < "$TMPD/console" > "$TMPD/log"
}
check "terminal boot" boot || { done_tests; exit; }
L=$TMPD/log

check "vesa mode 1024x768x16" grep -q 'size 1024x768x16' "$L"
for p in rio stats plumber webfs; do
	check "$p running" grep -qE " $p\$" "$L"
done
check "screen dump is 1024x768" python3 -c "
import struct, sys
d = open('$TMPD/screen.png', 'rb').read(24)
sys.exit(struct.unpack('>II', d[16:24]) != (1024, 768))"
check "no kernel panic" bash -c "! grep -q '^panic:' '$L'"
check "no suicide" bash -c "! grep -q 'suicide' '$L'"
[ -z "$GUI_SCREENSHOT" ] || cp "$TMPD/screen.png" "$GUI_SCREENSHOT"
done_tests
