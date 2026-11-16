/*
 * GCC port: mmap for APE, which has none (LLVM maps the files it reads and writes): the
 * memory is from posix_memalign (page aligned), a file's part read into it; a shared, writable
 * mapping goes back to the file at msync and munmap. No sharing between procs beyond the
 * program's own memory, no protection, no executable memory (PROT_EXEC is refused).
 * 9front-ng: except shared memory objects (memfd_create, shm_open): segments of
 * segment(3) at an address the kernel chooses, attached for MAP_SHARED: the same
 * memory in every process that maps one (below, after shm_open).
 * 9space: munmap gives back exactly the pages asked for (a part of a mapping too, as
 * SpiderMonkey's GC trims an over-sized mapping to an aligned chunk); before, it freed
 * the whole mapping whatever the length, memory the program still used. The memory of
 * a mapping is one block of malloc's, freed when its last part goes; the pages of a part
 * unmapped before go back to the system at once (segfree(2): the heap's pages are
 * released, the addresses stay the block's). MAP_FIXED is not honoured (addr is
 * ignored, as before).
 */
#define _POSIX_SOURCE
#define _PLAN9_SOURCE
#define _LOCK_EXTENSION
#define _RESEARCH_SOURCE
#include <sys/types.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <lock.h>
#include <stdint.h>

extern int _SEGFREE(void*, unsigned long);	/* ../plan9/sys9.h, included below */


enum { Pagesz = 4096 };	/* mmap's alignment; munmap's addresses are multiples */

/*
 * the memory of one mmap: freed (and fd closed) when no Map has a part of it and
 * no write back of a part is under way (another thread's munmap or msync)
 */
typedef struct Block Block;
struct Block {
	char	*base;
	size_t	live;	/* bytes still mapped: the sum of its Maps' len */
	int	busy;	/* write backs under way */
	int	fd;	/* a dup of the file's, if written back; else -1 */
	Block	*next;	/* in munmap's list of blocks to free */
};

/* a range still mapped: a whole mapping, or what munmap left of one */
typedef struct Map Map;
struct Map {
	char	*p;
	size_t	len;
	off_t	off;	/* the file's offset of p */
	Block	*b;
	Map	*next;
};

static Lock lk;
static Map *maps;

typedef struct Shm Shm;
static void *shmmap(int, size_t, off_t, int);
static int shmunmap(void *);
static Shm *shmfind(void *);
static int shmdata(int);

/* n bytes from p back to the file at off, if the mapping is written back */
static int
writeback(int fd, off_t off, char *p, size_t n)
{
	size_t done;
	long r;

	if(fd < 0)
		return 0;
	for(done = 0; done < n; done += r){
		r = pwrite(fd, p + done, n - done, off + done);
		if(r <= 0)
			return -1;
	}
	return 0;
}

void *
mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	Map *m;
	Block *b;
	char *p;
	size_t done;
	long r;
	int rfd;

	(void)addr;
	if(!(flags & MAP_ANONYMOUS) && (flags & MAP_SHARED) && (p = shmmap(fd, len, off, (prot & PROT_WRITE) != 0)) != NULL)
		return p;
	if(len == 0 || (prot & PROT_EXEC) || len > 0xffffffffUL){
		errno = len == 0 ? EINVAL : ENOMEM;
		return MAP_FAILED;
	}
	m = malloc(sizeof *m);
	b = malloc(sizeof *b);
	if(posix_memalign((void **)&p, Pagesz, len) != 0)
		p = NULL;
	if(m == NULL || b == NULL || p == NULL){
		free(m);
		free(b);
		free(p);
		errno = ENOMEM;
		return MAP_FAILED;
	}
	memset(p, 0, len);
	b->base = p;
	b->live = len;
	b->busy = 0;
	b->fd = -1;
	m->p = p;
	m->len = len;
	m->off = off;
	m->b = b;
	if(!(flags & MAP_ANONYMOUS)){
		/* a private mapping of a shared memory object: a copy of its bytes */
		rfd = shmdata(fd);
		for(done = 0; done < len; done += r){
			r = pread(rfd >= 0 ? rfd : fd, p + done, len - done, off + done);
			if(r < 0){
				if(rfd >= 0)
					close(rfd);
				free(p);
				free(b);
				free(m);
				return MAP_FAILED;
			}
			if(r == 0)
				break;	/* beyond the end: zeros */
		}
		if(rfd >= 0)
			close(rfd);
		if((flags & MAP_SHARED) && (prot & PROT_WRITE)){
			b->fd = dup(fd);
			if(b->fd < 0){
				free(p);
				free(b);
				free(m);
				return MAP_FAILED;
			}
		}
	}
	lock(&lk);
	m->next = maps;
	maps = m;
	unlock(&lk);
	return p;
}

/* the Map with p in it (with lk) */
static Map *
find(void *p)
{
	Map *m;

	for(m = maps; m != NULL; m = m->next)
		if((char *)p >= m->p && (char *)p < m->p + m->len)
			break;
	return m;
}

static void
freeblock(Block *b)
{
	if(b->fd >= 0)
		close(b->fd);
	free(b->base);
	free(b);
}

/* a write back of b's is done: b goes if nothing else holds it */
static void
unbusy(Block *b)
{
	int gone;

	lock(&lk);
	gone = --b->busy == 0 && b->live == 0;
	unlock(&lk);
	if(gone)
		freeblock(b);
}

/*
 * a part of a Map that munmap took away: written back (a shared file's), its pages
 * released (if the block stays), before its block may go
 */
typedef struct Cut Cut;
struct Cut {
	char	*p;
	size_t	len;
	off_t	off;
	Block	*b;
	int	release;	/* the block keeps other parts: segfree the pages */
	Cut	*next;
};

/*
 * the pages of [addr, addr+len) are unmapped, of whatever mappings there are in
 * that range (none: nothing to do); a mapping's part before or after them stays.
 * A mapping's memory is freed when its last part goes.
 */
int
munmap(void *addr, size_t len)
{
	char *lo, *hi, *a, *e;
	Map *m, *n, **l;
	Block *dead, *b;
	Cut *cuts, *c;
	int r;

	if(shmunmap(addr))
		return 0;	/* shared memory: one mmap of it less (see shmunmap) */
	if(len == 0 || ((uintptr_t)addr & (Pagesz-1)) != 0){
		errno = EINVAL;
		return -1;
	}
	a = addr;
	e = a + ((len + Pagesz-1) & ~(size_t)(Pagesz-1));
	if(e < a){
		errno = EINVAL;
		return -1;
	}
	cuts = NULL;
	dead = NULL;
	r = 0;
	lock(&lk);
	for(l = &maps; (m = *l) != NULL; ){
		lo = m->p > a ? m->p : a;
		hi = m->p + m->len < e ? m->p + m->len : e;
		if(lo >= hi){
			l = &m->next;
			continue;
		}
		b = m->b;
		if((c = malloc(sizeof *c)) != NULL){
			c->p = lo;
			c->len = hi - lo;
			c->off = m->off + (lo - m->p);
			c->b = b;
			c->release = 0;
			b->busy++;
			c->next = cuts;
			cuts = c;
		}else if(b->fd >= 0)
			r = -1;	/* not written back (the pages stay too) */
		b->live -= hi - lo;
		if(c != NULL)
			c->release = b->live != 0;
		if(lo == m->p && hi == m->p + m->len){	/* all of it */
			*l = m->next;
			free(m);
		}else if(lo == m->p){	/* its head */
			m->off += hi - m->p;
			m->len -= hi - m->p;
			m->p = hi;
			l = &m->next;
		}else if(hi == m->p + m->len){	/* its tail */
			m->len = lo - m->p;
			l = &m->next;
		}else{	/* its middle: what is after it a Map of its own */
			if((n = malloc(sizeof *n)) == NULL){
				b->live += hi - lo;	/* left mapped, as munmap fails */
				if(c != NULL)
					c->release = 0;	/* (written back all the same: harmless) */
				r = -1;
				errno = ENOMEM;
				l = &m->next;
				continue;
			}
			n->p = hi;
			n->len = m->p + m->len - hi;
			n->off = m->off + (hi - m->p);
			n->b = b;
			n->next = m->next;
			m->len = lo - m->p;
			m->next = n;
			l = &n->next;
		}
		if(b->live == 0 && b->busy == 0){
			b->next = dead;
			dead = b;
		}
	}
	unlock(&lk);
	while((c = cuts) != NULL){
		cuts = c->next;
		if(writeback(c->b->fd, c->off, c->p, c->len) < 0)
			r = -1;
		/*
		 * whole pages only: segfree zeroes the bytes of a page it covers
		 * in part, and a mapping's last page (len not a multiple) has
		 * malloc's bytes after its end
		 */
		e = (char *)((uintptr_t)(c->p + c->len) & ~(uintptr_t)(Pagesz-1));
		if(c->release && e > c->p)
			_SEGFREE(c->p, e - c->p);
		unbusy(c->b);
		free(c);
	}
	while((b = dead) != NULL){
		dead = b->next;
		freeblock(b);
	}
	return r;
}

int
msync(void *p, size_t len, int flags)
{
	Map *m;
	Block *b;
	size_t n;
	off_t off;
	int r;

	(void)flags;
	if(shmfind(p) != NULL)
		return 0;	/* shared memory: nothing to write back */
	lock(&lk);
	if((m = find(p)) == NULL){
		unlock(&lk);
		errno = ENOMEM;
		return -1;
	}
	n = m->p + m->len - (char *)p;
	off = m->off + ((char *)p - m->p);
	b = m->b;
	b->busy++;
	unlock(&lk);
	if(len < n)
		n = len;
	r = writeback(b->fd, off, p, n);
	unbusy(b);
	return r;
}

int
mprotect(void *p, size_t len, int prot)
{
	(void)p; (void)len;
	if(prot & PROT_EXEC){
		errno = EACCES;
		return -1;
	}
	return 0;
}

int
madvise(void *p, size_t len, int advice)
{
	(void)p; (void)len; (void)advice;
	return 0;
}

int
posix_madvise(void *p, size_t len, int advice)
{
	(void)p; (void)len; (void)advice;
	return 0;
}

/*
 * 9front-ng (../9front-ui PLAN §12): shared memory objects are segments of
 * segment(3) whose address the kernel chooses ("va 0 len"), so that any
 * process attaches one at that address and sees the same memory (Wayland's
 * wl_shm: a client's pool in the compositor).  The object's descriptor is
 * its directory in #g: memfd_create's is opened ORCLOSE (gone with its last
 * descriptor, in whatever process has it), shm_open's is named until
 * shm_unlink.  ftruncate (__shmtruncate) gives it its address and size, on a
 * 64-bit machine 256 MB of addresses (pages only where touched) so that it
 * can grow; mmap(MAP_SHARED) attaches it, munmap of the last such mapping
 * detaches it; a MAP_PRIVATE mapping is a copy of its bytes (its data file).
 */
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include "../plan9/lib.h"
#include "../plan9/sys9.h"

enum {
	Shmreserve = sizeof(void *) == 8 ? 256*1024*1024 : 0,
	OREAD9 = 0,
	OWRITE9 = 1,
	ORCLOSE9 = 64,
	DMDIR9 = 0x80000000,
};

struct Shm {
	char	name[128];
	char	*base;
	size_t	len;
	int	ref;	/* mmaps of it */
	int	dirfd;	/* #g/name kept open: the segment stays attachable by name */
	Shm	*next;
};

static Shm *shms;

/*
 * A program's procs (pthreads: rfork RFMEM) share the segments there were
 * when they were made, not those attached later: a segment mapped by one
 * proc faults in another (foot's render threads in its wl_shm pools).
 * _notehandler (plan9/signal.c) asks __shmfault: the proc attaches the
 * segment there too (the same address: the kernel chose it at "va 0") and
 * the instruction is done again. Segments unmapped meanwhile (their bases
 * in dead[]) the proc detaches then, so that it keeps no old pool's memory.
 */
enum { Ndead = 32, Nseen = 64 };
static char *dead[Ndead];
static unsigned long ndead;	/* bases put in dead[], ever */

/* per proc: dead[] detached up to (no __thread: libap is built by the host's gcc, whose TLS is %fs) */
static struct {
	int	pid;
	unsigned long	seen;
} seen[Nseen];

/* this proc's slot of seen[] (with lk); a new one starts at ndead */
static unsigned long *
seenof(int pid)
{
	int i, free;

	free = -1;
	for(i = 0; i < Nseen; i++){
		if(seen[i].pid == pid)
			return &seen[i].seen;
		if(seen[i].pid == 0 && free < 0)
			free = i;
	}
	if(free < 0)
		free = pid % Nseen;	/* full: one is taken over, its proc detaches again */
	seen[free].pid = pid;
	seen[free].seen = 0;
	return &seen[free].seen;
}
static int memfdn;

/*
 * A memfd's seals (Linux's F_ADD_SEALS, F_GET_SEALS) and its size as
 * ftruncate gave it (the segment reserves more addresses).  Kept in the
 * process: another that got the descriptor (SCM_RIGHTS) sees none.  One
 * made without MFD_ALLOW_SEALING is sealed against seals.
 */
typedef struct Seal Seal;
struct Seal {
	char	name[128];
	int	seals;
	off_t	size;
	Seal	*next;
};
static Seal *seals;

/* with lk */
static Seal *
sealof(char *name)
{
	Seal *s;

	for(s = seals; s != NULL; s = s->next)
		if(strcmp(s->name, name) == 0)
			return s;
	return NULL;
}

/* the segment fd is the directory of, if it is one */
static int
shmname(int fd, char *name, int n)
{
	char path[256];

	if(fd < 0 || _FD2PATH(fd, path, sizeof path) < 0)
		return 0;
	if(strncmp(path, "#g/", 3) != 0 || path[3] == 0 || strchr(path+3, '/') != NULL)
		return 0;
	snprintf(name, n, "%s", path+3);
	return 1;
}

/* its data file, opened for reading, if fd is a segment's; else -1 */
static int
shmdata(int fd)
{
	char name[128], path[160];

	if(!shmname(fd, name, sizeof name))
		return -1;
	snprintf(path, sizeof path, "#g/%s/data", name);
	return open(path, O_RDONLY);
}

/* its address and size, if it has them yet */
static int
shmva(char *name, uintptr_t *va, size_t *len)
{
	char path[160], buf[128], *e;
	int fd, n;

	snprintf(path, sizeof path, "#g/%s/ctl", name);
	if((fd = _OPEN(path, OREAD9)) < 0)
		return 0;
	n = _PREAD(fd, buf, sizeof buf-1, 0);
	_CLOSE(fd);
	if(n <= 3 || strncmp(buf, "va ", 3) != 0)
		return 0;
	buf[n] = 0;
	*va = strtoull(buf+3, &e, 0);
	*len = strtoull(e, NULL, 0);
	return 1;
}

static Shm *
shmfind(void *p)
{
	Shm *s;

	lock(&lk);
	for(s = shms; s != NULL; s = s->next)
		if((char *)p >= s->base && (char *)p < s->base + s->len)
			break;
	unlock(&lk);
	return s;
}

/* the size ftruncate gave a memfd */
static void
sealsize(char *name, off_t length)
{
	Seal *sl;

	lock(&lk);
	if((sl = sealof(name)) != NULL)
		sl->size = length;
	unlock(&lk);
}

/* fcntl's F_ADD_SEALS and F_GET_SEALS (plan9/fcntl.c): the seals, or -1 */
int
__shmseals(int fd, int cmd, int arg)
{
	char name[128];
	Seal *sl;
	int r;

	if(!shmname(fd, name, sizeof name)){
		errno = EINVAL;	/* not a memfd */
		return -1;
	}
	lock(&lk);
	if((sl = sealof(name)) == NULL){
		unlock(&lk);
		errno = EINVAL;	/* shm_open's, or one of another process */
		return -1;
	}
	if(cmd == F_GET_SEALS){
		r = sl->seals;
		unlock(&lk);
		return r;
	}
	if(sl->seals & F_SEAL_SEAL){
		unlock(&lk);
		errno = EPERM;
		return -1;
	}
	sl->seals |= arg;
	unlock(&lk);
	return 0;
}

/* ftruncate of a segment's descriptor (-2: not one) */
int
__shmtruncate(int fd, off_t length)
{
	char name[128], path[160], buf[64];
	uintptr_t va;
	size_t len, size;
	int cfd, n;

	if(!shmname(fd, name, sizeof name))
		return -2;
	{
		Seal *sl;
		int e;

		e = 0;
		lock(&lk);
		if((sl = sealof(name)) != NULL){
			if((length < sl->size && (sl->seals & F_SEAL_SHRINK)) ||
			   (length > sl->size && (sl->seals & F_SEAL_GROW)))
				e = EPERM;
		}
		unlock(&lk);
		if(e){
			errno = e;
			return -1;
		}
	}
	if(shmva(name, &va, &len)){
		if((size_t)length <= len){
			sealsize(name, length);
			return 0;
		}
		errno = EFBIG;	/* beyond what it reserved */
		return -1;
	}
	if(length == 0)
		return 0;
	size = length;
	if(size < Shmreserve)
		size = Shmreserve;
	snprintf(path, sizeof path, "#g/%s/ctl", name);
	if((cfd = _OPEN(path, OWRITE9)) < 0){
		_syserrno();
		return -1;
	}
	n = snprintf(buf, sizeof buf, "va 0 %llu", (unsigned long long)size);
	if(_PWRITE(cfd, buf, n, 0) != n){
		_syserrno();
		_CLOSE(cfd);
		return -1;
	}
	_CLOSE(cfd);
	sealsize(name, length);
	return 0;
}

static void *
shmmap(int fd, size_t len, off_t off, int writable)
{
	char name[128];
	uintptr_t va;
	size_t slen;
	Shm *s;
	void *p;

	if(!shmname(fd, name, sizeof name))
		return NULL;
	if(writable){
		Seal *sl;
		int sealed;

		lock(&lk);
		sealed = (sl = sealof(name)) != NULL && (sl->seals & (F_SEAL_WRITE|F_SEAL_FUTURE_WRITE));
		unlock(&lk);
		if(sealed){
			errno = EPERM;
			return MAP_FAILED;
		}
	}
	if(!shmva(name, &va, &slen) || off < 0 || (size_t)off > slen || len > slen - off){
		errno = ENXIO;	/* no size yet (ftruncate first), or beyond it */
		return MAP_FAILED;
	}
	lock(&lk);
	for(s = shms; s != NULL; s = s->next)
		if(strcmp(s->name, name) == 0){
			s->ref++;
			unlock(&lk);
			return s->base + off;
		}
	unlock(&lk);
	if((s = malloc(sizeof *s)) == NULL){
		errno = ENOMEM;
		return MAP_FAILED;
	}
	p = _SEGATTACH(0, name, NULL, 0);
	if(p == (void *)-1){
		_syserrno();
		free(s);
		return MAP_FAILED;
	}
	{
		char dir[160];

		snprintf(dir, sizeof dir, "#g/%s", name);
		s->dirfd = _OPEN(dir, OREAD9);
	}
	snprintf(s->name, sizeof s->name, "%s", name);
	s->base = p;
	s->len = slen;
	s->ref = 1;
	lock(&lk);
	s->next = shms;
	shms = s;
	unlock(&lk);
	return s->base + off;
}

/* munmap of a segment's mapping: 1 if it was one */
static int
shmunmap(void *p)
{
	Shm *s, **l;

	lock(&lk);
	for(l = &shms; (s = *l) != NULL; l = &s->next)
		if((char *)p >= s->base && (char *)p < s->base + s->len)
			break;
	if(s == NULL){
		unlock(&lk);
		return 0;
	}
	if(--s->ref > 0){
		unlock(&lk);
		return 1;
	}
	*l = s->next;
	dead[ndead++ % Ndead] = s->base;	/* other procs may have it attached (__shmfault) */
	*seenof(getpid()) = ndead;	/* this one detaches it here */
	unlock(&lk);
	_SEGDETACH(s->base);
	if(s->dirfd >= 0)
		_CLOSE(s->dirfd);
	free(s);
	return 1;
}

/*
 * a fault at addr in this proc: 1 if it was in a segment another proc of
 * the program mapped, now attached here too (the caller continues)
 */
int
__shmfault(unsigned long long addr)
{
	char name[128];
	char *base;
	unsigned long i, *seendead;
	Shm *s;
	void *p;

	lock(&lk);
	/* old pools' segments, unmapped since this proc last looked: not kept */
	seendead = seenof(getpid());
	if(ndead - *seendead > Ndead)
		*seendead = ndead - Ndead;
	for(i = *seendead; i < ndead; i++)
		_SEGDETACH(dead[i % Ndead]);	/* fails if not attached here */
	*seendead = ndead;
	for(s = shms; s != NULL; s = s->next)
		if(addr >= (uintptr_t)s->base && addr < (uintptr_t)s->base + s->len)
			break;
	if(s == NULL){
		unlock(&lk);
		return 0;
	}
	snprintf(name, sizeof name, "%s", s->name);
	base = s->base;
	unlock(&lk);
	p = _SEGATTACH(0, name, NULL, 0);
	if(p == (void *)-1)
		return 0;	/* attached already: a real fault */
	if(p != base){
		_SEGDETACH(p);
		return 0;
	}
	return 1;
}

/* a segment's directory as a descriptor APE knows */
/*
 * a new segment's mode: devsegment gives each 0660, as front's; another
 * by a wstat of its ctl (segment(3)); path is the segment's directory
 */
static int
segmode(const char *path, mode_t mode)
{
	char ctl[300];

	snprintf(ctl, sizeof ctl, "%s/ctl", path);
	return chmod(ctl, mode & 0777);
}

static int
shmfd(int fd, int cloexec)
{
	Fdinfo *fi;

	if(fd < 0){
		_syserrno();
		return -1;
	}
	if(fd >= OPEN_MAX){
		_CLOSE(fd);
		errno = EMFILE;
		return -1;
	}
	fi = &_fdinfo[fd];
	memset(fi, 0, sizeof *fi);
	fi->flags = FD_ISOPEN | (cloexec ? FD_CLOEXEC : 0);
	fi->oflags = O_RDWR;
	fi->uid = -2;
	fi->gid = -2;
	return fd;
}

int
memfd_create(const char *name, unsigned int flags)
{
	char path[128];
	Seal *sl;
	int n, fd;

	(void)name;
	lock(&lk);
	n = memfdn++;
	unlock(&lk);
	snprintf(path, sizeof path, "#g/ape.memfd.%d.%d", getpid(), n);
	fd = _CREATE(path, OREAD9|ORCLOSE9, DMDIR9|0600);
	if(fd >= 0 && segmode(path, 0600) < 0){
		_CLOSE(fd);	/* ORCLOSE: the segment goes with it */
		fd = -1;
	}
	fd = shmfd(fd, flags & MFD_CLOEXEC);
	if(fd >= 0 && (sl = malloc(sizeof *sl)) != NULL){
		snprintf(sl->name, sizeof sl->name, "ape.memfd.%d.%d", getpid(), n);
		sl->seals = flags & (MFD_ALLOW_SEALING|MFD_NOEXEC_SEAL) ? 0 : F_SEAL_SEAL;
		sl->size = 0;
		lock(&lk);
		sl->next = seals;
		seals = sl;
		unlock(&lk);
	}
	return fd;
}

static void
shmpath(char *buf, size_t n, const char *name)
{
	while(*name == '/')
		name++;
	snprintf(buf, n, "#g/ape.shm.%s", name);
}

int
shm_open(const char *name, int flags, mode_t mode)
{
	char path[256];
	int fd;

	shmpath(path, sizeof path, name);
	fd = _OPEN(path, OREAD9);
	if(fd >= 0 && (flags & (O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL)){
		_CLOSE(fd);
		errno = EEXIST;
		return -1;
	}
	if(fd < 0 && (flags & O_CREAT)){
		fd = _CREATE(path, OREAD9, DMDIR9|(mode&0777));
		if(fd >= 0 && segmode(path, mode) < 0){
			_CLOSE(fd);
			_REMOVE(path);
			fd = -1;
		}
	}
	return shmfd(fd, 1);
}

int
shm_unlink(const char *name)
{
	char path[256];

	shmpath(path, sizeof path, name);
	if(_REMOVE(path) < 0){
		_syserrno();
		return -1;
	}
	return 0;
}
