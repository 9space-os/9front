#!/bin/bash
# wlan: etherwlan (an 802.11 interface whose radio is a program, #ω) with
# wifi.c over a simulated radio and access points (wlansim.c: nfront on
# channel 6, nfront5 on 36 (5 GHz), nfrontwpa on 100 (5 GHz, radar
# detection) with WPA2-PSK; beacons, probe, authentication, association,
# the 4-way handshake, CCMP, ARP and ICMP echo for 10.0.9.1): scan,
# association, ipconfig and ping on each, aux/wpa for the third.
. "$(dirname "$0")/../lib.sh"
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$objtype" = amd64 ] || { skip "wlan" "objtype=$objtype"; done_tests; exit; }
[ -f "$ROOT/amd64/9pc64" ] || { skip "wlan" "no kernel, run $BUILDHOW"; done_tests; exit; }

buildtests(){ (cd "$HERE" && env PLAN9="$ROOT" ARCH=$objtype CC=gcc "$BUILDEXE" build.json); }
check "build tests/wlan (wlansim)" buildtests || { done_tests; exit; }

rm -f "$CLI_DIR/console.log"
# (rc) until ether1 is associated on channel $ch, at most 30 s; its ifstats with prefix $pfx
waitassoc="for(i in \`{seq 30}){ if(grep -s 'status: associated' /net/ether1/ifstats && grep -s 'channel: '\$ch /net/ether1/ifstats) i=done; if not sleep 1 }; sed 's/^/'\$pfx'stat: /' /net/ether1/ifstats"
wlanboot(){
	# (KARGS: a word a plan9.ini line, so ether1's options come by ctl: the
	# radio's address as aux/rtw89 sets it, the essid as wifi(4) says)
	UFS_EXTRA="$HERE/root" CMDWAIT=60 KARGS="ether1=type=wlan" \
	bash "$ROOT/scripts/guest/boot-cli.sh" \
		"bind -a '#ω' /dev; ls /dev/wlan1; echo ea 020000000002 >/dev/wlan1/ctl" \
		"bind -a '#l1' /net; echo essid nfront >/net/ether1/clone; cat /net/ether1/addr; echo" \
		"/tests/wlansim -d /dev/wlan1 >[2]/tmp/sim.log &" \
		"pfx=wlan; ch=06; $waitassoc" \
		"ip/ipconfig -g 10.0.9.1 ether /net/ether1 10.0.9.2 255.255.255.0 >[2=1] | sed 's/^/wlanping: /'; ip/ping -n 3 10.0.9.1 >[2=1] | sed 's/^/wlanping: /'" \
		"echo essid nfront5 >/net/ether1/clone; pfx=wlan5; ch=36; $waitassoc" \
		"ip/ping -n 3 10.0.9.1 >[2=1] | sed 's/^/wlan5ping: /'; grep 'rxba|replays' /net/ether1/ifstats | sed 's/^/wlan5ba: /'" \
		"echo 'key proto=wpapsk essid=nfrontwpa !password=nfront-secret' >/mnt/factotum/ctl; aux/wpa -s nfrontwpa /net/ether1; pfx=wlanwpa; ch=100; $waitassoc" \
		"ip/ping -n 3 10.0.9.1 >[2=1] | sed 's/^/wlanwpaping: /'; grep 'rxba|replays' /net/ether1/ifstats | sed 's/^/wlanwpaba: /'" \
		"sed 's/^/wlansim: /' /tmp/sim.log; cat /dev/wlan1/ctl | sed 's/^/wlanctl: /'; echo wlantest: done" > "$TMPD/boot" 2>&1
	tr -d '\r' < "$CLI_DIR/console.log" > "$TMPD/console"
	grep -q '^wlantest: done' "$TMPD/console" || { tail -20 "$TMPD/console"; false; }
}
check "boot with ether1=type=wlan, wlansim as its radio" wlanboot
grep '^wlan\(stat\|5stat\|wpastat\|sim\|ping\|5ping\|wpaping\|ctl\): ' "$TMPD/console" | grep -v '^wlan[a-z0-9]*stat: node: ' | sed 's/^/       /'
C=$TMPD/console
check "the radio's device: #ω/wlan1" grep -q 'wlan1/data\|/dev/wlan1' "$C"
check "the station's address by the radio's ctl: 020000000002" grep -q '^020000000002' "$C"
check "wifi.c found the access point and associated" grep -q '^wlanstat: status: associated' "$C"
check "the access point saw authentication and association" grep -q 'wlansim: wlansim: nfront: associated 020000000002' "$C"
check "ping 10.0.9.1 through it: echoes answered" bash -c "grep '^wlanping: ' '$C' | grep -q 'rtt\|time'"
# 5 GHz: nfront5 on 36 (no DS parameter set: the HT operation's channel)
check "5 GHz: the scan found nfront5, associated on channel 36" bash -c "grep -q '^wlan5stat: status: associated' '$C' && grep -q '^wlan5stat: channel: 36' '$C' && grep -q 'wlansim: wlansim: nfront5: associated 020000000002 on channel 36' '$C'"
check "5 GHz: ping through nfront5, no echo lost" bash -c "grep '^wlan5ping: ' '$C' | grep -q 'rtt\|time' && ! grep -q '^wlan5ping: .*lost' '$C'"
check "5 GHz: no 802.11b rates (association request, frames sent)" bash -c "! grep -q '802.11b rate' '$C'"
check "channels with radar detection (52-144): no probe requests" bash -c "! grep -q 'radar detection' '$C'"
check "the radio's capabilities in its ctl: HT (caps 2d1a...)" grep -q '^wlanctl: caps 2d1a' "$C"
check "WMM: nfront5 associated with WMM and the radio's HT capabilities" grep -q 'wlansim: wlansim: nfront5: associated 020000000002 on channel 36, with WMM, HT capabilities' "$C"
check "ifstats: nfront5 offers wmm and ht, our data is QoS (qos: wmm)" bash -c "grep -q '^wlan5stat: offers: wmm ht' '$C' && grep -q '^wlan5stat: qos: wmm' '$C' && grep -q '^wlanstat: qos: none' '$C'"
check "WMM: QoS data (TID 0), no frame of the wrong kind (nfront without WMM)" bash -c "grep -q 'wlansim: wlansim: nfront5: QoS data from 020000000002, TID 0' '$C' && ! grep -q 'wrong: .*data from' '$C'"
# WPA2-PSK: nfrontwpa on 100 (passive scan; its beacons tell no channel), aux/wpa
check "A-MPDU: nfront5 asked for a block ack agreement, the station agreed (TID 0, no A-MSDU)" grep -q 'wlansim: wlansim: nfront5: addba response from 020000000002: status 0 tid 0 size 32$' "$C"
check "A-MPDU: the station asked for one itself (TID 0, size 64)" grep -q 'wlansim: wlansim: nfront5: addba request from 020000000002: tid 0 size 64' "$C"
check "A-MPDU: frames out of order held, a hole's wait expired, ours agreed (ifstats)" bash -c "grep '^wlan5ba: rxba: tid 0 win 32' '$C' | grep -qv 'held 0 ' && grep '^wlan5ba: rxba' '$C' | grep -qv 'expired 0$' && grep -q '^wlan5stat: txba: 0x1' '$C'"
check "WPA2: aux/wpa's 4-way handshake with nfrontwpa on channel 100" bash -c "grep -q 'nfrontwpa: 4-way handshake: message 4 from 020000000002, mic ok: keys installed' '$C' && grep -q '^wlanwpastat: status: associated' '$C' && grep -q '^wlanwpastat: channel: 100' '$C'"
check "WPA2: the keys, pairwise and group: ccmp" bash -c "grep -q '^wlanwpastat: txkey0: ccmp:\[16\]' '$C' && grep -q '^wlanwpastat: rxkey4: ccmp:\[16\]' '$C' && grep -q '^wlanwpastat: rxkey1: ccmp:\[16\]' '$C'"
check "WPA2 with WMM: nfrontwpa associated with WMM (CCMP of QoS data: the TID)" grep -q 'wlansim: wlansim: nfrontwpa: associated 020000000002 on channel 100, with WMM' "$C"
check "A-MSDU: an echo reply in one, its subframe taken out (no echo lost)" bash -c "grep -q 'wlansim: wlansim: nfront5: an echo reply as an A-MSDU' '$C' && grep -q '^wlan5ping: 0: rtt' '$C'"
check "A-MPDU with CCMP: the older packet numbers reordered, not dropped as replays" bash -c "grep '^wlanwpaba: rxba: tid 0' '$C' | grep -qv 'held 0 ' && grep -q '^wlanwpaba: replays: 0$' '$C'"
check "WPA2: ping through nfrontwpa, ciphered with CCMP" bash -c "grep '^wlanwpaping: ' '$C' | grep -q 'rtt\|time' && ! grep -q '^wlanwpaping: .*lost' '$C' && grep -q 'nfrontwpa: echo from 10.0.9.2 (ccmp)' '$C'"

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
