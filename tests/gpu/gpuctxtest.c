/*
 * SPDX-License-Identifier: MIT
 *
 * gpuctxtest: the 3D side of /shr/gpu/0 on virtio-gpu (gpu(4)):
 * capsets, a virgl context with submissions and fences, a venus
 * context with a host blob mapped through the hostmem bar.
 * Prints "ok ..." / "FAIL ..." lines and a summary.
 */
#include <u.h>
#include <libc.h>
#include "gpufind.h"

static char *dev;	/* gpufind's, or the argument */
static int nfail;

static void
ok(char *fmt, ...)
{
	va_list arg;
	char buf[256];

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	print("ok %s\n", buf);
}

static void
fail(char *fmt, ...)
{
	va_list arg;
	char buf[256];

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	print("FAIL %s\n", buf);
	nfail++;
}

static int
havecap(int id)
{
	char path[64];

	snprint(path, sizeof path, "%s/caps/%d", dev, id);
	return access(path, AREAD) == 0;
}

static void
listcaps(void)
{
	char path[64], *s;
	Dir *d;
	int fd, n, i, m;
	uchar buf[8192];

	snprint(path, sizeof path, "%s/caps", dev);
	if((fd = open(path, OREAD)) < 0){
		fail("open %s: %r", path);
		return;
	}
	n = dirreadall(fd, &d);
	close(fd);
	s = smprint("");
	for(i = 0; i < n; i++){
		snprint(path, sizeof path, "%s/caps/%s", dev, d[i].name);
		if((fd = open(path, OREAD)) < 0){
			fail("open %s: %r", path);
			continue;
		}
		m = readn(fd, buf, sizeof buf);
		close(fd);
		s = smprint("%s %s:%d", s, d[i].name, m);
	}
	if(n > 0)
		ok("caps%s", s);
	else
		fail("no capsets");
	free(d);
}

/* a ctl request and its reply */
static char*
ctl(int fd, char *fmt, ...)
{
	static char buf[256];
	va_list arg;
	int n;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	if(write(fd, buf, strlen(buf)) < 0)
		return nil;
	n = pread(fd, buf, sizeof buf-1, 0);
	if(n < 0)
		return nil;
	buf[n] = 0;
	return buf;
}

typedef struct Ctx Ctx;
struct Ctx
{
	int	ctl;
	int	submit;
	int	fence;
	int	id;
	uchar	*seg;
};

static int
openctx(Ctx *c)
{
	char path[64], buf[256], *f[6];
	int n;

	snprint(path, sizeof path, "%s/clone", dev);
	if((c->ctl = open(path, ORDWR)) < 0){
		fail("open %s: %r", path);
		return -1;
	}
	n = pread(c->ctl, buf, sizeof buf-1, 0);
	if(n <= 0){
		fail("read ctx ctl: %r");
		return -1;
	}
	buf[n] = 0;
	if(tokenize(buf, f, nelem(f)) != 6 || strcmp(f[0], "ctx") != 0){
		fail("ctx ctl: %s", buf);
		return -1;
	}
	c->id = atoi(f[1]);
	c->seg = segattach(0, f[3], nil, 0);
	if(c->seg == (uchar*)-1){
		fail("segattach %s: %r", f[3]);
		return -1;
	}
	snprint(path, sizeof path, "%s/ctx/%d/submit", dev, c->id);
	if((c->submit = open(path, OWRITE)) < 0){
		fail("open %s: %r", path);
		return -1;
	}
	snprint(path, sizeof path, "%s/ctx/%d/fence", dev, c->id);
	if((c->fence = open(path, ORDWR)) < 0){
		fail("open %s: %r", path);
		return -1;
	}
	return 0;
}

static void
closectx(Ctx *c)
{
	if(c->seg != nil && c->seg != (uchar*)-1)
		segdetach(c->seg);
	close(c->ctl);
	close(c->submit);
	close(c->fence);
}

/* wait for fence n; returns the number completed */
static long
waitfence(Ctx *c, long n)
{
	char buf[64];
	int m;

	fprint(c->fence, "%ld", n);
	m = pread(c->fence, buf, sizeof buf-1, 0);
	if(m <= 0)
		return -1;
	buf[m] = 0;
	if(strstr(buf, "failed") != nil){
		fprint(2, "fence: %s", buf);
		return -2;
	}
	if(atol(buf) != n)
		fprint(2, "fence: %s", buf);
	return atol(buf);
}

static void
virgltest(int capset)
{
	Ctx c;
	char *r;
	long done;

	memset(&c, 0, sizeof c);
	if(openctx(&c) < 0)
		return;
	if((r = ctl(c.ctl, "init %d gputest", capset)) == nil){
		fail("virgl: init %d: %r", capset);
		goto Out;
	}
	ok("virgl: %s", r);
	if(fprint(c.submit, "0 0") < 0){
		fail("virgl: empty submit: %r");
		goto Out;
	}
	memset(c.seg, 0, 4);	/* VIRGL_CCMD_NOP, no payload */
	if(fprint(c.submit, "0 4") < 0){
		fail("virgl: nop submit: %r");
		goto Out;
	}
	if((done = waitfence(&c, 2)) != 2)
		fail("virgl: fence 2: got %ld", done);
	else
		ok("virgl: 2 submissions, fence 2 signalled");
Out:
	closectx(&c);
}

/*
 * a touch of a at a child's: its wait message, "" if it did not fault.
 * A fork's child, not RFMEM: the segment the view is in is inherited all
 * the same, and nothing of ours is in a child that faults (run 1 with
 * RFMEM: the parent printed nothing after the child's fault)
 */
static char*
touch(uchar *a, char *msg, int n)
{
	Waitmsg *w;
	int pid;

	switch(pid = rfork(RFPROC|RFFDG)){
	case -1:
		snprint(msg, n, "rfork: %r");
		return msg;
	case 0:
		*(volatile uchar*)a;
		_exits(nil);
	}
	msg[0] = 0;
	while((w = wait()) != nil){
		if(w->pid == pid){
			snprint(msg, n, "%s", w->msg);
			free(w);
			break;
		}
		free(w);
	}
	return msg;
}

static void
venustest(void)
{
	char *r, *f[8], seg[32], msg[ERRMAX];
	uvlong size, segsize;
	uchar *view;
	ulong *p;
	int i, res, bad;
	Ctx c;

	memset(&c, 0, sizeof c);
	if(openctx(&c) < 0)
		return;
	if((r = ctl(c.ctl, "init 4 gputest")) == nil){
		fail("venus: init: %r");
		goto Out;
	}
	ok("venus: %s", r);
	/* host3d, mappable, blob id 0: venus shared memory */
	if((r = ctl(c.ctl, "blob 2 1 0 65536")) == nil || tokenize(r, f, nelem(f)) < 3){
		fail("venus: blob: %r");
		goto Out;
	}
	res = atoi(f[1]);
	/* map id seg va size info: the blob's own segment, not the bar's (gpu(4)) */
	if((r = ctl(c.ctl, "map %d", res)) == nil || tokenize(r, f, nelem(f)) != 6){
		fail("venus: map: %r");
		goto Out;
	}
	snprint(seg, sizeof seg, "%s", f[2]);
	segsize = strtoull(f[4], nil, 0);
	size = 65536;
	if(segsize < size){
		fail("venus: map: a segment of %llud bytes for %llud", segsize, size);
		goto Out;
	}
	if((view = segattach(0, seg, nil, segsize)) == (uchar*)-1){
		fail("venus: segattach %s: %r", seg);
		goto Out;
	}
	p = (ulong*)view;
	for(i = 0; i < size/4; i++)
		p[i] = i*2654435761UL;
	bad = 0;
	for(i = 0; i < size/4; i++)
		if(p[i] != (ulong)(i*2654435761UL))
			bad++;
	if(bad)
		fail("venus: blob %d: %d words differ", res, bad);
	else
		ok("venus: blob %d mapped through its own segment %s, %llud bytes written and read", res, seg, size);
	/* only the blob: a page past its end is no other memory of the bar */
	print("venus: touching past the blob's end (a child)\n");
	if(strstr(touch(view + segsize, msg, sizeof msg), "fault") != nil)
		ok("venus: past the blob's end faults (%s)", msg);
	else
		fail("venus: past the blob's end: no fault (%s)", msg[0] ? msg : "read");
	/* how fast the processor reads host memory through the view (4 MB blob) */
	if(getenv("GPUCTX_SPEED") != nil){
		char *r2, *g[8];
		uchar *v2;
		vlong t0, t1;
		ulong sum;

		if((r2 = ctl(c.ctl, "blob 2 1 0 4194304")) != nil && tokenize(r2, g, nelem(g)) >= 3
		&& (r2 = ctl(c.ctl, "map %d", atoi(g[1]))) != nil && tokenize(r2, g, nelem(g)) == 6
		&& (v2 = segattach(0, g[2], nil, strtoull(g[4], nil, 0))) != (uchar*)-1){
			sum = 0;
			t0 = nsec();
			for(i = 0; i < 4194304; i += 8)
				sum += *(volatile uvlong*)(v2+i);
			t1 = nsec();
			print("speed: 4 MB read %s (info %s) %lld ms (%lud)\n", g[2], g[5],
				(t1-t0)/1000000, sum & 1);
			segdetach(v2);
		}
	}
	/* unmapped: the view revoked, a touch faults (a freed object is no one's) */
	if(ctl(c.ctl, "unmap %d", res) == nil){
		fail("venus: unmap: %r");
		segdetach(view);
		goto Out;
	}
	print("venus: touching the view after unmap (a child)\n");
	if(strstr(touch(view, msg, sizeof msg), "fault") != nil)
		ok("venus: the view after unmap faults (%s)", msg);
	else
		fail("venus: the view after unmap: no fault (%s)", msg[0] ? msg : "read");
	segdetach(view);
	if(ctl(c.ctl, "unref %d", res) == nil)
		fail("venus: unref: %r");
Out:
	closectx(&c);
}

void
main(void)
{
	dev = gpufind("virtio-gpu");
	listcaps();
	if(havecap(2))
		virgltest(2);
	else if(havecap(1))
		virgltest(1);
	else
		fail("no virgl capset");
	if(havecap(4))
		venustest();
	else
		print("skip venus: no capset 4\n");
	print("gpuctxtest: %s\n", nfail ? "FAIL" : "all passed");
	exits(nfail ? "fail" : nil);
}
