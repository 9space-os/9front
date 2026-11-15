#!/bin/bash
# wlan: etherwlan (an 802.11 interface whose radio is a program, #ω) with
# wifi.c over a simulated radio and open access point (wlansim.c: essid
# nfront on channel 6, beacons, probe, authentication, association, ARP
# and ICMP echo for 10.0.9.1): scan, association, ipconfig, ping.
. "$(dirname "$0")/../lib.sh"
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$objtype" = amd64 ] || { skip "wlan" "objtype=$objtype"; done_tests; exit; }
[ -f "$ROOT/amd64/9pc64" ] || { skip "wlan" "no kernel, run $BUILDHOW"; done_tests; exit; }

buildtests(){ (cd "$HERE" && env PLAN9="$ROOT" ARCH=$objtype CC=gcc "$BUILDEXE" build.json); }
check "build tests/wlan (wlansim)" buildtests || { done_tests; exit; }

rm -f "$CLI_DIR/console.log"
wlanboot(){
	# (KARGS: a word a plan9.ini line, so ether1's options come by ctl: the
	# radio's address as aux/rtw89 sets it, the essid as wifi(4) says)
	UFS_EXTRA="$HERE/root" CMDWAIT=40 KARGS="ether1=type=wlan" \
	bash "$ROOT/scripts/guest/boot-cli.sh" \
		"bind -a '#ω' /dev; ls /dev/wlan1; echo ea 020000000002 >/dev/wlan1/ctl" \
		"bind -a '#l1' /net; echo essid nfront >/net/ether1/clone; cat /net/ether1/addr; echo" \
		"/tests/wlansim -d /dev/wlan1 >[2]/tmp/sim.log &" \
		"for(i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20){ if(grep -s 'status: associated' /net/ether1/ifstats) i=done; if not sleep 1 }; sed 's/^/wlanstat: /' /net/ether1/ifstats" \
		"ip/ipconfig -g 10.0.9.1 ether /net/ether1 10.0.9.2 255.255.255.0 >[2=1] | sed 's/^/wlanping: /'; ip/ping -n 3 10.0.9.1 >[2=1] | sed 's/^/wlanping: /'" \
		"sed 's/^/wlansim: /' /tmp/sim.log; cat /dev/wlan1/ctl | sed 's/^/wlanctl: /'; echo wlantest: done" > "$TMPD/boot" 2>&1
	tr -d '\r' < "$CLI_DIR/console.log" > "$TMPD/console"
	grep -q '^wlantest: done' "$TMPD/console" || { tail -20 "$TMPD/console"; false; }
}
check "boot with ether1=type=wlan, wlansim as its radio" wlanboot
grep '^wlan\(stat\|sim\|ping\|ctl\): ' "$TMPD/console" | sed 's/^/       /'
check "the radio's device: #ω/wlan1" grep -q 'wlan1/data\|/dev/wlan1' "$TMPD/console"
check "the station's address by the radio's ctl: 020000000002" grep -q '^020000000002' "$TMPD/console"
check "wifi.c found the access point and associated" grep -q '^wlanstat: status: associated' "$TMPD/console"
check "the access point saw authentication and association" grep -q 'wlansim: wlansim: associated 020000000002' "$TMPD/console"
check "ping 10.0.9.1 through it: echoes answered" bash -c "grep '^wlanping: ' '$TMPD/console' | grep -q 'rtt\|time'"

# gpu(3)'s *userpci=: another PCI device for a user-level driver (as
# aux/rtw89's WLAN card): QEMU's pci-testdev (1b36/0005)
rm -f "$CLI_DIR/console.log"
userpciboot(){
	QEMU_EXTRA="-device pci-testdev" KARGS="*userpci=1b36/0005" CMDWAIT=20 \
	bash "$ROOT/scripts/guest/boot-cli.sh" \
		"for(d in '#G'/*) sed 's/^/userpci: /' \$d/ctl; echo userpcitest: done" > "$TMPD/boot2" 2>&1
	tr -d '\r' < "$CLI_DIR/console.log" > "$TMPD/console2"
	grep -q '^userpcitest: done' "$TMPD/console2" || { tail -20 "$TMPD/console2"; false; }
}
check "boot with *userpci=1b36/0005 and QEMU's pci-testdev" userpciboot
grep '^userpci: ' "$TMPD/console2" | sed 's/^/       /' | head -8
check "#G has it: id 1b36/0005, its bars" bash -c "grep -q '^userpci: id 1b36/0005' '$TMPD/console2' && grep -q '^userpci: bar' '$TMPD/console2'"
done_tests
