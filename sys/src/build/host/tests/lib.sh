# shared helpers for sys/src/build/host/tests/*/run.sh (sourced)
ROOT=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
. "$ROOT/sys/src/build/lib/paths.sh"	# GCC_DIR, RUST_DIR, GO_DIR, MCP_DIR, DISTFILES, PLAN9PORT, CACHE, CLI_DIR, ... (the distribution's paths.env)
# the architecture under test (sys/src/build/gen/arch.py): objtype, O, TOOLPREFIX,
# KERNEL (its configuration: pc64, pc, qemu), QEMU, SUFFIX (manifests);
# KBIN the ELF kernel the build leaves in /$objtype/bin
eval "$(python3 "$ROOT/sys/src/build/gen/arch.py")" || exit 1
KBIN=$objtype/bin/9$KERNEL
case $objtype in amd64) KDIR=pc64 ;; 386) KDIR=pc ;; *) KDIR=$objtype ;; esac	# sys/src/9/$KDIR
export objtype SUFFIX KERNEL KBIN KDIR
# guests with several processors: threads really run at once (races one hides)
export SMP=${SMP:-4}
# the build tool (go build in sys/src/cmd/build) and how to build everything
BUILDEXE=$ROOT/sys/src/cmd/build/build.exe
BUILDHOW="CC=gcc ARCH=$objtype sys/src/cmd/build/build.exe"
TMPD=$(mktemp -d)
# atexit command: a category's own cleanup at exit (a guest, a server), run
# before the work dir goes, in the order registered.  A category never sets
# its own EXIT trap: that would replace this one (KEEPTMP, the work dir).
__atexit=()
atexit(){ __atexit+=("$1"); }
# KEEPTMP=1: the work dir stays (its consoles, logs), its name on stderr
__exit(){
	# the category's exit status, whatever a cleanup returns: under set -e
	# a failing cleanup (a kill of a guest gone already) would change it
	local rc=$? c
	set +e
	for c in "${__atexit[@]}"; do eval "$c"; done
	if [ -n "$KEEPTMP" ]; then echo "tests: kept $TMPD" >&2; else rm -rf "$TMPD"; fi
	exit $rc
}
trap __exit EXIT
npass=0
nfail=0

pass(){ echo "  PASS $*"; npass=$((npass+1)); }
fail(){ echo "  FAIL $*"; nfail=$((nfail+1)); }
# check name command...: PASS if the command succeeds
check(){ local n=$1; shift; if "$@" > "$TMPD/out" 2>&1; then pass "$n"; else fail "$n"; sed 's/^/       /' "$TMPD/out" | head -15; return 1; fi; }
# skip name reason
skip(){ echo "  SKIP $1 ($2)"; }
# skipall name reason: the whole category skipped (a prerequisite missing,
# another objtype): not a failure for sys/src/build/host/tests/run.sh
skipall(){ skip "$1" "$2"; exit 0; }
# cdimage DEFAULT: IMG, what a test of the installed system (gcc, go, rust, mcp,
# rio, workflow) runs on: with INSTANCE=1 "instance" (dist/bin/instance: the
# installed 9space at INSTANCE_DIR of dist/local.env, brought up to date with
# the build first; sys/src/build/host/tests/vm.py's machine()), else the CD ISO, else DEFAULT.
# Fails when there is no such CD (the caller skips).
cdimage(){
	if [ "$INSTANCE" = 1 ]; then
		[ -n "$INSTANCE_DIR" ] || { echo "tests: INSTANCE=1 but no INSTANCE_DIR (dist/local.env)" >&2; exit 1; }
		IMG=instance; return 0
	fi
	IMG=${ISO:-$1}
	[ -n "$IMG" ] && [ -f "$IMG" ]
}
# deskguest LOG [first command ...]: the guest of a test that brings its own
# desktop (draw, sdl, glfw, winit, uiserver, wayland: wsys -x -i on the
# screen, keys by QMP), its MCP server at MCPURL (exported), QMP at $QMP;
# deskguest_stop ends it (registered with atexit). Takes UFS_EXTRA (laid over
# the guest's /: its tests/ the test programs), KARGS, MEM, QEMU_EXTRA as
# sys/src/build/host/guest/mcp-guest.sh, which it runs (its output into LOG),
# the first commands typed before its server starts.
# With INSTANCE=1 the instance instead (dist/bin/instance fresh: up to date,
# then a throwaway boot, QEMU_EXTRA its devices; nothing done stays): UFS_EXTRA
# laid over its / (tests/ replaced, the rest merged), its desktop ended (deskoff.rc), then a server like
# mcp-guest.sh's (no deny list, /tmp/mcp) in place of the profile's, with the
# first commands in its name space (KARGS: the screen is plan9.ini's, unless a
# first command sets another, e.g. aux/vga -l). With DESKKEEP=1 (tests/run.sh
# sets it for INSTANCE=1) deskguest_stop leaves that boot running, and the next
# category's deskguest takes it over when it asks for the same QEMU_EXTRA:
# deskoff.rc ends whatever the last one left (its server's programs, any
# wsys), then its own server starts; "deskguest_stop end" ends it in any
# case (sdl: QEMU writes its sound only when it ends; a guest of its own
# after it), and tests/run.sh at its end.
deskguest(){
	local log=$1; shift
	if [ "$INSTANCE" != 1 ]; then
		setsid bash "$ROOT/sys/src/build/host/guest/mcp-guest.sh" "$@" >"$log" 2>&1 &
		__deskguest=$!
		QMP=$CLI_DIR/qmp.sock
		atexit deskguest_stop
		return 0
	fi
	[ -n "$INSTANCE_DIR" ] || { echo "tests: INSTANCE=1 but no INSTANCE_DIR (dist/local.env)" >&2; exit 1; }
	# the instance's own settings (instance.env), not the guest's of mcp-guest.sh
	# (MEM, SMP: a console guest's, too small for a desktop system)
	local inst="env -u MEM -u SMP $DIST/bin/instance" first="" c D=$INSTANCE_DIR again=0
	atexit deskguest_stop
	QMP=$D/qmp.sock
	export MCPURL=http://127.0.0.1:$(. "$D/instance.env" 2>/dev/null; echo "${MCP_PORT:-18090}")/mcp
	MCPTOKEN=$(cat "$D/mcptoken"); export MCPTOKEN
	# an earlier category's throwaway boot, with the same devices: taken over
	if [ "$DESKKEEP" = 1 ] && [ -f "$D/qemu.desk" ] && [ -f "$D/qemu.snapshot" ] &&
		[ -s "$D/qemu.pid" ] && kill -0 "$(cat "$D/qemu.pid")" 2>/dev/null &&
		[ "$(cat "$D/qemu.extra" 2>/dev/null)" = "$QEMU_EXTRA" ] &&
		python3 "$ROOT/sys/src/build/host/guest/mcp.py" rc 'echo up' 20 >/dev/null 2>&1; then
		again=1
		echo "tests: deskguest: the throwaway boot of the category before" >"$log"
	else
		QEMU_EXTRA=$QEMU_EXTRA $inst fresh >"$log" 2>&1 || { echo "tests: instance fresh failed ($log)" >&2; return 1; }
	fi
	# UFS_EXTRA is laid over / (as mcp-guest.sh's root): its tests/ the test's
	# own (replaced), anything else merged into what the system has (fonts)
	local e
	if [ -n "$UFS_EXTRA" ]; then
		for e in "$UFS_EXTRA"/*; do
			[ -d "$e" ] || { echo "tests: deskguest: $e is not a directory (only directories are put on)" >&2; return 1; }
			if [ "$(basename "$e")" = tests ]; then $inst put "$e" /tests
			else $inst put -m "$e" "/$(basename "$e")"; fi >>"$log" 2>&1 || return 1
		done
	fi
	local M="python3 $ROOT/sys/src/build/host/guest/mcp.py"
	# the desktop; taken over: also what the category before left under its server
	{ [ $again = 1 ] && echo '*=(9front-mcp wsys rio)'; cat "$ROOT/sys/src/build/host/tests/deskoff.rc"; } |
		$M rc - 60 >>"$log" 2>&1 || { echo "tests: the instance's desktop did not end ($log)" >&2; return 1; }
	# the commands, then the server, detached: the profile's server goes first
	# (its port), this call with it; aux/realemu too (the boot's /dev/realmode
	# is its own name space's, not the server's: aux/vga -l needs one)
	for c in "$@"; do first="$first $c >[2=1];"; done
	$M rc "{rfork sF; sleep 1; for(p in \`{ps | awk '\$NF==\"9front-mcp\" {print \$2}'}) echo kill >/proc/\$p/ctl; sleep 1;$first mkdir -p /tmp/mcp; 9front-mcp -http 'tcp!*!8090' -tokenfile \$home/lib/mcptoken -dir /tmp/mcp -disks >/tmp/mcp.log >[2=1]} </dev/null >/dev/null >[2=1] &" 30 >>"$log" 2>&1 || true
	sleep 3
	$M wait 120 >>"$log" 2>&1 || { echo "tests: no MCP server in the instance after deskoff ($log)" >&2; return 1; }
	touch "$D/qemu.desk"	# a boot deskguest made: the next category may take it over
}
deskguest_stop(){	# [end]
	if [ "$INSTANCE" = 1 ]; then
		# kept for the next category (DESKKEEP), else its end: nothing stays
		[ "$1" != end ] && [ "$DESKKEEP" = 1 ] && [ -f "$INSTANCE_DIR/qemu.desk" ] && return 0
		"$DIST/bin/instance" stop >/dev/null 2>&1 || true
		unset MCPURL MCPTOKEN	# a guest of mcp-guest.sh after it: mcp.py's defaults
	elif [ -n "$__deskguest" ]; then
		kill -TERM -- -"$__deskguest" 2>/dev/null || kill "$__deskguest" 2>/dev/null
		wait "$__deskguest" 2>/dev/null || true
	fi
	__deskguest=
}
done_tests(){ echo "  -- $npass passed, $nfail failed"; [ $nfail -eq 0 ]; }
