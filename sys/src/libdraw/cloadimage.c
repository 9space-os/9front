#include <u.h>
#include <libc.h>
#include <draw.h>

int
cloadimage(Image *i, Rectangle r, uchar *data, int ndata)
{
	int m, nb, miny, maxy, ncblock;

	if(!rectinrect(r, i->r)){
		werrstr("cloadimage: bad rectangle");
		return -1;
	}

	miny = r.min.y;
	m = 0;
	ncblock = _compblocksize(r, i->depth);
	while(miny != r.max.y){
		maxy = atoi((char*)data+0*12);
		nb = atoi((char*)data+1*12);
		if(maxy<=miny || r.max.y<maxy){
			werrstr("cloadimage: bad maxy %d", maxy);
			return -1;
		}
		data += 2*12;
		ndata -= 2*12;
		m += 2*12;
		if(nb<=0 || ncblock<nb || nb>ndata){
			werrstr("cloadimage: bad count %d", nb);
			return -1;
		}

		_lockdisplay(i->display);
		if(drawcmd(i->display, "blllll<", 'Y', i->id, r.min.x, miny, r.max.x, maxy, nb, data) < 0){
			_unlockdisplay(i->display);
			return -1;
		}
		_unlockdisplay(i->display);

		miny = maxy;
		data += nb;
		ndata += nb;
		m += nb;
	}
	return m;
}

/*
 * 9space: rectangle r of i from the uncompressed data (rows of
 * bytesperline(r, depth) bytes, as loadimage's), compressed here into
 * image(6)'s blocks, each a 'Y' message as big as the display's buffer:
 * fewer bytes, and fewer writes, to a display far away (drawterm, rcpu);
 * a line that does not get smaller goes as loadimage's
 */
int
zloadimage(Image *i, Rectangle r, uchar *data, int ndata)
{
	uchar *out;
	int bpl, miny, nl, n, nout;
	Display *d;

	if(!rectinrect(r, i->r)){
		werrstr("zloadimage: bad rectangle");
		return -1;
	}
	bpl = bytesperline(r, i->depth);
	if(ndata < bpl*Dy(r)){
		werrstr("zloadimage: buffer too small");
		return -1;
	}
	d = i->display;
	nout = d->bufsize - (1+4+4*4) - 32;
	if(nout < 64)
		return loadimage(i, r, data, ndata);
	out = malloc(nout);
	if(out == nil)
		return -1;
	for(miny = r.min.y; miny < r.max.y; miny += nl){
		n = _compresslines(data+(miny-r.min.y)*bpl, bpl, r.max.y-miny, out, nout, &nl);
		if(n < 0){
			free(out);
			return -1;
		}
		if(nl == 0 || n >= nl*bpl){
			/* not one line fits compressed, or no smaller: as it is */
			nl = nl == 0 ? 1 : nl;
			if(loadimage(i, Rect(r.min.x, miny, r.max.x, miny+nl), data+(miny-r.min.y)*bpl, nl*bpl) < 0){
				free(out);
				return -1;
			}
			continue;
		}
		_lockdisplay(d);
		if(drawcmd(d, "blllll<", 'Y', i->id, r.min.x, miny, r.max.x, miny+nl, n, out) < 0){
			_unlockdisplay(d);
			free(out);
			return -1;
		}
		_unlockdisplay(d);
	}
	free(out);
	return ndata;
}
