#!/bin/bash
# acpi: ACPI's events (#P/acpiev, aux/acpi's acpievents) in QEMU (i440fx,
# PIIX4's ACPI): QEMU's power button (QMP system_powerdown: PM1's fixed
# button), a PCI device hot-plugged (QMP device_add: GPE 1, \_GPE._E01,
# its Notify), and an SSDT of our own (ssdt.py) whose methods notify a
# display output (0x86: brightness up) and a lid, evaluated through
# aux/acpi's aml file, the display's levels for aux/acpi's backlight
# (_BCL, _BCM, _BQC) and aux/acpikeys, a touchpad on I2C for aux/i2chid
# to find, and two batteries (_BIX, _BIF) and an AC adapter for aux/acpi's
# files.
# amd64 only (#P/acpiev is pc's).
. "$(dirname "$0")/../lib.sh"
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$objtype" = amd64 ] || { skip "acpi" "objtype=$objtype"; done_tests; exit; }
[ -f "$ROOT/amd64/9pc64" ] || { skip "acpi" "no kernel, run $BUILDHOW"; done_tests; exit; }

python3 "$HERE/ssdt.py" "$TMPD/ev.aml"
sock=$TMPD/qmp.sock
rm -f "$CLI_DIR/console.log"	# (the helper below waits for this boot's line, not the last one's)
# the host's part: once the guest reads acpievents, the button and the device
(
	end=$((SECONDS + 600))
	until tr -d '\r' < "$CLI_DIR/console.log" 2>/dev/null | grep -q '^acpitest: ready'; do
		[ $SECONDS -lt $end ] || exit 1
		sleep 1
	done
	python3 - "$sock" <<'PY'
import json, socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); f = s.makefile('rw'); f.readline()
def cmd(c, **a):
	f.write(json.dumps({'execute': c, 'arguments': a} if a else {'execute': c}) + '\n'); f.flush()
	while True:
		r = json.loads(f.readline())
		if 'event' not in r:
			return r
cmd('qmp_capabilities')
cmd('system_powerdown')
time.sleep(2)
print(cmd('device_add', driver='e1000', id='nfhp'))
PY
) > "$TMPD/host.log" 2>&1 &
host=$!

acpiboot(){
	QEMU_EXTRA="-acpitable file=$TMPD/ev.aml -qmp unix:$sock,server,nowait" CMDWAIT=40 \
	bash "$ROOT/scripts/guest/boot-cli.sh" \
		"aux/acpi" \
		"ls /dev/acpievents; {cat /dev/acpievents >/tmp/acpiev} &" \
		"echo acpitest: ready; sleep 10" \
		"echo -n 'eval \\NFLI 0' >/dev/aml; sleep 1" \
		"echo backlight \`{cat /dev/backlight}; echo 10 >/dev/backlight; echo backlight \`{cat /dev/backlight}" \
		"echo 10 >/dev/backlight; echo backlight \`{cat /dev/backlight}; echo 40 >/dev/backlight" \
		"aux/acpikeys & sleep 1; echo -n 'eval \\NFBU 0' >/dev/aml; sleep 2; echo backlight \`{cat /dev/backlight}" \
		"aux/i2chid -d >[2=1] | sed 's/^/i2chid: /'" \
		"sed 's/^/battery: /' /dev/battery; sed 's/^/ac: /' /dev/ac" \
		"sed 's/^/acpiev: /' /tmp/acpiev; echo acpitest: done" > "$TMPD/boot" 2>&1
	tr -d '\r' < "$CLI_DIR/console.log" > "$TMPD/console"
	grep -q '^acpitest: done' "$TMPD/console" || { tail -20 "$TMPD/console"; false; }
}
check "boot, aux/acpi, its acpievents read while QEMU sends its events" acpiboot
wait $host 2>/dev/null
grep '^acpiev: ' "$TMPD/console" | sed 's/^/       /'
check "the power button (PM1): button power" grep -q '^acpiev: button power$' "$TMPD/console"
check "a PCI device hot-plugged: GPE 1, \\_GPE._E01's Notify" grep -qE '^acpiev: notify \\_SB\.PCI0\.[A-Z0-9_.]+ 0x1$' "$TMPD/console"
check "a display output's Notify 0x86: key brightnessup" grep -q '^acpiev: key brightnessup$' "$TMPD/console"
check "a lid's Notify 0x80: lid open (_LID)" grep -q '^acpiev: lid open$' "$TMPD/console"
grep '^backlight ' "$TMPD/console" | sed 's/^/       /'
check "aux/acpi's backlight (_BCL, _BQC): 40 of 100 at the start" grep -q '^backlight 40 100$' "$TMPD/console"
check "10 written: level 10 through _BCM" bash -c "grep '^backlight ' '$TMPD/console' | sed -n 2p | grep -q '^backlight 10 100\$'"
check "10 again at the lowest level: stays 10" bash -c "grep '^backlight ' '$TMPD/console' | sed -n 3p | grep -q '^backlight 10 100\$'"
check "aux/acpikeys, no GPU driver: brightnessup moves the firmware's level 40 to 70" bash -c "grep '^backlight ' '$TMPD/console' | sed -n 4p | grep -q '^backlight 70 100\$'"
grep '^i2chid: ' "$TMPD/console" | sed 's/^/       /'
check "aux/i2chid: PNP0C50's bus, address, GpioInt from _CRS" grep -qF 'i2chid: aux/i2chid: \_SB.NFTP: i2cb address 0x15, gpio pin 9 level low' "$TMPD/console"
grep '^battery: \|^ac: ' "$TMPD/console" | sed 's/^/       /'
check "a battery with _BIX only (ACPI 4.0): 75 %, mW, 3 h left, discharging" grep -q '^battery: 75 mW 30000 40000 50000 2000 1000 mV 11800 11550 03:00:00 discharging 0 100$' "$TMPD/console"
check "a battery with _BIF only: 50 %, mA, idle (neither charging nor discharging)" grep -q '^battery: 50 mA 2000 4000 5000 400 200 mV 12100 12000 00:00:00 idle 0 100$' "$TMPD/console"
check "an AC adapter (ACPI0003, _PSR): online" grep -q '^ac: online$' "$TMPD/console"
done_tests
