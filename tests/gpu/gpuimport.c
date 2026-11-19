/*
 * gpuimport: one dma-buf imported twice into a context (gpu(4)'s export
 * and import, aux/i915's ctx ctl: bo, export, import, free; aux/amdgpu's
 * export takes a GEM handle, RADV's own): the second import is the same object (the same id, as Linux's
 * PRIME gives the same handle, not counted), its pages there, and one
 * free ends it.  The X1's soak (2026-11-19):
 * a second id shared the object and freeing it took the first's pages
 * (a ui server faulted).  Prints "gpuimport: ok ..." or "gpuimport:
 * FAIL ..."; "gpuimport: skip" where the driver has no bo command
 * (aux/virtiogpu's blobs).
 */
#include <u.h>
#include <libc.h>

static int failed;
static jmp_buf fault;

static void
fail(char *fmt, ...)
{
	va_list arg;
	char buf[256];

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	print("gpuimport: FAIL %s\n", buf);
	failed++;
}

/* a context: its ctl, open */
static int
newctx(void)
{
	char buf[256];
	int fd, n;

	if((fd = open("/dev/gpu/0/clone", ORDWR)) < 0)
		sysfatal("open /dev/gpu/0/clone: %r");
	if((n = read(fd, buf, sizeof buf-1)) <= 0)
		sysfatal("read clone: %r");
	buf[n] = 0;
	return fd;
}

/* a command and its reply's fields */
static int
cmd(int fd, char *reply, int nreply, char **f, int nf, char *fmt, ...)
{
	va_list arg;
	char buf[256];
	int n;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	if(write(fd, buf, strlen(buf)) < 0)
		return -1;
	if((n = pread(fd, reply, nreply-1, 0)) < 0)
		return -1;
	reply[n] = 0;
	return tokenize(reply, f, nf);
}

/* a fault while peeking: back to peek's setjmp (notejmp needs the note's Ureg) */
static void
catch(void *ur, char *msg)
{
	if(strstr(msg, "sys: trap: fault") != nil)
		notejmp(ur, fault, 1);
	noted(NDFLT);
}

/* the word at p, or -1 if reading it faults */
static vlong
peek(volatile u32int *p)
{
	if(setjmp(fault))
		return -1;
	return *p;
}

void
main(void)
{
	char r1[256], r2[256], r3[256], r4[256], *f[8], *g[8], *h[8];
	int a, b, n, id, i1, i2;
	volatile u32int *pa, *pb;

	a = newctx();
	b = newctx();
	notify(catch);

	n = cmd(a, r1, sizeof r1, f, nelem(f), "bo 65536");
	if(n < 5 || strcmp(f[0], "bo") != 0){
		print("gpuimport: skip (no bo command: %r)\n");
		exits(nil);
	}
	id = atoi(f[1]);
	if((pa = segattach(0, f[2], nil, 65536)) == (void*)-1)
		sysfatal("segattach %s: %r", f[2]);
	pa[0] = 0x9a5ace01;
	pa[16383] = 0x9a5ace02;

	if((n = cmd(a, r2, sizeof r2, f, nelem(f), "export %d", id)) < 2 || strcmp(f[0], "export") != 0)
		sysfatal("export: %r");

	if((n = cmd(b, r3, sizeof r3, g, nelem(g), "import %s", f[1])) < 5 || strcmp(g[0], "import") != 0)
		sysfatal("import: %r");
	i1 = atoi(g[1]);
	if((pb = segattach(0, g[2], nil, 65536)) == (void*)-1)
		sysfatal("segattach %s: %r", g[2]);
	if(peek(pb) != 0x9a5ace01 || peek(pb+16383) != 0x9a5ace02)
		fail("the import does not show the object's words");

	if((n = cmd(b, r4, sizeof r4, h, nelem(h), "import %s", f[1])) < 5 || strcmp(h[0], "import") != 0)
		sysfatal("second import: %r");
	i2 = atoi(h[1]);
	if(i2 != i1)
		fail("the second import is id %d, not the first's %d", i2, i1);
	else
		print("gpuimport: ok the second import is the same id (%d)\n", i1);

	/* the second import changed nothing: the pages still there */
	if(peek(pb) != 0x9a5ace01 || peek(pb+16383) != 0x9a5ace02)
		fail("after the second import the first's pages are gone");
	else
		print("gpuimport: ok after the second import the pages still there\n");

	/* one free ends it: its pages revoked (a fault) */
	if(cmd(b, r4, sizeof r4, h, nelem(h), "free %d", i1) < 0)
		fail("free %d: %r", i1);
	if(peek(pb) != -1)
		fail("after the free the import's pages are still there");
	else
		print("gpuimport: ok one free took the pages\n");
	if(cmd(b, r4, sizeof r4, h, nelem(h), "free %d", i1) >= 0)
		fail("a second free of %d was taken", i1);

	/* the exporter's object is its own */
	if(peek(pa) != 0x9a5ace01)
		fail("the exporter's object changed");
	cmd(a, r1, sizeof r1, f, nelem(f), "free %d", id);
	print("gpuimport: %d failed\n", failed);
	exits(failed ? "failed" : nil);
}
