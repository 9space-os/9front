#include <u.h>
#include <libc.h>
#include <draw.h>

#define	HSHIFT	3	/* HSHIFT==5 runs slightly faster, but hash table is 64x bigger */
#define	NHASH	(1<<(HSHIFT*NMATCH))
#define	HMASK	(NHASH-1)
#define	hupdate(h, c)	((((h)<<HSHIFT)^(c))&HMASK)
typedef struct Hlist Hlist;
struct Hlist{
	uchar *s;
	Hlist *next, *prev;
};

/*
 * 9space: writeimage's compressor as a function of its own (zloadimage
 * takes it too): as many whole lines of bpl bytes from data (nline of
 * them) as fit in nout bytes at out, in image(6)'s compressed form, one
 * block (its matches stay in it); the bytes used, *nl the lines (0: not
 * even one fits)
 */
int
_compresslines(uchar *data, int bpl, int nline, uchar *out, int nout, int *nl)
{
	uchar *outp, *eout, *loutp;
	Hlist *hash, *chain, *hp, *cp;
	int h, n, offs, runlen, ndump;
	uchar *line, *eline, *edata;
	uchar dumpbuf[NDUMP];
	uchar *p, *q, *s, *es, *t;

	*nl = 0;
	if(nline <= 0 || bpl <= 0)
		return 0;
	hash = mallocz(NHASH*sizeof(Hlist), 1);
	chain = mallocz(NMEM*sizeof(Hlist), 1);
	if(hash == nil || chain == nil){
		free(hash);
		free(chain);
		return -1;
	}
	line = data;
	edata = data + nline*bpl;
	cp = chain;
	h = 0;
	for(n = 0; n != NMATCH && line+n < edata; n++)
		h = hupdate(h, line[n]);
	outp = loutp = out;
	eout = out + nout;
	while(line != edata){
		ndump = 0;
		eline = line+bpl;
		for(p = line; p != eline; ){
			if(eline-p < NRUN)
				es = eline;
			else
				es = p+NRUN;
			q = 0;
			runlen = 0;
			for(hp = hash[h].next; hp; hp = hp->next){
				s = p + runlen;
				if(s >= es)
					continue;
				t = hp->s + runlen;
				for(; s >= p; s--)
					if(*s != *t--)
						goto matchloop;
				t += runlen+2;
				s += runlen+2;
				for(; s < es; s++)
					if(*s != *t++)
						break;
				n = s-p;
				if(n > runlen){
					runlen = n;
					q = hp->s;
					if(n == NRUN)
						break;
				}
		matchloop: ;
			}
			if(runlen < NMATCH){
				if(ndump == NDUMP){
					if(eout-outp < ndump+1)
						goto Bfull;
					*outp++ = ndump-1+128;
					memmove(outp, dumpbuf, ndump);
					outp += ndump;
					ndump = 0;
				}
				dumpbuf[ndump++] = *p;
				runlen = 1;
			}
			else{
				if(ndump != 0){
					if(eout-outp < ndump+1)
						goto Bfull;
					*outp++ = ndump-1+128;
					memmove(outp, dumpbuf, ndump);
					outp += ndump;
					ndump = 0;
				}
				offs = p-q-1;
				if(eout-outp < 2)
					goto Bfull;
				*outp++ = ((runlen-NMATCH)<<2) + (offs>>8);
				*outp++ = offs&255;
			}
			for(q = p+runlen; p != q; p++){
				if(cp->prev)
					cp->prev->next = 0;
				cp->next = hash[h].next;
				cp->prev = &hash[h];
				if(cp->next)
					cp->next->prev = cp;
				cp->prev->next = cp;
				cp->s = p;
				if(++cp == &chain[NMEM])
					cp = chain;
				if(edata-p > NMATCH)
					h = hupdate(h, p[NMATCH]);
			}
		}
		if(ndump != 0){
			if(eout-outp < ndump+1)
				goto Bfull;
			*outp++ = ndump-1+128;
			memmove(outp, dumpbuf, ndump);
			outp += ndump;
		}
		line = eline;
		loutp = outp;
		(*nl)++;
	}
Bfull:
	free(hash);
	free(chain);
	return loutp - out;
}

int
writeimage(int fd, Image *i, int)
{
	uchar *outbuf, *data;
	ulong n, nb;
	int bpl, miny, dy, chunk, ncblock, nl;
	Rectangle r;
	char hdr[11+5*12+1];
	char cbuf[20];

	chunk = i->display->bufsize - 32;	/* a little room for header */
	r = i->r;
	bpl = bytesperline(r, i->depth);
	ncblock = _compblocksize(r, i->depth);
	if(ncblock > chunk){
		sprint(hdr, "%11s %11d %11d %11d %11d ",
			chantostr(cbuf, i->chan), r.min.x, r.min.y, r.max.x, r.max.y);
		if(write(fd, hdr, 5*12) != 5*12)
			return -1;
		data = malloc(bpl);
		for(miny = r.min.y; miny != r.max.y; miny++){
			nb = unloadimage(i, Rect(r.min.x, miny, r.max.x, miny+1), data, bpl);
			if(nb != bpl)
				goto ErrOut0;
			if(write(fd, data, nb) != nb)
				goto ErrOut0;
		}
		free(data);
		return 0;
	}

	n = Dy(r)*bpl;
	data = malloc(n);
	if(data == 0){
	ErrOut0:
		free(data);
		return -1;
	}
	for(miny = r.min.y; miny != r.max.y; miny += dy){
		dy = r.max.y-miny;
		if(dy*bpl > chunk)
			dy = chunk/bpl;
		if(dy <= 0)
			dy = 1;
		nb = unloadimage(i, Rect(r.min.x, miny, r.max.x, miny+dy),
			data+(miny-r.min.y)*bpl, dy*bpl);
		if(nb != dy*bpl)
			goto ErrOut0;
	}

	outbuf = malloc(ncblock);
	if(outbuf == 0){
	ErrOut:
		free(outbuf);
		goto ErrOut0;
	}
	sprint(hdr, "compressed\n%11s %11d %11d %11d %11d ",
		chantostr(cbuf, i->chan), r.min.x, r.min.y, r.max.x, r.max.y);
	if(write(fd, hdr, 11+5*12) != 11+5*12)
		goto ErrOut;
	for(miny = r.min.y; miny != r.max.y; miny += nl){
		n = _compresslines(data+(miny-r.min.y)*bpl, bpl, r.max.y-miny, outbuf, ncblock, &nl);
		if((long)n <= 0 || nl == 0)
			goto ErrOut;
		sprint(hdr, "%11d %11ld ", miny+nl, n);
		write(fd, hdr, 2*12);
		write(fd, outbuf, n);
	}
	free(data);
	free(outbuf);
	return 0;
}
