/*
 * SPDX-License-Identifier: MIT
 *
 * uibig9p srv: 9P spoken to ui(4)'s srv entry directly, not through a
 * mount (the kernel's mount driver keeps msize at 32 KB and its header,
 * devmnt.c MAXRPC), to a ui of its own nobody mounts (the srv entry is
 * one pipe: a mount on it would share it): Tversion with msize 131072,
 * then one Twrite of ui's whole iounit (64 KB) of empty lines to ctl (the
 * file ui takes commands in; tree is read only) in
 * one write.  The message is
 * bigger than a pipe keeps whole (qio.c's Maxatomic, 64 KB), so ui reads
 * it in two; it must take messages by their size (the ninep crate's
 * Frames) and answer it (Rwrite, or Rerror: an answer all the same), then
 * the next request, in step.  "ok ..." or
 * "FAIL ...".  An alarm ends it after 20 s: a test does not hang on it.
 */
#include <u.h>
#include <libc.h>
#include <fcall.h>

static int fd;
static uchar *buf;
static uint bufsize = 131072;

static int
rpc(Fcall *t, Fcall *r, char *what, int errok)
{
	int n;

	n = convS2M(t, buf, bufsize);
	if(n == 0){
		print("FAIL %s: convS2M\n", what);
		return -1;
	}
	if(write(fd, buf, n) != n){
		print("FAIL %s: write %d: %r\n", what, n);
		return -1;
	}
	if((n = read9pmsg(fd, buf, bufsize)) <= 0){
		print("FAIL %s: no answer: %r\n", what);
		return -1;
	}
	if(convM2S(buf, n, r) != n){
		print("FAIL %s: answer of %d bytes not 9P\n", what, n);
		return -1;
	}
	if(r->tag != t->tag){
		print("FAIL %s: answer tag %d, not %d (out of step)\n", what, r->tag, t->tag);
		return -1;
	}
	if(r->type == Rerror){
		if(errok)
			return 0;
		print("FAIL %s: %s\n", what, r->ename);
		return -1;
	}
	if(r->type != t->type+1){
		print("FAIL %s: answer type %d\n", what, r->type);
		return -1;
	}
	return 0;
}

void
main(int argc, char **argv)
{
	Fcall t, r;
	char *names[1] = { "ctl" };
	uchar *data;
	uint count;

	if(argc != 2){
		fprint(2, "usage: uibig9p srv\n");
		exits("usage");
	}
	alarm(20*1000);
	if((fd = open(argv[1], ORDWR)) < 0)
		sysfatal("%s: %r", argv[1]);
	buf = malloc(bufsize);
	if(buf == nil)
		sysfatal("malloc: %r");

	t.type = Tversion;
	t.tag = NOTAG;
	t.msize = bufsize;
	t.version = "9P2000";
	if(rpc(&t, &r, "Tversion", 0) < 0)
		exits("version");
	bufsize = r.msize;
	count = bufsize - IOHDRSZ;

	t.type = Tattach;
	t.tag = 1;
	t.fid = 1;
	t.afid = NOFID;
	t.uname = getuser();
	t.aname = "";
	if(rpc(&t, &r, "Tattach", 0) < 0)
		exits("attach");

	t.type = Twalk;
	t.tag = 2;
	t.fid = 1;
	t.newfid = 2;
	t.nwname = 1;
	memmove(t.wname, names, sizeof names);
	if(rpc(&t, &r, "Twalk", 0) < 0)
		exits("walk");

	t.type = Topen;
	t.tag = 3;
	t.fid = 2;
	t.mode = OWRITE;
	if(rpc(&t, &r, "Topen", 0) < 0)
		exits("open");

	/* empty lines (nothing to do for ui): the whole iounit, one write */
	data = malloc(count);
	if(data == nil)
		sysfatal("malloc: %r");
	memset(data, '\n', count);
	t.type = Twrite;
	t.tag = 4;
	t.fid = 2;
	t.offset = 0;
	t.count = count;
	t.data = (char*)data;
	if(rpc(&t, &r, "Twrite", 1) < 0)
		exits("write");
	print("ok Twrite of %d bytes (a 9P message of %d, more than a pipe keeps whole) answered: %s\n",
		count, count + IOHDRSZ - 1, r.type == Rerror ? r.ename : "Rwrite");

	t.type = Tclunk;
	t.tag = 5;
	t.fid = 2;
	if(rpc(&t, &r, "Tclunk", 0) < 0)
		exits("clunk");
	print("ok the connection in step after it\n");
	exits(nil);
}
