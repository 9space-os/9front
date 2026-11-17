#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

/*
 * Attachable segment types
 */
/*
 * gpu branch: room for #G's segments too: per GPU (Maxgpu 4) each memory
 * BAR (6) uncached, cached and write-combining, its framebuffer and
 * OpRegion; 10 entries were full with two GPUs
 */
static Physseg physseg[96] = {
	{ SG_SHARED,	"shared",	0,	SEGMAXSIZE	},
	{ SG_BSS,	"memory",	0,	SEGMAXSIZE	},
	{ 0,		0,		0,	0		},
};

static Lock physseglock;

#define IHASHSIZE	64
#define ihash(s)	imagealloc.hash[s%IHASHSIZE]
static struct Imagealloc
{
	Lock;

	QLock	ireclaim;	/* mutex on reclaiming idle images */

	ulong	pgidle;		/* pages in idle list (reclaimable) */

	ulong	nidle;
	Image	*idle;
	Image	*hash[IHASHSIZE];

}imagealloc;

Segment* (*_globalsegattach)(char*);
/* 9front-ng: a pager's segment (devsegment.c): a page's address, waiting for one, the end */
static Segment*	groupoverlap(Mgrp*, uintptr, uintptr);

int	(*_pagerpa)(Pager*, uintptr, uintptr*);
int	(*_pagerwait)(Pager*, uintptr);
void	(*_pagerfree)(Pager*);

Segment *
newseg(int type, uintptr base, ulong size)
{
	Segment *s;
	int mapsize;

	assert((base & (BY2PG-1)) == 0);

	if(size > (SEGMAPSIZE*PTEPERTAB))
		error(Enovmem);

	/*
	 * 9front-ng: a physical segment has room for a Physseg of its own
	 * (segment(3)'s phys: a part of another one, freed with it)
	 */
	s = malloc(((type & SG_TYPE) != SG_PHYSICAL)?
		sizeof(Segment)+sizeof(Pte):
		sizeof(Segment)+sizeof(Physseg));

	if(s == nil)
		error(Enomem);

	s->Ref.ref = 1;
	s->type = type;
	s->size = size;
	s->base = base;
	s->top = base+((uintptr)size*BY2PG);
	s->used = s->swapped = 0;
	s->sema.prev = &s->sema;
	s->sema.next = &s->sema;

	s->firstproc = conf.nproc;
	s->lastproc = -1;
	s->segno = -1;

	switch(type & SG_TYPE){
	case SG_TEXT:
		s->flushme = 1;
		break;
	case SG_PHYSICAL:
		s->map = nil;
		s->mapsize = 0;
		s->freepte = nil;
		return s;
	}

	/* pre-allocated Pte */
	s->freepte = (Pte*)(s+1);

	mapsize = ROUND(size, PTEPERTAB)/PTEPERTAB;
	if(mapsize > nelem(s->ssegmap)){
		s->map = malloc(mapsize*sizeof(Pte*));
		if(s->map == nil){
			free(s);
			error(Enomem);
		}
		s->mapsize = mapsize;
	}
	else{
		s->map = s->ssegmap;
		s->mapsize = nelem(s->ssegmap);
	}

	return s;
}

/*
 * 9front-ng: a process's table of segments grows.  It starts as p->seg0
 * (NSEG slots, the fixed ones SSEG to SEG4 among them); segreserve makes
 * room for more, doubling it (a GPU client maps each buffer object as a
 * segment of its own: SDL's Vulkan renderer keeps some 300, games more).
 * p->nseg bounds the slots in use, which loops over the table walk.
 * p->sorted holds the attached segments by base (they do not overlap:
 * isoverlap), for the binary search of segbyaddr and isoverlap.
 *
 * Who changes it holds p->seglock: the process itself, its parent in
 * fork, devproc, and the other processes of its memory image (Mgrp, which
 * they hold first): a segment one of them attaches or detaches is
 * attached or detached in all (segattach, syssegdetach).  The arrays
 * change under p->segtab as well (an ilock: segclock reads TSEG at clock
 * interrupt), which is what a reader without p->seglock holds:
 * procflushmmu's matchers, procpagecount, segbyaddr; a table that grew
 * is freed, as no one reads it any more once the new one is in.  A
 * segment found that way stays only while the reader holds the lock: one
 * used after it (fault, the semaphores) is taken with a reference
 * (segref), as another process may detach it meanwhile.
 */
static int
growtab(Proc *p, int n)
{
	Segment **t, **o, **ot, **oo;
	int cap;

	if(n <= p->segcap)
		return 0;
	for(cap = 2*p->segcap; cap < n; cap *= 2)
		;
	t = malloc(cap*sizeof(Segment*));
	o = malloc(cap*sizeof(Segment*));
	if(t == nil || o == nil){
		free(t);
		free(o);
		return -1;
	}
	memmove(t, p->seg, p->segcap*sizeof(Segment*));
	memmove(o, p->sorted, p->nsorted*sizeof(Segment*));
	ilock(&p->segtab);
	ot = p->seg;
	oo = p->sorted;
	p->seg = t;
	p->sorted = o;
	p->segcap = cap;
	iunlock(&p->segtab);
	if(ot != p->seg0)
		free(ot);
	if(oo != p->sorted0)
		free(oo);
	return 0;
}

void
segreserve(Proc *p, int n)
{
	if(growtab(p, n) < 0)
		error(Enovmem);
}

/* 9front-ng: p's table back to p->seg0, every segment detached (pexit) */
void
segtabreset(Proc *p)
{
	Segment **ot, **oo;

	ilock(&p->segtab);
	ot = p->seg;
	oo = p->sorted;
	memset(p->seg0, 0, sizeof(p->seg0));
	p->seg = p->seg0;
	p->sorted = p->sorted0;
	p->segcap = NSEG;
	p->nseg = NSEG;
	p->nsorted = 0;
	iunlock(&p->segtab);
	if(ot != p->seg0)
		free(ot);
	if(oo != p->sorted0)
		free(oo);
}

/* the first of p->sorted that starts at or above base */
static int
sortpos(Proc *p, uintptr base)
{
	int lo, hi, m;

	lo = 0;
	hi = p->nsorted;
	while(lo < hi){
		m = (lo+hi)/2;
		if(p->sorted[m]->base < base)
			lo = m+1;
		else
			hi = m;
	}
	return lo;
}

static void
sortin(Proc *p, Segment *s)
{
	int i;

	i = sortpos(p, s->base);
	memmove(&p->sorted[i+1], &p->sorted[i], (p->nsorted-i)*sizeof(Segment*));
	p->sorted[i] = s;
	p->nsorted++;
}

static void
sortout(Proc *p, Segment *s)
{
	int i;

	for(i = sortpos(p, s->base); i < p->nsorted; i++)
		if(p->sorted[i] == s){
			p->nsorted--;
			memmove(&p->sorted[i], &p->sorted[i+1], (p->nsorted-i)*sizeof(Segment*));
			return;
		}
	panic("sortout: segment %#p not in pid %lud's table", s->base, p->pid);
}

/* of p->sorted the last that starts at or below addr, if it holds addr (p->segtab or p->seglock held) */
Segment*
findseg(Proc *p, uintptr addr)
{
	int lo, hi, m;
	Segment *s;

	lo = 0;
	hi = p->nsorted;
	while(lo < hi){
		m = (lo+hi)/2;
		if(p->sorted[m]->base <= addr)
			lo = m+1;
		else
			hi = m;
	}
	if(lo == 0)
		return nil;
	s = p->sorted[lo-1];
	return addr < s->top ? s : nil;
}

/*
 * the attached segment holding addr, or nil; unless the caller holds
 * p->seglock it may be gone once found (good for a test of the address)
 */
Segment*
segbyaddr(Proc *p, uintptr addr)
{
	Segment *s;

	ilock(&p->segtab);
	s = findseg(p, addr);
	iunlock(&p->segtab);
	return s;
}

/* 9front-ng: p's segment in slot i with a reference (putseg), or nil: devproc's text and profile */
Segment*
segslot(Proc *p, int i)
{
	Segment *s;

	ilock(&p->segtab);
	s = i < p->nseg ? p->seg[i] : nil;
	if(s != nil)
		incref(&s->Ref);
	iunlock(&p->segtab);
	return s;
}

/*
 * 9front-ng: up's segment holding addr, locked if dolock, or nil; given
 * back with segunref.  What a process uses without its seglock (fault,
 * the semaphores, segfree): in a memory image of several (up->mgrp)
 * another may detach the segment meanwhile, so it comes with a
 * reference; alone, nobody else changes up's table (only up joins or
 * leaves one), and the table's own reference keeps it, as in 9front.
 */
Segment*
segref(uintptr addr, int dolock)
{
	Segment *s;

	ilock(&up->segtab);
	s = findseg(up, addr);
	if(s != nil && up->mgrp != nil)
		incref(&s->Ref);
	iunlock(&up->segtab);
	if(s == nil || dolock == 0)
		return s;
	qlock(&s->QLock);
	if(addr >= s->base && addr < s->top)
		return s;
	qunlock(&s->QLock);
	segunref(s);
	return nil;
}

void
segunref(Segment *s)
{
	if(up->mgrp != nil)
		putseg(s);
}

/* Must be called with p->seglock locked; segno below p->segcap (segreserve). */
void
attachseg(Proc *p, int segno, Segment *s)
{
	int i = p->index;

	if(segno >= p->segcap)
		panic("attachseg: slot %d of %d", segno, p->segcap);
	qlock(&s->QLock);
	if(i < s->firstproc)
		s->firstproc = i;
	if(i > s->lastproc)
		s->lastproc = i;
	s->segno = segno;
	qunlock(&s->QLock);

	ilock(&p->segtab);
	p->seg[segno] = s;
	sortin(p, s);
	if(segno >= p->nseg)
		p->nseg = segno+1;
	iunlock(&p->segtab);
}

int
segno(Proc *p, Segment *s)
{
	int i = s->segno;

	if(i < 0 || (i < p->nseg && p->seg[i] == s))
		return i;
	for(i = 0; i < p->nseg; i++) {
		if(p->seg[i] == s)
			return i;
	}
	return -1;
}

/* Must be called with p->seglock locked */
Segment*
detachseg(Proc *p, int segno)
{
	Segment *s;

	ilock(&p->segtab);
	s = p->seg[segno];
	if(s != nil){
		p->seg[segno] = nil;
		sortout(p, s);
	}
	iunlock(&p->segtab);
	return s;
}

void
putseg(Segment *s)
{
	Image *i;

	if(s == nil)
		return;

	i = s->image;
	if(i != nil) {
		/*
		 *  We must hold image lock here during
		 *  decref() to prevent someone from taking
		 *  a reference to our segment from the cache.
		 *  Just letting decref(s) drop to zero *before*
		 *  and then checking s->ref again under image
		 *  lock is not sufficient, as someone can grab
		 *  a reference and then call putseg() again;
		 *  freeing segment. By the time we hold image lock,
		 *  the segment would be freed from under us.
		 */
		lock(&i->Lock);
		if(decref(&s->Ref) != 0){
			unlock(&i->Lock);
			return;
		}
		if(i->s == s)
			i->s = nil;
		putimage(i);
	} else if(decref(&s->Ref) != 0)
		return;

	assert(s->sema.prev == &s->sema);
	assert(s->sema.next == &s->sema);

	if(s->mapsize > 0){
		int i;
		Pte *pte;
		Page *entry, *fh, *ft;
		ulong np;

		np = 0;
		fh = ft = nil;

		for(i = 0; i < s->mapsize; i++){
			if((pte = s->map[i]) == nil)
				continue;
			while(pte->first <= pte->last){
				entry = *(pte->first++);
				if(entry == nil)
					continue;
				if(onswap(entry)){
					putswap(entry);
					continue;
				}
				entry = deadpage(entry);
				if(entry == nil)
					continue;
				if(fh != nil)
					ft->next = entry;
				else
					fh = entry;
				ft = entry;
				np++;
			}
			if(pte != (Pte*)(s+1))
				free(pte);
		}

		freepages(fh, ft, np);

		if(s->map != s->ssegmap)
			free(s->map);
	}

	if(s->profile != nil)
		free(s->profile);

	/* 9front-ng: a pager's segment (devsegment.c) */
	if((s->type&SG_TYPE) == SG_PHYSICAL && s->pseg != nil && s->pseg->pager != nil && _pagerfree != nil)
		(*_pagerfree)(s->pseg->pager);

	free(s);
}

static Pte*
ptealloc(Segment *s)
{
	Pte *new;

	new = s->freepte;
	if(new != nil)
		s->freepte = nil;
	else {
		new = malloc(sizeof(Pte));
		if(new == nil)
			return nil;
	}
	new->first = &new->pages[PTEPERTAB];
	new->last = new->pages;

	return new;
}

static Pte*
ptecpy(Pte *new, Pte *old)
{
	Page **src, **dst, *entry;

	dst = &new->pages[old->first-old->pages];
	new->first = dst;
	for(src = old->first; src <= old->last; src++, dst++){
		if((entry = *src) == nil)
			continue;
		if(onswap(entry))
			dupswap(entry);
		else
			incref(&entry->Ref);
		new->last = dst;
		*dst = entry;
	}
	return new;
}

Segment*
dupseg(int segno, int share)
{
	int i;
	Pte *pte;
	Segment *n, *s;

	s = up->seg[segno];
	if(s == nil || segno == ESEG)
		return nil;

	qlock(&s->QLock);
	if(waserror()){
		qunlock(&s->QLock);
		nexterror();
	}
	switch(s->type&SG_TYPE) {
	case SG_TEXT:		/* New segment shares pte set */
	case SG_SHARED:
	case SG_PHYSICAL:
	case SG_FIXED:
	case SG_STICKY:
	default:
	sameseg:
		incref(&s->Ref);
		qunlock(&s->QLock);
		poperror();
		return s;

	case SG_STACK:
		n = newseg(s->type, s->base, s->size);
		break;

	case SG_BSS:		/* Just copy on write */
		if(share)
			goto sameseg;
		n = newseg(s->type, s->base, s->size);
		break;

	case SG_DATA:		/* Copy on write plus demand load info */
		if(segno == TSEG){
			n = data2txt(s);
			qunlock(&s->QLock);
			poperror();
			return n;
		}
		if(share)
			goto sameseg;
		n = newseg(s->type, s->base, s->size);
		n->image = s->image;
		n->fstart = s->fstart;
		n->flen = s->flen;
		incref(&s->image->Ref);
		break;
	}
	for(i = 0; i < s->mapsize; i++){
		if(s->map[i] != nil){
			pte = ptealloc(n);
			if(pte == nil){
				qunlock(&s->QLock);
				poperror();
				putseg(n);
				error(Enomem);
			}
			n->map[i] = ptecpy(pte, s->map[i]);
		}
	}
	n->used = s->used;
	n->swapped = s->swapped;
	n->flushme = s->flushme;
	if(s->Ref.ref > 1)
		procflushseg(s);
	qunlock(&s->QLock);
	poperror();
	return n;
}

/* Must be called with s locked */
Page*
segpeek(Segment *s, uintptr addr)
{
	Pte *pte;
	uintptr soff;

	assert(addr < s->top);
	assert(addr >= s->base);

	soff = addr - s->base;
	pte = s->map[soff/PTEMAPMEM];
	if(pte == nil)
		return nil;
	return pte->pages[(soff&(PTEMAPMEM-1))/BY2PG];
}

/* Must be called with s locked */
Page**
segmap(Segment *s, uintptr addr)
{
	Pte *pte;
	Page **pg;
	uintptr soff;

	assert(addr < s->top);
	assert(addr >= s->base);

	soff = addr - s->base;
	pte = s->map[soff/PTEMAPMEM];
	if(pte == nil) {
		if((pte = ptealloc(s)) == nil)
			return nil;
		s->map[soff/PTEMAPMEM] = pte;
	}

	pg = &pte->pages[(soff&(PTEMAPMEM-1))/BY2PG];
	if(pg < pte->first)
		pte->first = pg;
	if(pg > pte->last)
		pte->last = pg;

	return pg;
}

/*
 *  Insert new Page into Segmnet s.
 *  On error, calls putpage(new).
 */
void
segpage(Segment *s, Page *new)
{
	Page **pg;

	qlock(&s->QLock);
	if((pg = segmap(s, new->va)) == nil) {
		qunlock(&s->QLock);
		putpage(new);
		error(Enomem);
	}
	settxtflush(new, s->flushme);
	assert(*pg == nil);
	*pg = new;
	s->used++;
	qunlock(&s->QLock);
}

void
relocateseg(Segment *s, uintptr base)
{
	int i;
	Pte *pte;
	Page **pg;
	uintptr offset;

	assert((base & (BY2PG-1)) == 0);

	qlock(&s->QLock);
	offset = base - s->base;
	for(i = 0; i < s->mapsize; i++) {
		if((pte = s->map[i]) == nil)
			continue;
		for(pg = pte->first; pg <= pte->last; pg++) {
			if(!pagedout(*pg))
				(*pg)->va += offset;
		}
	}
	s->base += offset;
	s->top += offset;
	qunlock(&s->QLock);
}

Image*
newimage(ulong pages)
{
	ulong pghsize;
	Image *i;

	/* make power of two */
	pghsize = pages-1;
	pghsize |= pghsize >> 16;
	pghsize |= pghsize >> 8;
	pghsize |= pghsize >> 4;
	pghsize |= pghsize >> 2;
	pghsize |= pghsize >> 1;
	pghsize++;

	if(pghsize > 1024)
		pghsize >>= 4;

	i = malloc(sizeof(Image) + pghsize * sizeof(Page*));
	if(i == nil)
		return nil;

	i->Ref.ref = 1;
	i->pghsize = pghsize;

	return i;
}

/* remove from idle list */
static void
busyimage(Image *i)
{
	/* not on idle list? */
	if(i->link == nil)
		return;

	if((*i->link = i->next) != nil)
		i->next->link  = i->link;
	i->link = nil;
	i->next = nil;
	imagealloc.pgidle -= i->pgref;
	imagealloc.nidle--;
}

Image*
attachimage(Chan *c, ulong pages)
{
	Image *i, **l;

retry:
	lock(&imagealloc.Lock);

	/*
	 * Search the image cache for remains of the text from a previous
	 * or currently running incarnation
	 */
	for(i = ihash(c->qid.path); i != nil; i = i->hash){
		if(eqchantdqid(c, i->type, i->dev, i->qid, 0)){
			incref(&i->Ref);
			busyimage(i);
			goto found;
		}
	}
	if(imagealloc.nidle > conf.nimage
	|| (i = newimage(pages)) == nil) {
		unlock(&imagealloc.Lock);
		if(imagealloc.nidle == 0)
			error(Enomem);
		if(imagereclaim(0) == 0)
			freebroken();		/* can use the memory */
		goto retry;
	}
	i->type = c->type;
	i->dev = c->dev;
	i->qid = c->qid;
	l = &ihash(c->qid.path);
	i->hash = *l;
	*l = i;
found:
	i->nattach++;
	unlock(&imagealloc.Lock);
	lock(&i->Lock);
	if(i->c == nil){
		i->c = c;
		incref(&c->Ref);
	}
	return i;
}

/* insert into idle list */
static void
idleimage(Image *i)
{
	Image **l, *j;

	/* already on idle list? */
	if(i->link != nil)
		return;

	l = &imagealloc.idle;
	j = imagealloc.idle;
	/* sort by least frequenty and most pages used first */
	for(; j != nil; l = &j->next, j = j->next){
		long c = j->nattach - i->nattach;
		if(c < 0)
			continue;
		if(c > 0)
			break;
		if(j->pgref < i->pgref)
			break;
	}
	if((i->next = j) != nil)
		j->link = &i->next;
	*(i->link = l) = i;
	imagealloc.pgidle += i->pgref;
	imagealloc.nidle++;
}

/* putimage(): called with image locked and unlocks */
void
putimage(Image *i)
{
	Chan *c;
	long r;

	r = decref(&i->Ref);
	if(i->notext){
		unlock(&i->Lock);
		return;
	}
	c = nil;
	if(r == 0){
		assert(i->pgref == 0);
		assert(i->s == nil);
		lock(&imagealloc.Lock);
		r = i->Ref.ref;
		if(r == 0){
			Image *f, **l;

			c = i->c;
			i->c = nil;
			busyimage(i);

			l = &ihash(i->qid.path);
			for(f = *l; f != nil; f = f->hash) {
				if(f == i) {
					*l = i->hash;
					break;
				}
				l = &f->hash;
			}
		}
		unlock(&imagealloc.Lock);
	} else if(r == i->pgref) {
		assert(i->pgref > 0);
		assert(i->s == nil);
		if(i->link == nil){
			c = i->c;
			i->c = nil;

			lock(&imagealloc.Lock);
			idleimage(i);
			unlock(&imagealloc.Lock);
		}
	}
	unlock(&i->Lock);

	if(r == 0)
		free(i);

	if(c != nil)
		ccloseq(c);	/* does not block */
}

ulong
imagecached(void)
{
	return imagealloc.pgidle;
}

ulong
imagereclaim(ulong pages)
{
	ulong np;
	Image *i;

	eqlock(&imagealloc.ireclaim);
	
	lock(&imagealloc.Lock);
	np = 0;
	while(np < pages || imagealloc.nidle > conf.nimage) {
		i = imagealloc.idle;
		if(i == nil)
			break;
		incref(&i->Ref);
		busyimage(i);
		unlock(&imagealloc.Lock);

		np += pagereclaim(i);

		lock(&i->Lock);
		putimage(i);

		lock(&imagealloc.Lock);
	}
	unlock(&imagealloc.Lock);

	qunlock(&imagealloc.ireclaim);

	return np;
}

/*
 *  Must be called with s locked.
 *
 *  This relies on s->ref > 1 indicating that
 *  the segment is shared with other processes
 *  different from the calling one.
 *
 *  The calling process (up) is responsible for
 *  flushing its own TBL by calling flushmmu()
 *  afterwards when returning a non-zero value
 */
static int
segfreemap(Segment *s, uintptr from, uintptr to)
{
	uintptr soff;
	int poff, i;
	Pte *pte;
	Page **pg, *entry, *fh, *ft;
	ulong np;
	int flush;

	to &= ~(BY2PG-1);
	from = PGROUND(from);
	if(from >= to)
		return 0;

	fh = ft = nil;
	np = 0;
	flush = 0;

	soff = from - s->base;
	poff = (soff & (PTEMAPMEM-1))/BY2PG;
	for(i = soff/PTEMAPMEM; i < s->mapsize; i++, poff = 0) {
		if((pte = s->map[i]) == nil) {
			from = (from | (PTEMAPMEM-1))+1;
			if(from >= to)
				goto done;
			continue;
		}

		for(pg = &pte->pages[poff]; pg < &pte->pages[PTEPERTAB]; pg++) {
			if(pg == pte->first)
				pte->first++;
			if((entry = *pg) != nil) {
				*pg = nil;
				if(onswap(entry)) {
					putswap(entry);
					s->swapped--;
				} else {
					if((entry = deadpage(entry)) != nil) {
						if(fh != nil)
							ft->next = entry;
						else
							fh = entry;
						ft = entry;
						np++;
					}
					flush = 1;
				}
				s->used--;
			}
			from += BY2PG;
			if(from >= to)
				goto done;
		}

		if(poff == 0) {
			s->map[i] = nil;
			if(pte == (Pte*)(s+1))
				s->freepte = pte;
			else
				free(pte);
		}
	}
done:
	/* skip TLB flush when no pages have been changed */
	if(flush == 0)
		return 0;

	/*
	 * we have to make sure other processors flush the
	 * entries from their TLBs before any pages are freed.
	 */
	if(s->Ref.ref > 1)
		procflushseg(s);

	freepages(fh, ft, np);

	/* tell caller it should flush its TLB */
	return 1;
}

/*
 * 9front-ng: take all pages out of segment s (an alias segment of
 * segment(3)): every process that has it attached loses them at once,
 * its TLB flushed; a later access faults.  The pages stay where else
 * they are (the sticky segments they came from).
 */
void
segrevoke(Segment *s)
{
	int flush;

	/*
	 * a physical segment (segment(3)'s phys) has no pages to take:
	 * it faults from now on, its mappings flushed everywhere; a
	 * fault that mapped it meanwhile goes around (fault's tlbflush)
	 */
	if((s->type & SG_TYPE) == SG_PHYSICAL){
		qlock(&s->QLock);
		s->type |= SG_FAULT;
		qunlock(&s->QLock);
		if(s->Ref.ref > 1)
			procflushseg(s);
		flushmmu();
		return;
	}
	qlock(&s->QLock);
	flush = segfreemap(s, s->base, s->top);
	qunlock(&s->QLock);
	if(flush)
		flushmmu();
}

uintptr
ibrk(int segno, uintptr newtop)
{
	Segment *s;
	ulong newsize;
	int mapsize;
	Pte **map;

	s = up->seg[segno];
	if(s == nil)
		error(Ebadarg);

	if(newtop == 0)
		return s->base;

	qlock(&s->QLock);

	/* We may start with the bss overlapping the data */
	if(newtop < s->base) {
		if(segno != BSEG || up->seg[DSEG] == nil || newtop < up->seg[DSEG]->base) {
			qunlock(&s->QLock);
			error(Enovmem);
		}
		newtop = s->base;
	}

	newtop = PGROUND(newtop);
	newsize = (newtop-s->base)/BY2PG;
	if(newtop < s->top) {
		/*
		 * do not shrink a segment shared with other procs, as the
		 * to-be-freed address space may have been passed to the kernel
		 * already by another proc and is past the validaddr stage.
		 */
		if(s->Ref.ref > 1){
			qunlock(&s->QLock);
			error(Einuse);
		}
		if(!segfreemap(s, newtop, s->top))
			goto done;	/* skip flushmmu() */
		s->top = newtop;
		s->size = newsize;
		qunlock(&s->QLock);
		flushmmu();
		return 0;
	}

	if(groupoverlap(up->mgrp, s->top, newtop - s->top) != nil){	/* 9front-ng: caller holds the image's lock (sysproc.c) */
		qunlock(&s->QLock);
		error(Esoverlap);
	}

	if(newsize > (SEGMAPSIZE*PTEPERTAB)) {
		qunlock(&s->QLock);
		error(Enovmem);
	}
	mapsize = ROUND(newsize, PTEPERTAB)/PTEPERTAB;
	if(mapsize > s->mapsize) {
		map = malloc(mapsize*sizeof(Pte*));
		if(map == nil){
			qunlock(&s->QLock);
			error(Enomem);
		}
		memmove(map, s->map, s->mapsize*sizeof(Pte*));
		if(s->map != s->ssegmap)
			free(s->map);
		s->map = map;
		s->mapsize = mapsize;
	}
done:
	s->top = newtop;
	s->size = newsize;
	qunlock(&s->QLock);
	return 0;
}

/* p's segment overlapping base to base+len, or nil (p->seglock held) */
static Segment*
overlap(Proc *p, uintptr base, uintptr len)
{
	int i;
	Segment *s;
	uintptr top;

	/*
	 * 9front-ng: of the segments, sorted by base and not overlapping,
	 * the last that starts below top ends last of those: it overlaps
	 * if any does
	 */
	top = base+len;
	i = sortpos(p, top);
	if(i == 0)
		return nil;
	s = p->sorted[i-1];
	if(top > s->base && base < s->top)
		return s;
	return nil;
}

Segment*
isoverlap(uintptr base, uintptr len)
{
	return overlap(up, base, len);
}

/*
 * 9front-ng: the memory image up shares (rfork RFMEM), locked for a change
 * of the members' tables, or nil; before up->seglock
 */
Mgrp*
mgrplock(void)
{
	Mgrp *g;

	g = up->mgrp;	/* only up changes it */
	if(g != nil)
		qlock(&g->QLock);
	return g;
}

void
mgrpunlock(Mgrp *g)
{
	if(g != nil)
		qunlock(&g->QLock);
}

/*
 * up leaves its memory image: exec's commit, pexit; the others change
 * its table no more
 */
void
mgrpleave(void)
{
	Mgrp *g;
	Proc **l;

	if((g = up->mgrp) == nil)
		return;
	qlock(&g->QLock);
	for(l = &g->procs; *l != nil; l = &(*l)->mnext)
		if(*l == up){
			*l = up->mnext;
			break;
		}
	up->mnext = nil;
	up->mgrp = nil;
	qunlock(&g->QLock);
	if(decref(&g->Ref) == 0)
		free(g);
}

/*
 * the segment overlapping va to va+len in up's table, or in another
 * member's: the temporary stack of an exec (ESEG) is each one's own;
 * everything else is the same in all (g held)
 */
static Segment*
groupoverlap(Mgrp *g, uintptr va, uintptr len)
{
	Segment *s;
	Proc *p;

	if((s = isoverlap(va, len)) != nil || g == nil)
		return s;
	for(p = g->procs; p != nil; p = p->mnext)
		if(p != up && (s = p->seg[ESEG]) != nil
		&& va+len > s->base && va < s->top)
			return s;
	return nil;
}

Physseg*
addphysseg(Physseg* new)
{
	Physseg *ps;

	/*
	 * Check not already entered and there is room
	 * for a new entry and the terminating null entry.
	 */
	lock(&physseglock);
	for(ps = physseg; ps->name; ps++){
		if(strcmp(ps->name, new->name) == 0){
			unlock(&physseglock);
			return nil;
		}
	}
	if(ps-physseg >= nelem(physseg)-2){
		unlock(&physseglock);
		return nil;
	}
	*ps = *new;
	unlock(&physseglock);

	return ps;
}

Physseg*
findphysseg(char *name)
{
	Physseg *ps;

	for(ps = physseg; ps->name; ps++)
		if(strcmp(ps->name, name) == 0)
			return ps;

	return nil;
}

/* 9front-ng: a free slot of p, i if it is, the table grown if every one is in use; -1 if it cannot grow (p->seglock held) */
static int
freesegno(Proc *p, int i)
{
	if(i < 0 || i == ESEG || i >= p->nseg || p->seg[i] != nil)
		i = 0;
	for(; i < p->nseg; i++) {
		if(p->seg[i] == nil && i != ESEG)
			return i;
	}
	if(growtab(p, p->nseg+1) < 0)
		return -1;
	return p->nseg;
}

static int
unusedsegno(int i)
{
	i = freesegno(up, i);
	if(i < 0)
		error(Enovmem);
	return i;
}

/* the members' seglocks taken so far: up to (not with) end */
static void
unlockmembers(Mgrp *g, Proc *end)
{
	Proc *p;

	for(p = g->procs; p != end; p = p->mnext)
		if(p != up)
			qunlock(&p->seglock);
}

/*
 * 9front-ng: s, about to be attached in up, attached in the other members
 * of g as well, at the same place; all or none (g and up->seglock held).
 * The tables are the same but for the stacks (va 0 kept clear of the
 * temporary ones: groupoverlap), so what fits up fits them.
 */
static void
mgrpattach(Mgrp *g, Segment *s, int segno)
{
	Proc *p;
	char *e;
	int n;

	e = nil;
	for(p = g->procs; p != nil; p = p->mnext){
		if(p == up)
			continue;
		qlock(&p->seglock);
		if(overlap(p, s->base, s->top - s->base) != nil)
			e = Esoverlap;
		else if(freesegno(p, segno) < 0)
			e = Enovmem;
		if(e != nil){
			qunlock(&p->seglock);
			unlockmembers(g, p);
			error(e);
		}
	}
	for(p = g->procs; p != nil; p = p->mnext){
		if(p == up)
			continue;
		n = freesegno(p, segno);	/* found above: no growth now */
		incref(&s->Ref);
		attachseg(p, n, s);
		qunlock(&p->seglock);
	}
}

uintptr
segattach(int attr, char *name, uintptr va, uintptr len)
{
	int segno;
	Segment *s, *os;
	Physseg *ps;
	Mgrp *g;

	if(va != 0 && va >= USTKTOP)
		error(Ebadarg);

	g = mgrplock();
	qlock(&up->seglock);
	if(waserror()){
		qunlock(&up->seglock);
		mgrpunlock(g);
		nexterror();
	}
		
	/*
	 *  first look for a global segment with the
	 *  same name
	 */
	if(_globalsegattach != nil){
		s = (*_globalsegattach)(name);
		if(s != nil){
			va = s->base;
			len = s->top - va;
			/* 9front-ng: an error puts the reference (9front kept it) */
			if(groupoverlap(g, va, len) != nil){
				putseg(s);
				error(Esoverlap);
			}
			if((segno = freesegno(up, s->segno)) < 0){
				putseg(s);
				error(Enovmem);
			}
			goto done;
		}
	}

	/* round up va+len */
	len += va & (BY2PG-1);
	len = PGROUND(len);

	if(len == 0)
		error(Ebadarg);

	/*
	 * Find a hole in the address space.
	 * Starting at the lowest possible stack address - len,
	 * check for an overlapping segment, and repeat at the
	 * base of that segment - len until either a hole is found
	 * or the address space is exhausted.  Ensure that we don't
	 * map the zero page.
	 */
	if(va == 0) {
		for (os = up->seg[SSEG]; os != nil; os = groupoverlap(g, va, len)) {
			va = os->base;
			if(len >= va)
				error(Enovmem);
			va -= len;
			/* 9front-ng: not in segment(3)'s shared region (portdat.h) */
			if(va < SHAREDTOP && va+len > SHAREDBASE){
				if(len >= SHAREDBASE)
					error(Enovmem);
				va = SHAREDBASE - len;
			}
		}
	}

	va &= ~(BY2PG-1);
	if(va == 0 || (va+len) > USTKTOP || (va+len) < va)
		error(Ebadarg);

	if(groupoverlap(g, va, len) != nil)
		error(Esoverlap);

	ps = findphysseg(name);
	if(ps == nil)
		error(Ebadarg);

	/*
	 * 9front-ng: a device whose memory and registers reach all of the
	 * machine's (a GPU's bars: it does DMA) is for the hostowner; its
	 * driver gives clients segment(3) phys views of their objects
	 */
	if(ps->eveonly && !iseve())
		error(Eperm);

	if(len > ps->size)
		error(Enovmem);

	segno = unusedsegno(-1);

	/* Turn off what is not allowed */
	attr &= ~(SG_TYPE | SG_CACHED | SG_DEVICE | SG_WC);

	/* Copy in defaults */
	attr |= ps->attr;

	s = newseg(attr, va, len/BY2PG);
	s->pseg = ps;
done:
	if(g != nil){
		if(waserror()){
			putseg(s);
			nexterror();
		}
		mgrpattach(g, s, segno);
		poperror();
	}
	attachseg(up, segno, s);
	qunlock(&up->seglock);
	mgrpunlock(g);
	poperror();

	return va;
}

static int
segflush1(Segment *s, uintptr from, uintptr to)
{
	if((s->type & SG_NOEXEC) != 0)
		return 0;

	if((s->type & SG_RONLY) != 0 && s->flushme)
		return 0;

	s->flushme = 1;
	if(s->Ref.ref > 1)
		procflushseg(s);

	if(s->mapsize == 0)
		return 1;

	from &= ~(BY2PG);
	to = PGROUND(to);
	while(from < to){
		Page *p = segpeek(s, from);
		if(!pagedout(p)){
			settxtflush(p, 1);
		}
		from += BY2PG;
	}

	return 1;
}

static int
segfree1(Segment *s, uintptr from, uintptr to)
{
	uintptr afrom, ato;
	int fill;
	Page *p;
	KMap *k;

	if((s->type & SG_RONLY) != 0 || s->mapsize == 0)
		return 0;

	fill = 0;
	switch(s->type&SG_TYPE){
	case SG_FIXED:
	case SG_STICKY:
		qunlock(&s->QLock);
		if(!waserror()){
			memset((uchar*)from, fill, to-from);
			poperror();
		}
		qlock(&s->QLock);
		/* wet floor */
	case SG_PHYSICAL:
	case SG_TEXT:
		return 0;

	case SG_STACK:
		fill = 0xfe;
		/* wet floor */
	default:
	case SG_BSS:
	case SG_SHARED:
		ato = to & ~(BY2PG-1);
		afrom = PGROUND(from);

		/* fill partial covered pages with pattern */
		if(afrom > to)
			afrom = ato = to;
		if(from < afrom && segpeek(s, from) != nil){
			qunlock(&s->QLock);
			if(!waserror()){
				memset((uchar*)from, fill, afrom-from);
				poperror();
			}
			qlock(&s->QLock);
		}
		if(ato < to && ato >= from && segpeek(s, ato) != nil){
			qunlock(&s->QLock);
			if(!waserror()){
				memset((uchar*)ato, fill, to-ato);
				poperror();
			}
			qlock(&s->QLock);
		}
		return segfreemap(s, afrom, ato);

	case SG_DATA:
		ato = to & ~(BY2PG-1);
		afrom = PGROUND(from);

		/* reset partially covered pages to original image data. */
		if(afrom > to)
			afrom = ato = to;
		if(from < afrom && segpeek(s, from) != nil
		&& (p = lookpage(s->image, s->fstart + (from & ~(BY2PG-1)) - s->base)) != nil){
			qunlock(&s->QLock);
			k = kmap(p);
			if(!waserror()){
				memmove((uchar*)from, (uchar*)VA(k) + (from & (BY2PG-1)), afrom-from);
				poperror();
			}
			kunmap(k);
			putpage(p);
			qlock(&s->QLock);
		}
		if(ato < to && ato >= from && segpeek(s, ato) != nil
		&& (p = lookpage(s->image, s->fstart + ato - s->base)) != nil){
			qunlock(&s->QLock);
			k = kmap(p);
			if(!waserror()){
				memmove((uchar*)ato, (uchar*)VA(k), to-ato);
				poperror();
			}
			kunmap(k);
			putpage(p);
			qlock(&s->QLock);
		}
		return segfreemap(s, afrom, ato);
	}
}

static int
withsegrange(void *va, uintptr len, int (*fun)(Segment*, uintptr, uintptr))
{
	uintptr from, to, top;
	Segment *s;
	int flush;

	from = (uintptr)va;
	to = from + len;
	if(to < from || to > USTKTOP)
		error(Ebadarg);

	flush = 0;
	while(from < to){
		s = segref(from, 1);	/* 9front-ng: another of the memory image may detach it */
		if(s == nil){
			if(flush)
				flushmmu();
			error(Ebadarg);
		}
		top = s->top;
		if(to <= top){
			flush |= (*fun)(s, from, to);
			qunlock(&s->QLock);
			segunref(s);
			break;
		}
		flush |= (*fun)(s, from, top);
		qunlock(&s->QLock);
		segunref(s);
		from = top;
	}

	if(flush)
		flushmmu();

	return 0;
}

int
segflush(void *va, uintptr len)
{
	return withsegrange(va, len, segflush1);
}

int
segfree(void *va, uintptr len)
{
	return withsegrange(va, len, segfree1);
}

void
segclock(uintptr pc)
{
	Segment *s;

	/* 9front-ng: another of the memory image may detach the text (segtab) */
	ilock(&up->segtab);
	s = up->seg[TSEG];
	if(s != nil && s->profile != nil){
		s->profile[0] += TK2MS(1);
		if(pc >= s->base && pc < s->top) {
			pc -= s->base;
			s->profile[pc>>LRESPROF] += TK2MS(1);
		}
	}
	iunlock(&up->segtab);
}

Segment*
txt2data(Segment *s)
{
	Segment *ps;

	ps = newseg(SG_DATA, s->base, s->size);
	ps->image = s->image;
	ps->fstart = s->fstart;
	ps->flen = s->flen;
	ps->flushme = 1;
	incref(&s->image->Ref);
	return ps;
}

Segment*
data2txt(Segment *s)
{
	Segment *ps;
	Image *i;

	i = s->image;
	lock(&i->Lock);
	if((ps = i->s) != nil && ps->flen == s->flen){
		assert(ps->image == i);
		incref(&ps->Ref);	
		unlock(&i->Lock);
		return ps;
	}
	if(waserror()){
		unlock(&i->Lock);
		nexterror();
	}
	ps = newseg(SG_TEXT | SG_RONLY, s->base, s->size);
	ps->image = i;
	ps->fstart = s->fstart;
	ps->flen = s->flen;
	if(i->s == nil)
		i->s = ps;
	incref(&i->Ref);
	unlock(&i->Lock);
	poperror();
	return ps;
}

enum {
	/* commands to segmentioproc */
	Cnone=0,
	Cread,
	Cwrite,
	Cdie,
};

static int
cmddone(void *arg)
{
	Segio *sio = arg;

	return sio->cmd == Cnone;
}

static void
docmd(Segio *sio, int cmd)
{
	sio->err = nil;
	sio->cmd = cmd;
	while(waserror())
		;
	wakeup(&sio->cmdwait);
	sleep(&sio->replywait, cmddone, sio);
	poperror();
	if(sio->err != nil)
		error(sio->err);
}

static int
cmdready(void *arg)
{
	Segio *sio = arg;

	return sio->cmd != Cnone;
}

static void
segmentioproc(void *arg)
{
	Segio *sio = arg;
	int done, segno;

	qlock(&up->seglock);
	sio->p = up;
	incref(&sio->s->Ref);
	segno = unusedsegno(sio->s->segno);
	attachseg(up, segno, sio->s);
	qunlock(&up->seglock);

	while(waserror())
		;
	for(done = 0; !done;){
		sleep(&sio->cmdwait, cmdready, sio);
		if(waserror())
			sio->err = up->errstr;
		else {
			if(sio->s != nil && up->seg[segno] != sio->s){
				Segment *tmp;

				qlock(&up->seglock);
				tmp = detachseg(up, segno);
				incref(&sio->s->Ref);
				segno = unusedsegno(sio->s->segno);
				attachseg(up, segno, sio->s);
				qunlock(&up->seglock);
				putseg(tmp);

				flushmmu();
			}
			switch(sio->cmd){
			case Cread:
				memmove(sio->data, sio->addr, sio->dlen);
				break;
			case Cwrite:
				memmove(sio->addr, sio->data, sio->dlen);
				if(sio->s->flushme)
					segflush(sio->addr, sio->dlen);
				break;
			case Cdie:
				done = 1;
				break;
			}
			poperror();
		}
		sio->cmd = Cnone;
		wakeup(&sio->replywait);
	}

	pexit("done", 1);
}

long
segio(Segio *sio, Segment *s, void *a, long n, vlong off, int read)
{
	uintptr m;
	void *b;

	b = a;
	if(s != nil){
		m = s->top - s->base;
		if(off < 0 || off >= m){
			if(!read)
				error(Ebadarg);
			return 0;
		}
		if(off+n > m){
			if(!read)
				error(Ebadarg);	
			n = m - off;
		}

		if((uintptr)a < KZERO) {
			b = smalloc(n);
			if(waserror()){
				free(b);
				nexterror();
			}
			if(!read)
				memmove(b, a, n);
		}
	}

	eqlock(&sio->QLock);
	if(waserror()){
		qunlock(&sio->QLock);
		nexterror();
	}
	sio->s = s;
	if(s == nil){
		if(sio->p != nil){
			docmd(sio, Cdie);
			sio->p = nil;
		}
		qunlock(&sio->QLock);
		poperror();
		return 0;
	}
	if(sio->p == nil){
		sio->cmd = Cnone;
		kproc("segmentio", segmentioproc, sio);
	}
	sio->addr = (char*)s->base + off;
	sio->data = b;
	sio->dlen = n;
	docmd(sio, read ? Cread : Cwrite);
	qunlock(&sio->QLock);
	poperror();

	if(a != b){
		if(read)
			memmove(a, b, n);
		free(b);
		poperror();
	}
	return n;
}
