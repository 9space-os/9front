"""
A 9front CD booted in QEMU the way a user does, for the tests that need
the running system (sys/src/build/host/tests/rio, usb, workflow, gcc, go, rust, mcp): every
boot prompt answered with its default (Enter) until the desktop's grey
screen shows (rio's, or wsys' that draws the same), then an rc on
the serial line ('#t/eia0') as a way in that keeps working when rio
does not. Keys and mouse go through QMP like a user's.

	vm = VM(iso, work, objtype='amd64', extra=['-device', 'qemu-xhci,id=usb'])
	vm.boot()                      # up to rio, serial rc running
	vm.type("echo hi >'#t/eia0'\\n")
	vm.serial('ps -a')             # what the serial rc prints
	vm.close()

Environment: KVM=0 (no KVM), CPU, SMP, MEM (QEMU's -cpu, -smp, -m;
defaults qemu64 or qemu32, 1, 1024).

machine(image, ...) is the same for a test that may run on the instance
(dist/bin/instance: an installed 9space in QEMU, INSTANCE_DIR of
dist/local.env): image 'instance' (sys/src/build/host/tests/lib.sh's cdimage with INSTANCE=1)
gives an Instance, which brings it up to date with the build, connects
to its QMP and serial sockets and leaves it running at the end; any other
image a VM booting that CD. A test works in vm.workdir(name), emptied
first, and reports with vm.result(ok, name); on the instance, which
stays, close removes the directory when no check failed.
"""
import json, os, re, socket, subprocess, sys, tempfile, time

KEYS = {' ': 'spc', '-': 'minus', '/': 'slash', '.': 'dot', '\n': 'ret', "'": 'apostrophe',
	';': 'semicolon', '=': 'equal', ',': 'comma', '#': ('shift', '3'), '>': ('shift', 'dot'),
	'<': ('shift', 'comma'), '&': ('shift', '7'), '[': 'bracket_left', ']': 'bracket_right',
	'|': ('shift', 'backslash'), '*': ('shift', '8'), '{': ('shift', 'bracket_left'),
	'}': ('shift', 'bracket_right'), '!': ('shift', '1'), '$': ('shift', '4'), '_': ('shift', 'minus'),
	'(': ('shift', '9'), ')': ('shift', '0'), '\\': 'backslash', '^': ('shift', '6'),
	'"': ('shift', 'apostrophe'), ':': ('shift', 'semicolon'), '+': ('shift', 'equal'),
	'~': ('shift', 'grave_accent'), '`': 'grave_accent', '?': ('shift', 'slash'), '%': ('shift', '5'),
	'@': ('shift', '2'), '\t': 'tab'}

class VM:
	def __init__(self, iso, work, objtype='amd64', extra=(), net='user,id=n0'):
		"""net: QEMU's -netdev (e.g. with hostfwd=tcp:127.0.0.1:PORT-:PORT)"""
		self.work = work
		os.makedirs(work, exist_ok=True)
		# unix socket paths are limited to 108 bytes
		self.sock = tempfile.mkdtemp(prefix='vm')
		qsock, ssock = os.path.join(self.sock, 'qmp'), os.path.join(self.sock, 'ser')
		q = ['qemu-system-i386' if objtype == '386' else 'qemu-system-x86_64',
			'-m', os.environ.get('MEM', '1024'), '-smp', os.environ.get('SMP', '1'),
			'-cpu', os.environ.get('CPU', 'qemu32' if objtype == '386' else 'qemu64'),
			'-display', 'none', '-no-reboot',
			'-qmp', 'unix:%s,server,nowait' % qsock,
			'-chardev', 'socket,id=ser,path=%s,server=on,wait=off' % ssock, '-serial', 'chardev:ser',
			'-netdev', net, '-device', 'e1000,netdev=n0',
			'-drive', 'file=%s,format=raw,if=ide,index=2,media=cdrom,readonly=on' % iso]
		if os.environ.get('KVM', '1') != '0':	# KVM always (KVM=0: emulation)
			if not os.access('/dev/kvm', os.W_OK):
				raise SystemExit('vm: /dev/kvm not writable (KVM=0 to emulate)')
			q += ['-accel', 'kvm']
		self.vm = subprocess.Popen(q + list(extra), stdout=subprocess.DEVNULL,
			stderr=open(os.path.join(work, 'qemu.log'), 'w'))
		self.q = self._connect(qsock).makefile('rw')
		self.q.readline()
		self.cmd({'execute': 'qmp_capabilities'})
		self.ser = self._connect(ssock)
		self.ser.setblocking(False)
		self.log = open(os.path.join(work, 'serial.log'), 'ab')

	def _connect(self, path):
		for _ in range(200):
			try:
				s = socket.socket(socket.AF_UNIX)
				s.connect(path)
				return s
			except OSError:
				if self.vm is not None and self.vm.poll() is not None:
					sys.exit('qemu exited, see %s/qemu.log' % self.work)
				time.sleep(0.1)
		sys.exit('no socket ' + path)

	ok = True	# no result failed (result)
	wd = None	# the test's own directory (workdir)

	def result(self, ok, name):
		"""one check's line, ok|fail name; a failed one keeps the work
		directory (close)"""
		print('%s %s' % ('ok' if ok else 'fail', name), flush=True)
		if not ok:
			self.ok = False
		return ok

	def workdir(self, name, path=None):
		"""the test's own directory, /tmp/test.NAME (or path), emptied first
		(an earlier run's files would make its mkdir or cargo new fail); the
		serial rc goes there. On a machine that stays (Instance) close removes
		it when no check failed, else it is kept to look at"""
		self.wd = path or '/tmp/test.' + name
		self.serial('rm -rf %s; mkdir -p %s && cd %s' % (self.wd, self.wd, self.wd), 3)
		return self.wd

	def cmd(self, c):
		self.q.write(json.dumps(c) + '\n')
		self.q.flush()
		while True:
			r = json.loads(self.q.readline())
			if 'return' in r or 'error' in r:
				return r

	def type(self, text):
		for ch in text:
			k = ('shift', ch.lower()) if ch.isupper() else KEYS.get(ch, ch)
			keys = k if isinstance(k, tuple) else (k,)
			r = self.cmd({'execute': 'send-key', 'arguments': {'keys': [{'type': 'qcode', 'data': x} for x in keys]}})
			if 'error' in r:
				sys.exit('send-key %r: %s' % (ch, r['error']['desc']))
			time.sleep(0.03)

	def _ev(self, *e):
		self.cmd({'execute': 'input-send-event', 'arguments': {'events': list(e)}})

	def move(self, dx, dy, step=3, pause=0.005):
		"""relative mouse motion, in pieces of at most step: the kernel
		accelerates steps of more than 3 (devmouse.c scale()), so small
		ones land where they are meant to"""
		while dx or dy:
			sx, sy = max(-step, min(step, dx)), max(-step, min(step, dy))
			self._ev({'type': 'rel', 'data': {'axis': 'x', 'value': sx}},
				{'type': 'rel', 'data': {'axis': 'y', 'value': sy}})
			dx -= sx
			dy -= sy
			time.sleep(pause)

	def goto(self, x, y):
		"""the mouse to x, y on the screen: into the top left corner
		(overshoot is clamped), then there in unaccelerated steps"""
		self.move(-3000, -3000, step=100)
		self.move(x, y)

	def click(self, x, y, b='left'):
		self.goto(x, y)
		self.button(b, True)
		self.button(b, False)

	def button(self, b, down):
		"""b: left, middle, right"""
		self._ev({'type': 'btn', 'data': {'down': down, 'button': b}})
		time.sleep(0.15)

	def shot(self, name):
		f = os.path.join(self.work, name + '.ppm')
		self.cmd({'execute': 'screendump', 'arguments': {'filename': f}})
		time.sleep(0.5)
		return f

	def rio_up(self):
		"""rio's grey background on most of the screen"""
		try:
			d = open(self.shot('screen'), 'rb').read()
			_, w, h, _, px = d.split(maxsplit=4)
		except (OSError, ValueError):
			return False
		n = grey = 0
		for i in range(0, len(px) - 3, 3 * 997):
			n += 1
			grey += max(abs(px[i] - 0x76), abs(px[i+1] - 0x76), abs(px[i+2] - 0x76)) < 6
		return n > 0 and grey > n // 2

	def inkernel(self):
		r = self.cmd({'execute': 'human-monitor-command', 'arguments': {'command-line': 'info registers'}})
		reg = r.get('return', '')
		return 'RIP=ffffffff8' in reg or 'EIP=f0' in reg

	def boot(self, wait=300):
		"""to rio, taking every prompt's default; then the serial rc"""
		end = time.time() + wait
		while time.time() < end and not self.inkernel():
			time.sleep(1)
		while time.time() < end and not self.rio_up():
			self.type('\n')
			time.sleep(4)
		if not self.rio_up():
			return False
		time.sleep(3)	# riostart's windows
		# click into the first window: keyboard focus, as a user would
		self.move(-3000, -3000, step=100)	# corner: overshoot is clamped
		self.move(200, 250)
		self.button('left', True)
		self.button('left', False)
		time.sleep(0.5)
		self.type("rc -i <'#t/eia0' >'#t/eia0' >[2=1] &\n")
		time.sleep(2)
		return 'serial-ok' in self.serial('echo serial-ok', 3)

	def read(self, secs):
		out = b''
		end = time.time() + secs
		while time.time() < end:
			try:
				d = self.ser.recv(65536)
				if d:
					out += d
					self.log.write(d)
					self.log.flush()
					continue
			except BlockingIOError:
				pass
			time.sleep(0.1)
		return out.decode(errors='replace').replace('\r', '')

	def serial(self, line, secs=4):
		"""a line to the serial rc; what it prints within secs"""
		self.read(0.2)
		self.ser.send(line.encode() + b'\n')
		return self.read(secs)

	def close(self):
		if self.vm.poll() is None:
			self.vm.kill()
			self.vm.wait()
		for f in os.listdir(self.sock):
			os.remove(os.path.join(self.sock, f))
		os.rmdir(self.sock)


class Instance(VM):
	"""the instance (dist/bin/instance) as a VM: running and up to date
	with the build (instance up: it boots only when it does not run, and
	only a new build reboots it); it keeps running after the test. A test
	tidies up its own traces (workdir before, close after when no check
	failed). net's hostfwd= rules are added to its network for the test
	(hostfwd_add, removed at close), extra is refused: its devices are its
	own"""
	def __init__(self, work, objtype='amd64', extra=(), net=None):
		sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'lib'))
		import paths
		if extra:
			sys.exit('vm: the instance has its own devices (%s)' % ' '.join(extra))
		if objtype != 'amd64':
			sys.exit('vm: the instance is amd64')
		self.work = work
		os.makedirs(work, exist_ok=True)
		d = paths.get('INSTANCE_DIR')
		if not d:
			sys.exit('vm: no INSTANCE_DIR (dist/local.env)')
		inst = os.path.join(paths.get('DIST'), 'bin', 'instance')
		self.inst = inst
		self.dir = d
		self.mcppy = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'guest', 'mcp.py')
		self.mcpport = '18090'
		envf = os.path.join(d, 'instance.env')	# its MCP_PORT, if it has another
		if os.path.exists(envf):
			for line in open(envf):
				if line.startswith('MCP_PORT='):
					self.mcpport = line.strip().split('=', 1)[1]
		r = subprocess.run([inst, 'up'], stdout=open(os.path.join(work, 'instance.log'), 'w'), stderr=subprocess.STDOUT)
		if r.returncode != 0:
			sys.exit('vm: instance up failed, see %s/instance.log' % work)
		self.vm = None
		self.sock = None
		self.q = self._connect(os.path.join(d, 'qmp.sock')).makefile('rw')
		self.q.readline()
		self.cmd({'execute': 'qmp_capabilities'})
		self.fwd = []
		for rule in (net or '').split(','):
			if rule.startswith('hostfwd='):
				self.fwd.append(rule[len('hostfwd='):])
				self.hmp('hostfwd_add n0 ' + self.fwd[-1])
		self.ser = self._connect(os.path.join(d, 'serial.sock'))
		self.ser.setblocking(False)
		self.log = open(os.path.join(work, 'serial.log'), 'ab')

	def hmp(self, line):
		return self.cmd({'execute': 'human-monitor-command', 'arguments': {'command-line': line}}).get('return', '')

	def mcp(self, command, timeout=30):
		"""a command through the instance's MCP server (sys/src/build/host/guest/mcp.py)"""
		env = dict(os.environ, MCPURL='http://127.0.0.1:%s/mcp' % self.mcpport)
		tok = os.path.join(self.dir, 'mcptoken')
		if os.path.exists(tok):
			env['MCPTOKEN'] = open(tok).read().strip()
		return subprocess.run(['python3', self.mcppy, 'rc', command, str(timeout)], env=env,
			capture_output=True, text=True, timeout=timeout + 60).stdout

	def boot(self, wait=300):
		"""its desktop is up (the profile's); then a serial rc of this test's
		own in a window of its own, which wsys makes (window(1) through its
		/srv entry, from the MCP server): the desktop's namespace (/mnt/wsys,
		$wsys), nothing an earlier test bound or set, and its own note group.
		Not typed into a window: one that stays from test to test has its
		insertion point wherever it was left, and a line typed there never
		reaches its rc. Every rc on eia0 is stopped first (an earlier test's
		with whatever it left running, a profile's outside the desktop): two
		would share the line, each taking some of the lines"""
		end = time.time() + wait
		pidf = os.path.join(self.dir, 'serial.pid')	# its pid and window
		prev = open(pidf).read().split() if os.path.exists(pidf) else []
		if prev and prev[0].isdigit():
			self.mcp("if(grep -s eia0 /proc/%s/fd >/dev/null) echo kill >/proc/%s/notepg" % (prev[0], prev[0]))
		if len(prev) > 1 and prev[1].isdigit():	# its window stays without it: deleted
			self.mcp("rfork n; mount /srv/wsys.$user /mnt/wsys && echo delete >/mnt/wsys/%s/wctl" % prev[1])
		self.mcp('for(p in /proc/[0-9]*) if(grep -s eia0 $p/fd >/dev/null) echo kill >$p/ctl')
		# the desktop is up when wsys serves (its /srv entry), not by its grey:
		# an earlier test's windows may cover it (kept when that test failed)
		while time.time() < end and 'up' not in self.mcp('test -e /srv/wsys.$user && echo up'):
			time.sleep(2)
		self.mcp("rfork n; mount /srv/wsys.$user /mnt/wsys && "
			"echo '#!/bin/rc' >/tmp/serial.rc && "
			"echo 'exec rc -i </dev/eia0 >/dev/eia0 >[2=1]' >>/tmp/serial.rc && chmod +x /tmp/serial.rc && "
			"wctl=/mnt/wsys/wctl window -r 0 600 420 790 /tmp/serial.rc")
		while time.time() < end:
			m = re.search(r'serial-ok (\d+) *(\d*)', self.serial('echo serial-ok $pid `{cat /dev/winid}', 3))
			if m is not None:
				open(pidf, 'w').write('%s %s\n' % (m.group(1), m.group(2)))
				return True
		return False

	def close(self):
		"""the instance keeps running; the test's directory goes if no check
		failed (kept to look at otherwise), its forwards go"""
		if self.ok and self.wd:
			self.serial('cd /; rm -rf ' + self.wd, 5)
		for rule in self.fwd:
			self.hmp('hostfwd_remove n0 ' + rule.rsplit('-', 1)[0])	# tcp:127.0.0.1:PORT
		self.ser.close()
		self.q.close()


def machine(image, work, objtype='amd64', extra=(), net='user,id=n0'):
	"""a VM booting the CD image, or the instance for image 'instance'"""
	if image == 'instance':
		return Instance(work, objtype, extra, net)
	return VM(image, work, objtype, extra, net)
