/*
 * segtest: a process's table of segments grows (9front-ng, port/segment.c):
 * far more segments than the table it starts with (a GPU client maps each
 * buffer object as one), each found by its address (seg's binary search:
 * write and read every one), /proc/n/segment lists them, a fork's child
 * has them all, detaching every other one and attaching again reuses the
 * free slots, an exec keeps them (segattach(2)).  nfront: a sticky
 * segment with dma32 (segment(3)) has its pages below 4 GB, contiguous.
 * Prints "ok ..." / "FAIL ..." lines and a summary.
 *
 * usage: segtest [n]
 */
#include <u.h>
#include <libc.h>

enum { Len = 4096 };

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

/* the lines of /proc/pid/segment */
static int
nsegs(int pid)
{
	char path[64], buf[8192];
	int fd, n, lines, i;

	snprint(path, sizeof path, "/proc/%d/segment", pid);
	if((fd = open(path, OREAD)) < 0)
		return -1;
	lines = 0;
	while((n = read(fd, buf, sizeof buf)) > 0)
		for(i = 0; i < n; i++)
			lines += buf[i] == '\n';
	close(fd);
	return lines;
}

/*
 * nfront: "va VA LEN sticky [contig] dma32": its pages (#g/name/pa)
 * below 4 GB and contiguous; aligned to the size with contig
 */
static void
dma32(char *name, uintptr va, ulong len, int contig)
{
	char path[64];
	uchar b[8];
	uvlong pa, first, align;
	ulong i;
	int fd;

	snprint(path, sizeof path, "#g/%s", name);
	if((fd = create(path, OREAD, DMDIR|0700)) < 0){
		fail("%s: create: %r", name);
		return;
	}
	close(fd);
	snprint(path, sizeof path, "#g/%s/ctl", name);
	if((fd = open(path, OWRITE)) < 0 || fprint(fd, "va %#p %#lux sticky%s dma32", va, len, contig ? " contig" : "") < 0){
		fail("%s: ctl: %r", name);
		goto out;
	}
	close(fd);
	snprint(path, sizeof path, "#g/%s/pa", name);
	if((fd = open(path, OREAD)) < 0){
		fail("%s: pa: %r", name);
		goto out;
	}
	first = 0;
	for(i = 0; i < len/4096; i++){
		if(readn(fd, b, 8) != 8){
			fail("%s: pa: short", name);
			break;
		}
		pa = (uvlong)b[0] | (uvlong)b[1]<<8 | (uvlong)b[2]<<16 | (uvlong)b[3]<<24
			| (uvlong)b[4]<<32 | (uvlong)b[5]<<40 | (uvlong)b[6]<<48 | (uvlong)b[7]<<56;
		if(i == 0)
			first = pa;
		if(pa + 4096 > 1ULL<<32 || pa != first + i*4096){
			fail("%s: page %lud at %#llux (first %#llux)", name, i, pa, first);
			break;
		}
	}
	close(fd);
	for(align = 4096; align < len && align < 2*1024*1024; align <<= 1)
		;
	if(i == len/4096){
		if(contig && (first & (align-1)) != 0)
			fail("%s: at %#llux, not aligned to %#llux", name, first, align);
		else
			ok("%s: %lud pages below 4 GB from %#llux", name, i, first);
	}
out:
	snprint(path, sizeof path, "#g/%s", name);
	remove(path);
}

/* each segment holds its number: are they all where they were */
static int
check(uchar **seg, int n, int step)
{
	int i;

	for(i = 0; i < n; i += step)
		if(*(int*)seg[i] != i || *(int*)(seg[i]+Len-sizeof(int)) != ~i)
			return i;
	return -1;
}

void
main(int argc, char **argv)
{
	uchar **seg;
	int i, n, base, bad, pid;
	Waitmsg *w;

	n = argc > 1 ? atoi(argv[1]) : 1000;
	seg = malloc(n*sizeof seg[0]);
	if(seg == nil)
		sysfatal("malloc: %r");
	base = nsegs(getpid());

	for(i = 0; i < n; i++){
		seg[i] = segattach(0, "shared", nil, Len);
		if(seg[i] == (void*)-1){
			fail("segattach %d of %d: %r", i, n);
			break;
		}
		*(int*)seg[i] = i;
		*(int*)(seg[i]+Len-sizeof(int)) = ~i;
	}
	if(i < n){
		print("segtest: %d failed\n", nfail);
		exits("failed");
	}
	ok("%d segments attached", n);
	if((bad = check(seg, n, 1)) >= 0)
		fail("segment %d at %#p does not hold %d", bad, seg[bad], bad);
	else
		ok("each found by its address");
	if(nsegs(getpid()) != base+n)
		fail("/proc/n/segment: %d lines, want %d", nsegs(getpid()), base+n);
	else
		ok("/proc/n/segment lists %d", base+n);

	/* a child has them all (fork copies the table, shared segments shared) */
	switch(pid = fork()){
	case -1:
		fail("fork: %r");
		break;
	case 0:
		if(check(seg, n, 1) >= 0 || nsegs(getpid()) != base+n)
			exits("child");
		*(int*)(seg[n-1]+Len/2) = 0x5e9;
		exits(nil);
	default:
		w = wait();
		if(w == nil || w->pid != pid || w->msg[0] != 0)
			fail("child: %s", w ? w->msg : "no wait");
		else if(*(int*)(seg[n-1]+Len/2) != 0x5e9)
			fail("child's write to the last segment not seen");
		else
			ok("a fork's child has all %d", n);
		free(w);
	}

	/* every other one detached, then attached again in the free slots */
	for(i = 0; i < n; i += 2)
		if(segdetach(seg[i]) < 0)
			fail("segdetach %d: %r", i);
	if(nsegs(getpid()) != base+n-(n+1)/2)
		fail("after detaching half: %d lines, want %d", nsegs(getpid()), base+n-(n+1)/2);
	for(i = 1; i < n; i += 2)
		if(*(int*)seg[i] != i){
			fail("segment %d lost after detaching the even ones", i);
			break;
		}
	for(i = 0; i < n; i += 2){
		seg[i] = segattach(0, "shared", nil, Len);
		if(seg[i] == (void*)-1){
			fail("segattach again %d: %r", i);
			break;
		}
		*(int*)seg[i] = i;
		*(int*)(seg[i]+Len-sizeof(int)) = ~i;
	}
	if((bad = check(seg, n, 1)) >= 0)
		fail("after attaching again: segment %d does not hold %d", bad, bad);
	else if(nsegs(getpid()) != base+n)
		fail("after attaching again: %d lines, want %d", nsegs(getpid()), base+n);
	else
		ok("half detached and attached again: %d", n);

	/* an exec keeps them (segattach(2): without SG_CEXEC), besides the new image's own */
	switch(pid = fork()){
	case -1:
		fail("fork: %r");
		break;
	case 0:
		execl("/bin/rc", "rc", "-c",
			smprint("n=`{wc -l </proc/$pid/segment}; if(test $n -ge %d) exit ''; exit $n", n), nil);
		exits("exec");
	default:
		w = wait();
		if(w == nil || w->msg[0] != 0)
			fail("after exec: %s", w ? w->msg : "no wait");
		else
			ok("exec keeps them");
		free(w);
	}

	dma32("segtest.dma32", 0x5000000000ULL, 8*1024*1024, 0);
	dma32("segtest.dma32c", 0x5001000000ULL, 8*1024*1024, 1);

	print(nfail ? "segtest: %d failed\n" : "segtest: all passed\n", nfail);
	exits(nfail ? "failed" : nil);
}
