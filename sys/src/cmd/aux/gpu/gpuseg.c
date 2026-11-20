/*
 * SPDX-License-Identifier: MIT
 *
 * gpuseg: a GPU driver's #g segments (segment(3)), the one place for
 * how they go with the driver; included by aux/virtiogpu
 * (virtio/virtiogpu.c) and by the LinuxKPI (sys/src/linux/kpi/plan9/
 * p9dev.c: aux/i915, aux/amdgpu).  Needs <u.h> and <libc.h>.
 *
 * The rule: a view (a segment that owns no pages: alias, phys, a pager
 * and its queue; a client's window onto memory the driver has
 * elsewhere) is made ORCLOSE always, so that it goes with the driver
 * however it ends, lost if so marked (segment(3)), and no process keeps
 * seeing the driver's memory, which the next driver hands out again.  A
 * segment that owns its pages (sticky, fixed: memory the device reads
 * and writes) is made ORCLOSE only once the driver armed the device's
 * reset at its exit (gpu(3) onexit; segarmed), else its pages could go
 * back to the allocator while the device still uses them.
 *
 *	segcreate(name, perm, view)	make #g/name: 1 made here, 0 there
 *					already, -1 an error
 *	segmode(name, perm)		its mode (devsegment gives 0660)
 *	segrm(name)			remove it: close its ORCLOSE file,
 *					else by name
 *	segarmed()			the reset at exit is armed: the
 *					page-owning segments made from now on
 *					go with us too
 *	segcount()			the ORCLOSE segments open now (each
 *					holds a file: MAXNFD, portdat.h)
 */

typedef struct Segfd Segfd;
struct Segfd {
	char	*name;
	int	fd;
};
static Segfd *segfds;
static int nsegfds, maxsegfds;
static Lock segfdlk;
static int segorclose;	/* the page-owning segments too (segarmed) */

static int
segmode(const char *name, ulong perm)
{
	char path[96];
	Dir d;

	snprint(path, sizeof path, "#g/%s/ctl", name);
	nulldir(&d);
	d.mode = perm & 0777;
	return dirwstat(path, &d);
}

static int
segcreate(const char *name, ulong perm, int view)
{
	char path[96];
	Segfd *t;
	int fd, orclose;

	orclose = segorclose || view;
	snprint(path, sizeof path, "#g/%s", name);
	if((fd = create(path, OREAD|OCEXEC|(orclose ? ORCLOSE : 0), DMDIR|perm)) < 0)
		return access(path, AEXIST) == 0 ? 0 : -1;
	if(!orclose){
		close(fd);
		return 1;
	}
	lock(&segfdlk);
	if(nsegfds == maxsegfds){
		t = realloc(segfds, (maxsegfds + 64) * sizeof *t);
		if(t == nil){
			unlock(&segfdlk);
			close(fd);	/* ORCLOSE: gone again */
			return -1;
		}
		segfds = t;
		maxsegfds += 64;
	}
	if((segfds[nsegfds].name = strdup((char*)name)) == nil){
		unlock(&segfdlk);
		close(fd);
		return -1;
	}
	segfds[nsegfds].fd = fd;
	nsegfds++;
	unlock(&segfdlk);
	return 1;
}

static void
segrm(const char *name)
{
	char path[96];
	int i, fd;

	fd = -1;
	lock(&segfdlk);
	for(i = 0; i < nsegfds; i++)
		if(strcmp(segfds[i].name, (char*)name) == 0){
			fd = segfds[i].fd;
			free(segfds[i].name);
			segfds[i] = segfds[--nsegfds];
			break;
		}
	unlock(&segfdlk);
	if(fd >= 0){
		close(fd);	/* ORCLOSE: removed */
		return;
	}
	snprint(path, sizeof path, "#g/%s", name);
	remove(path);
}

static void
segarmed(void)
{
	segorclose = 1;
}

static int
segcount(void)
{
	return nsegfds;
}
