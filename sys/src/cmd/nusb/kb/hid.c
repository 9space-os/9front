/*
 * HID report descriptors and input reports: keys and mouse(3) events
 * (nfront: out of nusb/kb's kb.c, for nusb/kb and aux/i2chid).
 */

#include <u.h>
#include <libc.h>
#include "hid.h"

int hiddebug;

static int tpinput(Hid*, uchar*, int);

/*
 * scan codes >= 0x80 are extended (E0 XX)
 */
#define isext(sc)	((sc) >= 0x80)

static char sctab[2*256] =
{
/*
 * key code to scan code; for the page table used by
 * the logitech bluetooth keyboard.
 */
[0x00]	0x0,	0x0,	0x0,	0x0,	0x1e,	0x30,	0x2e,	0x20,
[0x08]	0x12,	0x21,	0x22,	0x23,	0x17,	0x24,	0x25,	0x26,
[0x10]	0x32,	0x31,	0x18,	0x19,	0x10,	0x13,	0x1f,	0x14,
[0x18]	0x16,	0x2f,	0x11,	0x2d,	0x15,	0x2c,	0x2,	0x3,
[0x20]	0x4,	0x5,	0x6,	0x7,	0x8,	0x9,	0xa,	0xb,
[0x28]	0x1c,	0x1,	0xe,	0xf,	0x39,	0xc,	0xd,	0x1a,
[0x30]	0x1b,	0x2b,	0x2b,	0x27,	0x28,	0x29,	0x33,	0x34,
[0x38]	0x35,	0x3a,	0x3b,	0x3c,	0x3d,	0x3e,	0x3f,	0x40,
[0x40]	0x41,	0x42,	0x43,	0x44,	0x57,	0x58,	0xe3,	0x46,
[0x48]	0xf7,	0xd2,	0xc7,	0xc9,	0xd3,	0xcf,	0xd1,	0xcd,
[0x50]	0xcb,	0xd0,	0xc8,	0x45,	0x35,	0x37,	0x4a,	0x4e,
[0x58]	0x1c,	0xcf,	0xd0,	0xd1,	0xcb,	0xcc,	0xcd,	0xc7,
[0x60]	0xc8,	0xc9,	0xd2,	0xd3,	0x56,	0xff,	0xf4,	0xf5,
[0x68]	0xd5,	0xd9,	0xda,	0xdb,	0xdc,	0xdd,	0xde,	0xdf,
[0x70]	0xf8,	0xf9,	0xfa,	0xfb,	0x0,	0x0,	0x0,	0x0,
[0x78]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0xf1,
[0x80]	0xf3,	0xf2,	0x0,	0x0,	0x0,	0xfc,	0x0,	0x0,
[0x88]	0x70,	0x0,	0x79,	0x7b,	0x0,	0x0,	0x0,	0x0,
[0x90]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x98]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xa0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xa8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xb0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xb8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xc0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xc8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xd0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xd8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xe0]	0x1d,	0x2a,	0x38,	0xdb,	0x9d,	0x36,	0xb8,	0xfe,
[0xe8]	0x0,	0x0,	0x0,	0x0,	0x0,	0xf3,	0xf2,	0xf1,
[0xf0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xf8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
/* consumer page to scan code */
[0x100]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x108]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x110]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x118]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x120]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x128]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x130]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x138]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x140]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x148]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x150]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x158]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x160]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x168]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x9a,
[0x170]	0x91,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x178]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x180]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x188]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x190]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x198]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x1a0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x1a8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x1b0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x99,	0x90,	0x0,
[0x1b8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x1c0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x1c8]	0x0,	0x0,	0x0,	0x0,	0x0,	0xa2,	0x0,	0x0,
[0x1d0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x1d8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x1e0]	0x0,	0x0,	0xa0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x1e8]	0x0,	0xb0,	0xae,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x1f0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x1f8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
};

static int
signext(int v, int bits)
{
	int s;

	s = sizeof(v)*8 - bits;
	v <<= s;
	v >>= s;
	return v;
}

static int
getbits(uchar *p, uchar *e, int bits, int off)
{
	int v, m;

	p += off/8;
	off %= 8;
	v = 0;
	m = 1;
	if(p < e){
		while(bits--){
			if(*p & (1<<off))
				v |= m;
			if(++off == 8){
				if(++p >= e)
					break;
				off = 0;
			}
			m <<= 1;
		}
	}
	return v;
}

enum {
	Ng	= RepCnt+1,
	UsgCnt	= Delim+1,	/* fake */
	Nl	= UsgCnt+1,
	Nu	= 256,
};

static uchar*
repparse1(uchar *d, uchar *e, int g[], int l[], int c,
	void (*f)(int t, int v, int g[], int l[], int c, void *a), void *a)
{
	int z, k, t, v, i;

	while(d < e){
		v = 0;
		t = *d++;
		z = t & 3, t >>= 2;
		k = t & 3, t >>= 2;
		switch(z){
		case 3:
			d += 4;
			if(d > e) continue;
			v = d[-4] | d[-3]<<8 | d[-2]<<16 | d[-1]<<24;
			break;
		case 2:
			d += 2;
			if(d > e) continue;
			v = d[-2] | d[-1]<<8;
			break;
		case 1:
			d++;
			if(d > e) continue;
			v = d[-1];
			break;
		}
		switch(k){
		case 0:	/* main item*/
			switch(t){
			case Collection:
				/*
				 * nfront: the collection's usage before the locals are
				 * cleared (9front read l[Nu] after: always 0, only
				 * the page; a touchscreen's finger 0x0D0022 was never
				 * recognized)
				 */
				i = l[UsgCnt] > 0 ? l[Nl] : g[UsagPg]<<16;
				memset(l, 0, Nl*sizeof(l[0]));
				l[Usage] = i;
				(*f)(t, v, g, l, c, a);

				d = repparse1(d, e, g, l, v, f, a);

				l[Usage] = i;
				(*f)(CollectionEnd, v, g, l, c, a);
				continue;
			case CollectionEnd:
				return d;
			case Input:
			case Output:
			case Feature:
				if(l[UsgCnt] == 0 && l[UsagMin] != 0 && l[UsagMin] < l[UsagMax])
					for(i=l[UsagMin]; i<=l[UsagMax] && l[UsgCnt] < Nu; i++)
						l[Nl + l[UsgCnt]++] = i;
				for(i=0; i<g[RepCnt]; i++){
					l[Usage] = i < l[UsgCnt] ? l[Nl + i] : 0;
					(*f)(t, v, g, l, c, a);
				}
				break;
			}
			memset(l, 0, Nl*sizeof(l[0]));
			continue;
		case 1:	/* global item */
			if(t == Push){
				int w[Ng];
				memmove(w, g, sizeof(w));
				d = repparse1(d, e, w, l, c, f, a);
			} else if(t == Pop){
				return d;
			} else if(t < Ng){
				if(t == RepId)
					v &= 0xFF;
				else if(t == UsagPg)
					v &= 0xFFFF;
				else if(t != RepSize && t != RepCnt){
					v = signext(v, (z == 3) ? 32 : 8*z);
				}
				g[t] = v;
			}
			continue;
		case 2:	/* local item */
			if(l[Delim] != 0)
				continue;
			if(t == Delim){
				l[Delim] = 1;
			} else if(t < Delim){
				if(z != 3 && (t == Usage || t == UsagMin || t == UsagMax))
					v = (v & 0xFFFF) | (g[UsagPg] << 16);
				l[t] = v;
				if(t == Usage && l[UsgCnt] < Nu)
					l[Nl + l[UsgCnt]++] = v;
			}
			continue;
		case 3:	/* long item */
			if(t == 15)
				d += v & 0xFF;
			continue;
		}
	}
	return d;
}

/*
 * parse the report descriptor and call f for every (Input, Output
 * and Feature) main item as often as it would appear in the report
 * data packet.
 */
static void
repparse(uchar *d, uchar *e,
	void (*f)(int t, int v, int g[], int l[], int c, void *a), void *a)
{
	int l[Nl+Nu], g[Ng];

	memset(l, 0, sizeof(l));
	memset(g, 0, sizeof(g));
	repparse1(d, e, g, l, 0, f, a);
}

static void
hidparse(int t, int f, int g[], int l[], int, void *a)
{
	Hidreport *p = a;
	Hidslot *s = &p->s[p->ns];
	int v, m, cp;

	switch(t){
	case Input:
		if(g[RepId] != 0){
			if(p->p[0] != g[RepId]){
				p->o = 0;
				return;
			}
			if(p->o < 8)
				p->o = 8;	/* skip report id byte */
		}
		break;
	case Collection:
		if(g[RepId] != 0 && p->p[0] != g[RepId])
			return;
		if(s->valid && p->ns < nelem(p->s)-1)
			s = &p->s[++p->ns];
		memset(s, 0, sizeof(*s));
		s->usage = l[Usage];
		s->id = (p->ns+1)<<8 | g[RepId];
		return;
	case CollectionEnd:
		if(g[RepId] != 0 && p->p[0] != g[RepId])
			return;
		if(!s->valid || s->usage != l[Usage])
			return;
		/* if out of range or touchscreen finger not touching, ignore the slot */
		if(s->oor || s->usage == 0x0D0022 && s->b == 0)
			s->valid = 0;
		return;
	default:
		return;
	}
	v = getbits(p->p, p->e, g[RepSize], p->o);
	p->o += g[RepSize];

	if((f & (Fconst|Fdata)) != Fdata)
		return;

	if(hiddebug > 1)
		fprint(2, "hidparse: t=%x f=%x usage=%x v=%x\n", t, f, l[Usage], v);

	if((cp = ((l[Usage]>>16) == 0x0c)) || (l[Usage]>>16) == 0x07){	/* consumer/keycode */
		if((f & (Fvar|Farray)) == Fvar)
			if(v != 0) v = l[Usage] & 0xFF;
		if(p->nk < nelem(p->k) && v != 0)
			p->k[p->nk++] = v | cp*Consumer;
		return;
	}

	if(g[LogiMin] < 0)
		v = signext(v, g[RepSize]);
	if((f & (Fvar|Farray)) == Fvar && v >= g[LogiMin] && v <= g[LogiMax]){
		/*
		 * we use logical units below, but need the
		 * sign to be correct for mouse deltas.
		 * so if physical unit is signed but logical
		 * is unsigned, convert to signed but in logical
		 * units.
		 */
		if((f & (Fabs|Frel)) == Frel
		&& g[PhysMin] < 0 && g[PhysMax] > 0
		&& g[LogiMin] >= 0 && g[LogiMin] < g[LogiMax])
			v -= (g[PhysMax] * (g[LogiMax] - g[LogiMin])) / (g[PhysMax] - g[PhysMin]);

		switch(l[Usage]){
		default:
			return;

		case 0x090001:
		case 0x090002:
		case 0x090003:
		case 0x090004:
		case 0x090005:
		case 0x090006:
		case 0x090007:
		case 0x090008:
			m = 1<<(l[Usage] - 0x090001);
		Button:
			s->m |= m;
			s->b &= ~m;
			if(v != 0)
				s->b |= m;
			break;

		case 0x0D0032:	/* In Range */
			s->oor = !v;
			break;

		case 0x0D0042:	/* Tip Switch */
			m = 1;
			goto Button;
		case 0x0D0044:	/* Barrel Switch */
			m = 2;
			goto Button;
		case 0x0D0045:	/* Eraser */
			m = 4;
			goto Button;

		case 0x0D0048:	/* Contact width */
			s->w = v;
			break;
		case 0x0D0049:	/* Contact height */
			s->h = v;
			break;

		case 0x0D0051:	/* Contact identifier */
			s->id = v;
			break;

		case 0x010030:
			if((f & (Fabs|Frel)) == Fabs){
				v = ((vlong)(v - g[LogiMin]) << 31) / (g[LogiMax] - g[LogiMin]);
				s->abs |= 1;
			}
			s->x = v;
			break;
		case 0x010031:
			if((f & (Fabs|Frel)) == Fabs){
				v = ((vlong)(v - g[LogiMin]) << 31) / (g[LogiMax] - g[LogiMin]);
				s->abs |= 2;
			}
			s->y = v;
			break;
		case 0x010038:
			if((f & (Fabs|Frel)) == Fabs)
				s->abs |= 4;
			s->z = v;
			break;
		}
		s->valid = 1;
	}
}


static ushort *
keykey(ushort *a, ushort k, int n)
{
	while(n > 0){
		if(*a++ == k)
			return a-1;
		n--;
	}
	return nil;
}

/* the bytes of scan code sc to kbdfs(8): 1 or 2 (0: none) */
int
hidscan(uchar *buf, uchar sc, int up)
{
	if(sc == 0)
		return 0;
	if(isext(sc)){
		buf[0] = SCesc1;
		buf[1] = (up ? Keyup : 0) | sc&Keymask;
		return 2;
	}
	buf[0] = (up ? Keyup : 0) | sc&Keymask;
	return 1;
}

/* one input report of n bytes */
void
hidinput(Hid *h, uchar *data, int n)
{
	uchar uks[Nkey], dks[Nkey];
	int i, nuks, ndks, bpress, abs, x, y, z, b;
	Hidreport *p;
	Hidslot *s, *l, *lasts;

	if(h->tp.on && tpinput(h, data, n))
		return;
	p = &h->r;
	lasts = h->lasts;
	if(n > sizeof p->p)
		n = sizeof p->p;
	memset(p->p, 0, sizeof p->p);
	memmove(p->p, data, n);
	p->o = 0;
	p->e = p->p + n;
	p->ns = 0;
	p->nk = 0;
	memset(p->s, 0, sizeof(p->s[0]));
	repparse(h->rep, h->rep+h->nrep, hidparse, p);
	if(p->s[p->ns].valid)
		p->ns++;

	/* handle keyboard report */
	if(p->nk != 0 || h->nlastk != 0){
		if(hiddebug){
			fprint(2, "kbd: ");
			for(i = 0; i < p->nk; i++)
				fprint(2, "%#4.4ux ", p->k[i]);
			fprint(2, "\n");
		}

		/* collect key presses/releases */
		for(i=nuks=0; i<h->nlastk; i++){
			if(keykey(p->k, h->lastk[i], p->nk) == nil)
				uks[nuks++] = sctab[h->lastk[i]];
		}
		for(i=ndks=0; i<p->nk; i++){
			if(keykey(h->lastk, p->k[i], h->nlastk) == nil)
				dks[ndks++] = sctab[p->k[i]];
		}
		if(h->keys != nil && (nuks > 0 || ndks > 0))
			h->keys(h, uks, nuks, dks, ndks);
		memmove(h->lastk, p->k, (h->nlastk = p->nk)*sizeof(h->lastk[0]));
		p->nk = 0;
	}

	/* handle mouse/touchpad */
	if(p->ns == 0)
		return;

	/* combine all the slots */
	bpress = abs = x = y = z = b = 0;
	for(i=0; i<p->ns; *l = *s, i++){
		s = &p->s[i];

		/* find the last slot of the same id */
		for(l = lasts; l->valid && l < &lasts[nelem(h->lasts)-1]; l++)
			if(l->usage == s->usage && l->id == s->id)
				break;
		if(l == &lasts[nelem(h->lasts)-1] || !l->valid)
			*l = *s;

		/* convert absolute z to relative */
		z += s->z;
		if(s->abs & 4)
			z -= l->z;

		if(hiddebug) {
			if((s->abs & 3) == 3)
				fprint(2, "ptr[%d]: id=%x b=%x m=%x x=%f y=%f z=%d\n",
					i, s->id, s->b, s->m,
					(uint)s->x / 2147483648.0,
					(uint)s->y / 2147483648.0,
					s->z);
			else
				fprint(2, "ptr[%d]: id=%x b=%x m=%x x=%d y=%d z=%d\n",
					i, s->id, s->b, s->m, s->x, s->y, s->z);
		}

		/* map to mouse buttons */
		b |= s->b & 1;
		if(s->b & (4|8))
			b |= 2;
		if(s->b & 2)
			b |= 4;
		bpress |= s->m;

		/* X/Y are absolute? */
		if((s->abs & 3) == 3){
			/* ignore absolute position when nothing changed */
			if(s->abs == l->abs && s->x == l->x && s->y == l->y && s->b == l->b)
				continue;
			abs = 1;
			x = s->x;
			y = s->y;
		} else {
			/* everything needs to be relative */
			if((s->abs & 3) != 0 || abs)
				continue;
			x += s->x;
			y += s->y;
		}
	}

	if(bpress == 0)
		b = h->lastb & 7;
	if(z != 0)
		b |= z > 0 ? 8 : 16;

	if(abs || x != 0 || y != 0 || z != 0 || b != h->lastb){
		h->lastb = b;
		if(h->mouse != nil)
			h->mouse(h, abs, x, y, b);
	}
}

/*
 * Precision touchpads (Microsoft's Windows Precision Touchpad; as
 * OpenBSD's hidmt.c finds them: the touchpad's application collection
 * 0x0D0005, the Input Mode feature 0x0D0052 set to 3, a finger per
 * logical collection 0x0D0022 with tip, confidence, contact id, x, y,
 * the contact count 0x0D0054 once per report, hybrid reports after the
 * first with a count of 0).  The gestures are our own: one finger
 * moves, two scroll, the pad pressed with one, two, three fingers is
 * buttons 1, 3, 2 (a thumb resting at the bottom not counted), a tap
 * the same.
 */
enum {
	UInputmode	= 0x0D0052,
	USurface	= 0x0D0057,
	UButtonsw	= 0x0D0058,
	UTouchpad	= 0x0D0005,
	UFinger		= 0x0D0022,

	Nstack		= 16,
	Taptime		= 200,		/* ms */
	Tapmove		= 300,		/* 10µm */
	Scrollstep	= 250,		/* 10µm */
};

typedef struct Hidfield Hidfield;
struct Hidfield
{
	int	usage;
	int	val;
	int	found;
	int	id;
	int	off;
	int	size;
};

typedef struct Tpdesc Tpdesc;
struct Tpdesc
{
	Hidfield	f[3];
	int	off[256];
	int	seen[256];
	int	stack[Nstack];
	int	depth;
	int	intp;
	int	hastp;
	int	xmin, xmax, ymin, ymax;
	int	w, h;
};

typedef struct Tpparse Tpparse;
struct Tpparse
{
	uchar	*p;
	uchar	*e;
	int	o;
	int	stack[Nstack];
	int	depth;
	int	intp;
	int	infinger;
	int	matched;
	Hidfinger	cur;
	Hidfinger	f[Nfinger];
	int	nf;
	int	cc;
	int	btn;
};

/* a physical extent in 10µm, 0 if none or unlikely (20mm to 400mm) */
static int
physsize(int g[])
{
	double v;
	int e, u;

	v = g[PhysMax] - g[PhysMin];
	if(v <= 0)
		return 0;
	e = g[UnitExp] & 0xf;
	if(e > 7)
		e -= 16;
	for(; e > 0; e--)
		v *= 10;
	for(; e < 0; e++)
		v /= 10;
	u = g[UnitTyp] & 0xff;
	if(u == 0x11)
		v *= 1000;	/* cm */
	else if(u == 0x13)
		v *= 2540;	/* inch */
	else
		return 0;
	if(v < 2000 || v > 40000)
		return 0;
	return v;
}

static void
push(int *stack, int *depth, int *intp, int usage)
{
	if(*depth < Nstack)
		stack[*depth] = usage;
	(*depth)++;
	if(usage == UTouchpad)
		(*intp)++;
}

static void
pop(int *stack, int *depth, int *intp)
{
	if(*depth <= 0)
		return;
	(*depth)--;
	if(*depth < Nstack && stack[*depth] == UTouchpad)
		(*intp)--;
}

static void
tpdesc(int t, int f, int g[], int l[], int, void *a)
{
	Tpdesc *d = a;
	Hidfield *x;
	int id;

	USED(f);
	switch(t){
	case Collection:
		push(d->stack, &d->depth, &d->intp, l[Usage]);
		if(l[Usage] == UTouchpad)
			d->hastp = 1;
		return;
	case CollectionEnd:
		pop(d->stack, &d->depth, &d->intp);
		return;
	case Input:
		if(d->intp == 0)
			return;
		if(l[Usage] == 0x010030 && d->xmax <= d->xmin){
			d->xmin = g[LogiMin];
			d->xmax = g[LogiMax];
			d->w = physsize(g);
		}
		if(l[Usage] == 0x010031 && d->ymax <= d->ymin){
			d->ymin = g[LogiMin];
			d->ymax = g[LogiMax];
			d->h = physsize(g);
		}
		return;
	case Feature:
		id = g[RepId] & 0xff;
		if(!d->seen[id]){
			d->seen[id] = 1;
			d->off[id] = 8;	/* the id's byte, also id 0's (setfeature drops it) */
		}
		for(x = d->f; x < d->f + nelem(d->f); x++)
			if(!x->found && l[Usage] == x->usage){
				x->found = 1;
				x->id = id;
				x->off = d->off[id];
				x->size = g[RepSize];
			}
		d->off[id] += g[RepSize];
		return;
	}
}

static void
putbits(uchar *p, int off, int bits, int v)
{
	int i;

	for(i = 0; i < bits; i++, off++)
		if(v & 1<<i)
			p[off/8] |= 1 << off%8;
		else
			p[off/8] &= ~(1 << off%8);
}

int
hidtouchpad(Hid *h, int (*setfeature)(Hid*, uchar *rep, int n))
{
	static Tpdesc d;
	uchar buf[64];
	Hidtouch *t;
	Hidfield *x, *y;
	int n;

	memset(&d, 0, sizeof d);
	d.f[0].usage = UInputmode;
	d.f[0].val = 3;		/* touchpad */
	d.f[1].usage = USurface;
	d.f[1].val = 1;
	d.f[2].usage = UButtonsw;
	d.f[2].val = 1;
	repparse(h->rep, h->rep+h->nrep, tpdesc, &d);
	if(!d.hastp || !d.f[0].found){
		werrstr("no precision touchpad");
		return -1;
	}
	if(d.xmax <= d.xmin || d.ymax <= d.ymin){
		werrstr("touchpad without x, y");
		return -1;
	}
	t = &h->tp;
	memset(t, 0, sizeof *t);
	t->xmin = d.xmin;
	t->xmax = d.xmax;
	t->ymin = d.ymin;
	t->ymax = d.ymax;
	t->w = d.w != 0 ? d.w : 10000;		/* 100mm by 65mm, a guess */
	t->h = d.h != 0 ? d.h : 6500;
	if(hiddebug)
		fprint(2, "touchpad: x %d-%d y %d-%d, %d.%.2d by %d.%.2d mm\n",
			t->xmin, t->xmax, t->ymin, t->ymax, t->w/100, t->w%100, t->h/100, t->h%100);

	/* the feature reports: Input Mode's, then the switches' if elsewhere */
	for(x = d.f; x < d.f + nelem(d.f); x++){
		if(!x->found || x->size == 0)
			continue;
		for(y = d.f; y < x; y++)
			if(y->found && y->id == x->id)
				break;
		if(y < x)
			continue;	/* sent with an earlier one */
		n = (d.off[x->id] + 7) / 8;
		if(n > sizeof buf)
			continue;
		memset(buf, 0, n);
		buf[0] = x->id;
		for(y = x; y < d.f + nelem(d.f); y++)
			if(y->found && y->id == x->id)
				putbits(buf, y->off, y->size, y->val);
		if((*setfeature)(h, buf, n) < 0 && x == d.f)
			return -1;
	}
	t->on = 1;
	return 0;
}

static void
tpparse(int t, int f, int g[], int l[], int, void *a)
{
	Tpparse *p = a;
	int v;

	switch(t){
	case Collection:
		push(p->stack, &p->depth, &p->intp, l[Usage]);
		if(p->intp > 0 && l[Usage] == UFinger){
			p->infinger = 1;
			memset(&p->cur, 0, sizeof p->cur);
			p->cur.conf = 1;	/* unless it says */
		}
		return;
	case CollectionEnd:
		if(p->infinger && l[Usage] == UFinger){
			if(p->nf < Nfinger)
				p->f[p->nf++] = p->cur;
			p->infinger = 0;
		}
		pop(p->stack, &p->depth, &p->intp);
		return;
	case Input:
		break;
	default:
		return;
	}
	if(g[RepId] != 0){
		if(p->p[0] != g[RepId])
			return;
		if(p->o < 8)
			p->o = 8;
	}
	v = getbits(p->p, p->e, g[RepSize], p->o);
	p->o += g[RepSize];
	if((f & (Fconst|Fdata)) != Fdata || p->intp == 0)
		return;
	p->matched = 1;
	if(g[LogiMin] < 0)
		v = signext(v, g[RepSize]);
	switch(l[Usage]){
	case 0x0D0042:	/* Tip Switch */
		p->cur.tip = v != 0;
		break;
	case 0x0D0047:	/* Confidence */
		p->cur.conf = v != 0;
		break;
	case 0x0D0051:	/* Contact identifier */
		p->cur.id = v;
		break;
	case 0x010030:
		p->cur.x = v;
		break;
	case 0x010031:
		p->cur.y = v;
		break;
	case 0x0D0054:	/* Contact Count */
		p->cc = v;
		break;
	case 0x090001:
	case 0x090002:
	case 0x090003:
		if(v != 0)
			p->btn = 1;
		break;
	}
}

static void
tpmouse(Hid *h, int dx, int dy, int b)
{
	Hidtouch *t = &h->tp;

	if(dx == 0 && dy == 0 && b == t->b)
		return;
	t->b = b;
	if(h->mouse != nil)
		h->mouse(h, 0, dx, dy, b);
}

/*
 * motion of 10µm in dt ms to pixels: 10 a mm up to 40mm/s, 30 a mm
 * from 200mm/s (the speed, not the motion of a report: pads report
 * at different rates)
 */
static int
topixels(int *frac, int u, int dt)
{
	int s, g, d;

	if(dt < 1)
		dt = 1;
	s = (u < 0 ? -u : u) * 10 / dt;	/* mm/s */
	g = 100;
	if(s > 40)
		g += 100 * (s > 200 ? 160 : s - 40) / 80;
	*frac += u * g;
	d = *frac / 1000;
	*frac -= d * 1000;
	return d;
}

/* a frame's fingers: the gestures */
static void
tpframe(Hid *h)
{
	Hidtouch *t = &h->tp;
	Hidfinger cur[Nfinger], *c, *l;
	int i, n, nm, ux, uy, sx, sy, mx, my, dx, dy, b, tap, moving, up;
	vlong now;

	now = nsec() / 1000000;
	n = 0;
	for(i = 0; i < t->nf; i++)
		if(t->f[i].tip && t->f[i].conf && n < nelem(cur))
			cur[n++] = t->f[i];

	/*
	 * the pad pressed: the button by the fingers on it, not counting
	 * one resting in the bottom fifth (a thumb that presses while
	 * another finger drags: button 1, not the menu's 3)
	 */
	if(t->pbutton && t->button == 0){
		up = 0;
		for(c = cur; c < cur + n; c++)
			if(c->y < t->ymin + (t->ymax - t->ymin) * 4 / 5)
				up++;
		if(up == 0)
			up = n;
		t->button = up <= 1 ? 1 : up == 2 ? 4 : 2;
		t->clicked = 1;
	}
	if(!t->pbutton)
		t->button = 0;

	/* the fingers' motion since the last frame */
	nm = sx = sy = mx = my = 0;
	moving = 0;
	for(c = cur; c < cur + n; c++){
		for(l = t->last; l < t->last + t->nlast; l++)
			if(l->id == c->id)
				break;
		if(l == t->last + t->nlast)
			continue;	/* a new finger */
		ux = (vlong)(c->x - l->x) * t->w / (t->xmax - t->xmin);
		uy = (vlong)(c->y - l->y) * t->h / (t->ymax - t->ymin);
		/* the finger that moved most (the pad held by one, dragged by another) */
		if(nm == 0 || abs(ux) + abs(uy) > abs(mx) + abs(my)){
			mx = ux;
			my = uy;
			moving = 1;
		}
		sx += ux;
		sy += uy;
		nm++;
		t->moved += (ux < 0 ? -ux : ux) + (uy < 0 ? -uy : uy);
	}

	/* a touch: when it began, how many fingers at most */
	if(n > 0 && t->nlast == 0){
		t->t0 = now;
		t->maxn = 0;
		t->moved = 0;
		t->clicked = t->pbutton != 0;
		t->fx = t->fy = t->scroll = 0;
	}
	if(n > t->maxn)
		t->maxn = n;

	b = t->button;
	dx = dy = 0;
	if(moving && (n == 1 || t->button != 0)){
		i = now - t->tlast > 100 ? 100 : now - t->tlast;
		dx = topixels(&t->fx, mx, i);
		dy = topixels(&t->fy, my, i);
	} else if(nm == 2 && n == 2){
		t->scroll += (t->natural ? -sy : sy) / 2;
		while(t->scroll >= Scrollstep){
			tpmouse(h, 0, 0, b|16);
			tpmouse(h, 0, 0, b);
			t->scroll -= Scrollstep;
		}
		while(t->scroll <= -Scrollstep){
			tpmouse(h, 0, 0, b|8);
			tpmouse(h, 0, 0, b);
			t->scroll += Scrollstep;
		}
	}
	tpmouse(h, dx, dy, b);

	/* all fingers up: was it a tap? */
	if(n == 0 && t->nlast > 0 && !t->clicked
	&& now - t->t0 < Taptime && t->moved < Tapmove){
		tap = t->maxn <= 1 ? 1 : t->maxn == 2 ? 4 : 2;
		tpmouse(h, 0, 0, b|tap);
		tpmouse(h, 0, 0, b);
	}
	memmove(t->last, cur, n * sizeof cur[0]);
	t->nlast = n;
	t->tlast = now;
}

/* a report of the touchpad's: 1, or 0 if it is another one's */
static int
tpinput(Hid *h, uchar *data, int n)
{
	static Tpparse p;
	Hidtouch *t = &h->tp;
	int i;

	memset(&p, 0, sizeof p);
	p.p = data;
	p.e = data + n;
	p.cc = -1;
	repparse(h->rep, h->rep+h->nrep, tpparse, &p);
	if(!p.matched)
		return 0;
	if(hiddebug > 1)
		for(i = 0; i < p.nf; i++)
			fprint(2, "finger[%d]: id %d tip %d conf %d x %d y %d (count %d button %d)\n",
				i, p.f[i].id, p.f[i].tip, p.f[i].conf, p.f[i].x, p.f[i].y, p.cc, p.btn);
	if(p.cc != 0 || t->want == 0){
		/* a frame's first report (or all of it) */
		t->nf = 0;
		t->want = p.cc > 0 ? p.cc : p.cc < 0 ? p.nf : 0;
		t->pbutton = p.btn;
	} else
		t->pbutton |= p.btn;
	for(i = 0; i < p.nf && t->nf < t->want && t->nf < Nfinger; i++)
		t->f[t->nf++] = p.f[i];
	if(t->nf >= t->want || t->nf == Nfinger){
		tpframe(h);
		t->want = 0;
	}
	return 1;
}
