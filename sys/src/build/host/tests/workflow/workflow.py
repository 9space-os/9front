#!/usr/bin/env python3
"""
The reference workflow (dist/docs/roadmap.md, phase 1): a working session
on the CD's desktop as a user has it, keys and mouse from the host
through QMP, the programs of the profile ui (wsys, term, foot) and of
9front together on one boot, the work in /tmp/wf (emptied first, removed
with the session's windows when every check passed: sys/src/build/host/tests/vm.py workdir):

	term	typed into: a file written, its output on term's screen
		(TERMDEBUG=screen)
	sam	the editor: a/.../ on a new file from its command window,
		the file's window swept when samterm asks, a line typed
		into it, written (w)
	paint	a draw(3) program: a stroke drawn with button 1, the canvas
		written from its prompt (w file)
	uitodo	a client of ui(4): its text box found by nodes/id/rect,
		clicked, an item typed with enter, the list in its file
	foot	a Wayland program in wlsys(4): typed into, its rc writes a file
	9P	the session's files read from the host over TCP (exportfs
		behind aux/listen1, plan9port's 9p), one written into it
	files	the file manager (ui(4)): a file selected, renamed, plumbed;
		a directory made and gone into; a file made from outside shows

and at the end every one of them still running, no process Broken,
wsys up.  The guest's own view (window rectangles, files) comes
through an rc on the serial line; everything the user does goes
through QMP as keys and mouse.

usage: sys/src/build/host/tests/workflow/workflow.py iso workdir objtype 9p
   prints one line per check: ok|fail|skip <name>, # a note
"""
import os, re, socket, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from vm import machine

iso, work, objtype, p9p = sys.argv[1:5]

def freeport():
	s = socket.socket()
	s.bind(('127.0.0.1', 0))
	p = s.getsockname()[1]
	s.close()
	return p

GPORT = 5640	# the guest's 9P port (aux/listen1)
hport = freeport()
vm = machine(iso, work, objtype, net='user,id=n0,hostfwd=tcp:127.0.0.1:%d-:%d' % (hport, GPORT))

result = vm.result	# ok|fail lines; a failed one keeps /tmp/wf and the windows

def note(s):
	print('# ' + s, flush=True)

nseq = 0
def sh(cmd, secs=10):
	"""cmd in the serial rc; what it printed, up to its end marker"""
	global nseq
	nseq += 1
	end = '@@end%d' % nseq
	vm.read(0.1)
	vm.ser.send(('%s; echo %s\n' % (cmd, end)).encode())
	out = ''
	t = time.time() + secs
	while time.time() < t and end not in out:
		out += vm.read(0.3)
	return out.split(end)[0].strip()

def until(test, secs=15):
	"""until the rc test (a command) succeeds in the guest"""
	t = time.time() + secs
	while time.time() < t:
		if 'yes' in sh('if(%s) echo yes' % test, 5):
			return True
		time.sleep(0.5)
	return False

def wins():
	return set(sh("ls -p /mnt/wsys | grep '^[0-9]+$'").split())

def newwin(old, secs=20):
	"""the window that was not in old, once it shows; its contents' rectangle"""
	t = time.time() + secs
	while time.time() < t:
		n = wins() - old
		if n:
			w = max(n, key=int)
			r = sh('c=`{cat /mnt/wsys/%s/ctl}; echo $c(2-5)' % w).split()
			if len(r) == 4 and all(x.lstrip('-').isdigit() for x in r):
				sh('echo %s >>/tmp/wf/windows' % w)	# deleted when done (tidy)
				return w, [int(x) for x in r]
		time.sleep(0.5)
	return None, None

def window(rect, cmd):
	"""a window made by window(1), as riostart does; its number and contents"""
	old = wins()
	sh("window -r %s %s" % (rect, cmd))
	return newwin(old)

def tidy():
	"""the windows this test made (newwin's list in /tmp/wf/windows) deleted"""
	sh("if(test -e /tmp/wf/windows) for(w in `{cat /tmp/wf/windows}) echo delete >/mnt/wsys/$w/wctl >[2]/dev/null")

def key(name):
	"""one key by its QMP name (end, home, ...)"""
	vm.cmd({'execute': 'send-key', 'arguments': {'keys': [{'type': 'qcode', 'data': name}]}})
	time.sleep(0.2)

def typed(text):
	vm.type(text)
	time.sleep(0.5)

def ninep(*args, data=None):
	"""plan9port's 9p against the guest's exportfs"""
	env = dict(os.environ, PLAN9=os.path.dirname(os.path.dirname(p9p)))
	r = subprocess.run([p9p, '-n', '-a', 'tcp!127.0.0.1!%d' % hport] + list(args),
		input=data, capture_output=True, timeout=30, env=env)
	return r.returncode, r.stdout

try:
	if not result(vm.boot(), 'boot to the desktop, serial rc'):
		sys.exit(0)
	if 'desk-wsys' not in sh("if(ps | grep -s ' wsys$') echo desk-^wsys"):
		print('skip the reference workflow (the desktop is rio: a CD of the profile ui or full)', flush=True)
		sys.exit(0)
	# the serial rc: a name space and environment of its own (the first
	# window's stay as they were), wsys' tree of all windows at /mnt/wsys
	# (a window's own /mnt/wsys is its directory alone: clone, but no
	# numbered windows, which wins() lists)
	sh("rfork en; if(! ls -p /mnt/wsys | grep -s '^[0-9]+$' && ! ~ $#wsys 0) mount -c $wsys /mnt/wsys")
	result('yes' in sh("ls -p /mnt/wsys | grep -s '^[0-9]+$' && echo yes"), "wsys' tree at /mnt/wsys")
	# an earlier run's windows (its list, kept when a check failed) and
	# term's screens go first; then /tmp/wf, empty
	tidy()
	sh('rm -f /tmp/term.*.screen')
	vm.workdir('workflow', '/tmp/wf')
	user = sh('echo $user') or 'glenda'

	# term: typed into, as in a terminal
	# (TERMDEBUG=screen: term writes its screen to /tmp/term.pid.screen)
	sh("echo '#!/bin/rc' >/tmp/wf/term.rc; echo 'TERMDEBUG=screen exec term' >>/tmp/wf/term.rc; chmod +x /tmp/wf/term.rc")
	w, r = window('180 170 700 430', '/tmp/wf/term.rc')
	if result(w is not None, 'term: its window'):
		vm.click((r[0]+r[2])//2, (r[1]+r[3])//2)
		typed("cd /tmp/wf; echo plan: notes, sketch, todo >plan; cat plan\n")
		ok = until("grep -s '^plan: notes, sketch, todo$' /tmp/wf/plan")
		result(ok, 'term: a file written from what was typed')
		scr = sh('cat /tmp/term.*.screen')
		result(scr.count('plan: notes, sketch, todo') >= 2, "term: the command and its output on term's screen")

	# sam: a/.../ in the command window on the new file (under samterm
	# its first read came within that command and cancelled it: patch-upstream
	# 0040); the file has no window, so samterm asks for one to be swept
	# (button 3); a click below the text, a second line typed into it;
	# back in the command window (the top fifth), w
	w, r = window('200 190 720 530', 'sam /tmp/wf/notes')
	if result(w is not None, 'sam: its window'):
		time.sleep(2)	# samterm's first frame
		cmdat = ((r[0]+r[2])//2, r[1] + (r[3]-r[1])//10)
		vm.click(*cmdat)
		typed('a/notes from sam\\n/\n')
		time.sleep(1)
		vm.goto(r[0] + 20, r[1] + (r[3]-r[1])//4)
		vm.button('right', True)
		vm.move(r[2]-r[0]-40, (r[3]-r[1])*3//4 - 20, step=3, pause=0.003)
		vm.button('right', False)
		time.sleep(1)
		vm.click((r[0]+r[2])//2, r[3] - 40)	# below the text: its end
		typed('typed in sam\n')
		vm.shot('sam1')
		vm.click(*cmdat)
		typed('w\n')
		time.sleep(1)
		vm.shot('sam2')
		result(until("~ `{cat /tmp/wf/notes | wc -l} 2 && grep -s '^notes from sam$' /tmp/wf/notes && grep -s '^typed in sam$' /tmp/wf/notes"),
			'sam: a/.../ on the new file, its window swept, a line typed into it, written (w)')

	# paint: a stroke, the canvas written from its prompt
	w, r = window('220 210 740 580', 'paint')
	if result(w is not None, 'paint: its window'):
		time.sleep(1)
		vm.goto(r[0] + 60, r[1] + 60)
		vm.button('left', True)
		vm.move(240, 140, step=2, pause=0.01)
		vm.button('left', False)
		typed('w /tmp/wf/sketch\n')
		ok = until('test -s /tmp/wf/sketch')
		result(ok, 'paint: the canvas written (w file)')
		if ok:
			sh('iconv -u -c k8 </tmp/wf/sketch >/tmp/wf/sketch.k8')

	# uitodo: a client of ui(4); its text box found in its tree
	sh('uitodo -f /tmp/wf/todo >/tmp/wf/uitodo.log >[2=1] </dev/null &; todopid=$apid')
	srv = '/srv/ui.%s.$todopid.0' % user
	ok = until('test -e ' + srv) and 'yes' in sh('mount %s /n/uit && echo yes' % srv)
	if result(ok, 'uitodo: its window of ui(4) mounted'):
		until('grep -s textbox /n/uit/tree')
		tb = sh("grep '^new [^ ]* textbox' /n/uit/tree | sed 1q | awk '{print $2}'")
		at = sh("o=`{cat /n/uit/ctl}; r=`{cat /n/uit/nodes/%s/rect}; "
			"echo `{echo 'int('$o(1)'+('$r(1)+$r(3)')/2)' | hoc} `{echo 'int('$o(2)'+('$r(2)+$r(4)')/2)' | hoc}" % tb).split()
		if result(len(at) == 2, 'uitodo: where its text box is (nodes/%s/rect)' % tb):
			x, y = int(at[0]), int(at[1])
			vm.click(x, y)	# the first may only make the window current
			vm.click(x, y)
			typed('milk\n')
			result(until("grep -s '^0 milk$' /tmp/wf/todo"), 'uitodo: an item typed, enter: in its list')

	# foot: a Wayland program in wlsys, rc in it
	old = wins()
	sh('wlsys foot /bin/rc -i >/tmp/wf/wlsys.log >[2=1] </dev/null &')
	w, r = newwin(old, 30)
	if result(w is not None, 'foot: its window (wlsys)'):
		time.sleep(2)
		vm.click((r[0]+r[2])//2, (r[1]+r[3])//2)
		typed('echo wayland-ok >/tmp/wf/foot\n')
		result(until("grep -s '^wayland-ok$' /tmp/wf/foot"), 'foot: typed into, its rc wrote a file')
	else:
		note('wlsys: ' + sh('cat /tmp/wf/wlsys.log'))

	# 9P over the network: the session's files from the host
	if not 'yes' in sh('test -e /net/ipifc/0/local && echo yes'):
		sh('ip/ipconfig', 20)
	sh("aux/listen1 -t 'tcp!*!%d' /bin/exportfs -r /tmp/wf >[2]/dev/null &" % GPORT)
	time.sleep(1)
	rc, ls = ninep('ls')
	result(rc == 0 and b'notes' in ls.split(), '9P: the host lists /tmp/wf (exportfs, aux/listen1)')
	rc, notes = ninep('read', 'notes')
	result(rc == 0 and notes == b'notes from sam\ntyped in sam\n', "9P: the host reads sam's file")
	rc, todo = ninep('read', 'todo')
	result(rc == 0 and b'0 milk' in todo, "9P: the host reads uitodo's list")
	rc, k8 = ninep('read', 'sketch.k8')
	dark = sum(1 for b in k8[60:] if b < 0x80) if rc == 0 and k8[:12].strip() == b'k8' else 0
	result(dark > 100, "9P: paint's image read by the host, the stroke in it (%d dark pixels)" % dark)
	rc, _ = ninep('create', 'fromhost')
	if rc == 0:
		rc, _ = ninep('write', 'fromhost', data=b'written by the host\n')
	result(rc == 0 and until("grep -s '^written by the host$' /tmp/wf/fromhost"), '9P: a file written by the host')

	# files, the file manager (ui(4)): a click selects notes, a name typed
	# at the end of its rename box renames it, a second click plumbs it (a
	# reader on the edit port takes the message), a directory made, gone
	# into by two clicks; a file made there from outside shows
	old = wins()
	sh('files /tmp/wf >/tmp/wf/files.log >[2=1] </dev/null &; filespid=$apid')
	fw, _ = newwin(old)
	if fw:	# bigger, as a user makes it: /tmp/wf has more entries than its first size shows
		sh('echo resize -r 120 60 760 740 >/mnt/wsys/%s/wctl' % fw)
		time.sleep(1)
	srv = '/srv/ui.%s.$filespid.0' % user
	ok = until('test -e ' + srv) and 'yes' in sh('mount %s /n/uif && echo yes' % srv)
	if result(ok, 'files: its window of ui(4) mounted'):
		def node(text, kind='button', key='text'):
			# the node of that kind whose key (text, placeholder) is text
			q = text.replace("'", "''")
			return sh("for(n in `{grep '^set [^ ]* %s %s$' /n/uif/tree | awk '{print $2}'}) "
				"if(grep -s '^new '$n' %s ' /n/uif/tree) echo $n" % (key, q, kind)).split('\n')[0].strip()
		def where(n, end=False):
			# the middle of node n (or its right end, inside), on the screen;
			# None when that is not in the window's contents (ctl: x y width height)
			v = sh("o=`{cat /n/uif/ctl}; r=`{cat /n/uif/nodes/%s/rect}; echo $o $r" % n).split()
			try:
				o, r = [float(x) for x in v[:4]], [float(x) for x in v[4:8]]
			except ValueError:
				return None
			if len(r) < 4:
				return None
			x = o[0] + (r[2] - 6 if end else (r[0] + r[2]) / 2)
			y = o[1] + (r[1] + r[3]) / 2
			if not (o[0] <= x < o[0] + o[2] and o[1] <= y < o[1] + o[3]):
				note('files: node %s at %d,%d, outside the window %s' % (n, x, y, o))
				return None
			return int(x), int(y)
		until("grep -s 'text notes$' /n/uif/tree")
		b = node('notes')
		p = where(b) if b else None
		if result(p is not None, 'files: notes listed, a button (%s)' % b):
			vm.click(*p)	# the first may only make the window current
			# (only ASCII in what goes to the serial rc: a » there does not
			# arrive as typed; the selection's mark is any word before the name)
			if not until("grep -s 'text [^ ]+ notes$' /n/uif/tree", 3):
				vm.click(*p)
			result(until("grep -s 'text [^ ]+ notes$' /n/uif/tree"), 'files: a click selects notes')
			tb = node('notes', 'textbox')
			q = where(tb, end=True) if tb else None
			if q:
				vm.click(*q)
				key('end')	# the click leaves the cursor at the start
				typed('.txt\n')
			result(until('test -f /tmp/wf/notes.txt && ! test -e /tmp/wf/notes'), 'files: renamed to notes.txt (its rename box, enter)')
			sh('cat /mnt/plumb/edit >/tmp/wf/plumbed &')
			time.sleep(0.5)
			b = sh("grep '^set [^ ]* text [^ ]+ notes.txt$' /n/uif/tree | sed 1q | awk '{print $2}'")
			if b:
				vm.click(*where(b))
			result(until("grep -s notes.txt /tmp/wf/plumbed && grep -s '^/tmp/wf$' /tmp/wf/plumbed"),
				'files: a second click plumbs it (edit port: notes.txt from /tmp/wf)')
		mk = node('new directory', 'textbox', 'placeholder')
		q = where(mk) if mk else None
		if q:
			vm.click(*q)
			typed('archive\n')
		result(until('test -d /tmp/wf/archive') and until("grep -s 'text archive/$' /n/uif/tree"), 'files: a directory made, listed')
		b = node('archive/')
		if b:
			vm.click(*where(b))
			time.sleep(0.5)
			b = sh("grep '^set [^ ]* text [^ ]+ archive/$' /n/uif/tree | sed 1q | awk '{print $2}'")
			if b:
				vm.click(*where(b))
		result(until("grep -s 'text /tmp/wf/archive$' /n/uif/tree"), 'files: two clicks go into archive (its path in the box)')
		sh('touch /tmp/wf/archive/later')
		result(until("grep -s 'text later$' /n/uif/tree", 8), 'files: a file made from outside shows (read every 2 s)')

	# the session's programs still there, nothing Broken
	ps = sh('ps')
	for p in ('wsys', 'term', 'sam', 'paint', 'uitodo', 'foot', 'wlsys', 'files'):
		result(re.search(r' %s$' % p, ps, re.M) is not None, '%s still running' % p)
	broken = [l for l in ps.splitlines() if ' Broken ' in l]
	result(not broken, 'no process Broken' + (': ' + '; '.join(broken) if broken else ''))
	vm.shot('final')
	if vm.ok:	# all well: the session's windows go (vm.close removes /tmp/wf)
		tidy()
finally:
	vm.close()
