#!/usr/bin/env python3
"""soak.py [-m minutes] [-r seconds] [-w wlanaddr] [-H host] [-n]: the
laptop's working day as a test (tests/hw/soak.rc) on a machine booted by
PXE (scripts/hw/pxe.sh), started over MCP and watched from here.

It puts tests/hw/soak.rc and tests/gpu's vkcube into the PXE root
($CLI_DIR/pxe/ufsroot: /sys/lib/hwtest/soak.rc, /tests/vkcube), starts
soak there in a note group of its own and then, until it ends, every
minute: pings the machine (-H, its Ethernet address, and -w, its WLAN
one), reads soak's log straight from the root (the machine writes it to
/usr/glenda/soak/log on the host's ufs), and once a round fetches 50 MB
from the WLAN address (port 5001) and sends 50 MB to it (5002).  The
machine counts as hung when its log has not grown for three rounds and
neither address answers, or when MCP and the log both stand still for
three rounds; then it stops (a reboot is the user's call).

Out: $CLI_DIR/soak/<date>/ with host.log (this one's lines), log and
kmesg (the machine's), summary.txt.  -n: only say what it would do.

env: MCPURL (default http://H:8090/mcp), MCPTOKEN (default the PXE
root's token, $CLI_DIR/pxe/mcptoken)."""
import argparse, os, shutil, socket, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'scripts', 'lib'))
import paths

CLI = paths.get('CLI_DIR')
PXE = os.path.join(CLI, 'pxe')
UFS = os.path.join(PXE, 'ufsroot')
MCP = os.path.join(ROOT, 'scripts', 'guest', 'mcp.py')

ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
ap.add_argument('-m', type=int, default=480, help='minutes (default 480)')
ap.add_argument('-r', type=int, default=120, help="a round's seconds (default 120)")
ap.add_argument('-w', default='192.168.1.14', help='the WLAN address ("" for none)')
ap.add_argument('-H', default='192.168.1.223', help='the Ethernet address (MCP)')
ap.add_argument('-n', action='store_true', help='only say what it would do')
a = ap.parse_args()

env = dict(os.environ)
env.setdefault('MCPURL', 'http://%s:8090/mcp' % a.H)
if 'MCPTOKEN' not in env:
	with open(os.path.join(PXE, 'mcptoken')) as f:
		env['MCPTOKEN'] = f.read().strip()

out = os.path.join(CLI, 'soak', time.strftime('%Y%m%d-%H%M'))
mlog = os.path.join(UFS, 'usr', 'glenda', 'soak', 'log')
period = a.r + 60		# a round: its programs, ten seconds of settling, ten idle, checks

def say(s):
	line = '%s %s' % (time.strftime('%H:%M:%S'), s)
	print(line, flush=True)
	if not a.n:
		with open(os.path.join(out, 'host.log'), 'a') as f:
			f.write(line + '\n')

def mcp(script, secs=30):
	"""rc script on the machine; its output, or None if MCP did not answer"""
	try:
		r = subprocess.run([sys.executable, MCP, 'rc', '-', str(secs)], input=script,
			capture_output=True, text=True, env=env, timeout=secs + 15)
	except subprocess.TimeoutExpired:
		return None
	return r.stdout if r.returncode == 0 else None

def ping(addr):
	return addr != '' and subprocess.run(['ping', '-c', '1', '-W', '2', addr],
		stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0

def fetch(addr, port, secs=60):
	"""bytes read from addr!port until it closes, and the seconds"""
	t0 = time.time()
	n = 0
	try:
		with socket.create_connection((addr, port), timeout=10) as s:
			s.settimeout(secs)
			while True:
				b = s.recv(65536)
				if not b:
					break
				n += len(b)
	except OSError as e:
		return n, time.time() - t0, str(e)
	return n, time.time() - t0, None

def send(addr, port, size=50 << 20, secs=60):
	t0 = time.time()
	buf = b'\0' * 65536
	n = 0
	try:
		with socket.create_connection((addr, port), timeout=10) as s:
			s.settimeout(secs)
			while n < size:
				s.sendall(buf)
				n += len(buf)
	except OSError as e:
		return n, time.time() - t0, str(e)
	return n, time.time() - t0, None

def stage():
	"""soak.rc and vkcube into the PXE root, only where they differ"""
	for src, dst in ((os.path.join(ROOT, 'tests', 'hw', 'soak.rc'), 'sys/lib/hwtest/soak.rc'),
			(os.path.join(ROOT, 'tests', 'gpu', 'root', 'tests', 'vkcube'), 'tests/vkcube')):
		t = os.path.join(UFS, dst)
		if not os.path.exists(src):
			sys.exit('soak: no %s (build tests/gpu)' % src)
		if os.path.exists(t) and open(t, 'rb').read() == open(src, 'rb').read():
			continue
		say('stage %s -> %s' % (os.path.relpath(src, ROOT), dst))
		if not a.n:
			os.makedirs(os.path.dirname(t), exist_ok=True)
			shutil.copy(src, t + '.new')
			os.chmod(t + '.new', 0o755)
			os.replace(t + '.new', t)

def logtail(path, since):
	try:
		with open(path) as f:
			lines = f.readlines()
	except OSError:
		return [], since
	return lines[since:], len(lines)

if not a.n:
	os.makedirs(out)
say('soak: %d minutes, rounds of %d s, machine %s, WLAN %s, out %s' % (a.m, a.r, a.H, a.w or '-', out))
stage()
if a.n:
	sys.exit(0)

wflag = '-w ' + a.w if a.w else ''
# soak in a note group and namespace of its own, none of MCP's files
# open in it (MCP waits for whoever holds its output); whether it began
# is read from its log in the root
if os.path.exists(mlog):
	os.remove(mlog)
mcp('''@{rfork ns; rc /sys/lib/hwtest/soak.rc -m %d -r %d %s} </dev/null >/usr/glenda/soak.out >[2=1] &
''' % (a.m, a.r, wflag), 20)
t0 = time.time()
while time.time() - t0 < 30 and not (os.path.exists(mlog) and 'start soak' in open(mlog).read()):
	time.sleep(2)
if not (os.path.exists(mlog) and 'start soak' in open(mlog).read()):
	say('FAIL soak did not start (no log line in %s)' % mlog)
	sys.exit(1)
say('started: ' + open(mlog).read().splitlines()[0])

seen = 0
lastgrow = time.time()
lastnet = 0
deadline = time.time() + a.m * 60 + 2 * period + 120
nets = []
hung = None
fails = 0
done = None
while time.time() < deadline:
	time.sleep(60)
	new, seen = logtail(mlog, seen)
	if new:
		lastgrow = time.time()
	for l in new:
		l = l.rstrip('\n')
		if ' FAIL ' in l:
			fails += 1
			say('machine: ' + l)
		elif ' done soak' in l:
			done = l
			say('machine: ' + l)
		elif ' state:' in l or ' kmesg:' in l:
			say('machine: ' + l)
	if done:
		break
	eth, wl = ping(a.H), ping(a.w)
	if not eth or (a.w and not wl):
		say('ping: ethernet %s, WLAN %s' % ('up' if eth else 'DOWN', ('up' if wl else 'DOWN') if a.w else '-'))
	stale = time.time() - lastgrow
	if stale > 3 * period:
		m = mcp('echo alive', 20)
		if (not eth and not wl) or m is None:
			hung = 'no log for %d s; ethernet %s, WLAN %s, MCP %s' % (stale,
				'up' if eth else 'down', 'up' if wl else 'down', 'up' if m else 'down')
			say('HANG ' + hung)
			break
		say('note: no log for %d s, but the machine answers' % stale)
	if a.w and time.time() - lastnet > period and wl:
		lastnet = time.time()
		n1, t1, e1 = fetch(a.w, 5001)
		n2, t2, e2 = send(a.w, 5002)
		rx = n1 * 8 / t1 / 1e6 if t1 > 0 else 0
		tx = n2 * 8 / t2 / 1e6 if t2 > 0 else 0
		nets.append((rx, tx))
		if e1 or e2 or n1 < 50 << 20:
			fails += 1
			say('FAIL net: from the machine %d bytes (%s), to it %d bytes (%s)' % (n1, e1, n2, e2))
		else:
			say('net: from the machine %.1f Mbit/s, to it %.1f Mbit/s' % (rx, tx))

for f in ('log', 'kmesg'):
	p = os.path.join(UFS, 'usr', 'glenda', 'soak', f)
	if os.path.exists(p):
		shutil.copy(p, os.path.join(out, f))
with open(mlog) as f:
	mlines = f.read().splitlines()
states = [l for l in mlines if ' state:' in l]
mem = [l.split('mem ')[1].split()[0] for l in states if 'mem ' in l]
rounds = sum(1 for l in mlines if 'programs alive at the end' in l or ' gone at the end' in l)
summary = ['soak %s: %d minutes asked, %d rounds logged' % (os.path.basename(out), a.m, rounds),
	'result: %s' % ('HANG: ' + hung if hung else (done or 'no end line (deadline)')),
	'failures seen: %d (machine and network)' % fails]
if mem:
	summary.append('memory in use (user pages): first %s, last %s' % (mem[0], mem[-1]))
if nets:
	summary.append('WLAN: %d transfers, from the machine %.1f-%.1f Mbit/s, to it %.1f-%.1f' % (len(nets),
		min(x[0] for x in nets), max(x[0] for x in nets), min(x[1] for x in nets), max(x[1] for x in nets)))
with open(os.path.join(out, 'summary.txt'), 'w') as f:
	f.write('\n'.join(summary) + '\n')
for l in summary:
	say(l)
sys.exit(0 if not hung and done and fails == 0 else 1)
