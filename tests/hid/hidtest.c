/*
 * tests/hid: nusb/kb's hid.c on the host -- a precision touchpad
 * (a descriptor as Microsoft's sample: two fingers per report, the
 * contact count, the pad's button; Input Mode in a configuration
 * collection of its own) and what hidinput makes of its fingers.
 */
#include <u.h>
#include <libc.h>
#include <stdarg.h>
#include <stdlib.h>
#include "hid.h"

static uchar desc[] = {
	0x05, 0x0D,		/* Usage Page (Digitizer) */
	0x09, 0x05,		/* Usage (Touch Pad) */
	0xA1, 0x01,		/* Collection (Application) */
	0x85, 0x01,		/*  Report ID (1) */
#define FINGER \
	0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,	/* Usage (Finger), Collection (Logical) */ \
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, \
	0x09, 0x47, 0x81, 0x02,	/* Confidence */ \
	0x09, 0x42, 0x81, 0x02,	/* Tip Switch */ \
	0x95, 0x06, 0x81, 0x03,	/* 6 bits padding */ \
	0x95, 0x01, 0x75, 0x08, 0x25, 0x0F, \
	0x09, 0x51, 0x81, 0x02,	/* Contact Identifier */ \
	0x05, 0x01, 0x75, 0x10, 0x55, 0x0E, 0x65, 0x11,	/* Unit Exponent (-2), Unit (cm) */ \
	0x26, 0xFF, 0x0F, 0x35, 0x00, 0x46, 0xE8, 0x03,	/* Logical Max 4095, Physical Max 1000 */ \
	0x09, 0x30, 0x81, 0x02,	/* X */ \
	0x46, 0x8A, 0x02,	/* Physical Max 650 */ \
	0x09, 0x31, 0x81, 0x02,	/* Y */ \
	0xC0
	FINGER,
	FINGER,
	0x05, 0x0D, 0x55, 0x00, 0x65, 0x00, 0x15, 0x00, 0x25, 0x05, 0x75, 0x08, 0x95, 0x01,
	0x09, 0x54, 0x81, 0x02,	/*  Contact Count */
	0x05, 0x09, 0x09, 0x01, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01,
	0x81, 0x02,		/*  Button 1 */
	0x95, 0x07, 0x81, 0x03,	/*  padding */
	0xC0,
	0x05, 0x0D,		/* Usage Page (Digitizer) */
	0x09, 0x0E,		/* Usage (Device Configuration) */
	0xA1, 0x01,
	0x85, 0x03,		/*  Report ID (3) */
	0x09, 0x22, 0xA1, 0x02,
	0x09, 0x52, 0x15, 0x00, 0x25, 0x0A, 0x75, 0x08, 0x95, 0x01,
	0xB1, 0x02,		/*   Feature: Input Mode */
	0xC0,
	0x09, 0x57, 0x09, 0x58, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02,
	0xB1, 0x02,		/*  Feature: Surface Switch, Button Switch */
	0x95, 0x06, 0xB1, 0x03,
	0xC0,
};

typedef struct Ev Ev;
struct Ev { int abs, x, y, b; };

static Ev ev[256];
static int nev;
static uchar feat[64];
static int nfeat;
static vlong now;
static int nfail;

vlong
nsec(void)
{
	return now * 1000000;
}

void
werrstr(char *fmt, ...)
{
	USED(fmt);
}

static void
mouse(Hid *h, int abs, int x, int y, int b)
{
	USED(h);
	if(nev < nelem(ev)){
		ev[nev].abs = abs;
		ev[nev].x = x;
		ev[nev].y = y;
		ev[nev].b = b;
		nev++;
	}
}

static int
setfeature(Hid *h, uchar *rep, int n)
{
	USED(h);
	memmove(feat, rep, n);
	nfeat = n;
	return 0;
}

typedef struct F F;
struct F { int on, id, x, y, conf; };

/* one report: up to two fingers, the contact count, the button */
static void
report(Hid *h, int cc, F *f, int nf, int button, int ms)
{
	uchar r[15];
	int i;

	now += ms;
	memset(r, 0, sizeof r);
	r[0] = 1;
	for(i = 0; i < 2 && i < nf; i++){
		uchar *p = r + 1 + 6*i;
		p[0] = (f[i].conf ? 1 : 0) | (f[i].on ? 2 : 0);
		p[1] = f[i].id;
		p[2] = f[i].x; p[3] = f[i].x>>8;
		p[4] = f[i].y; p[5] = f[i].y>>8;
	}
	r[13] = cc;
	r[14] = button;
	hidinput(h, r, sizeof r);
}

static void
ok(int c, char *what)
{
	printf("%s %s\n", c ? "ok  " : "FAIL", what);
	if(!c){
		int i;
		nfail++;
		for(i = 0; i < nev; i++)
			printf("\tevent %c %d %d %d\n", "ma"[ev[i].abs], ev[i].x, ev[i].y, ev[i].b);
	}
}

static int
count(int b)
{
	int i, n;

	for(i = n = 0; i < nev; i++)
		if(ev[i].b == b)
			n++;
	return n;
}

int
main(void)
{
	static Hid h;
	F f[2];
	int i, sx, sy;

	h.rep = desc;
	h.nrep = sizeof desc;
	h.mouse = mouse;
	ok(hidtouchpad(&h, setfeature) == 0, "hidtouchpad finds the touchpad");
	ok(nfeat == 3 && feat[0] == 3 && feat[1] == 3 && feat[2] == 3,
		"feature report 3: Input Mode 3, surface and button switches on");
	ok(h.tp.w == 10000 && h.tp.h == 6500, "the pad's size: 100mm by 65mm (Physical Max, cm, exponent -2)");

	/* one finger moved 10mm right, slowly: pointer right, no button */
	nev = 0;
	f[0] = (F){1, 0, 1000, 1000, 1};
	report(&h, 1, f, 1, 0, 1000);
	for(i = 1; i <= 10; i++){
		f[0].x = 1000 + i*41;	/* 4095 to 100mm: 41 a mm */
		report(&h, 1, f, 1, 0, 40);	/* 25mm/s */
	}
	f[0].on = 0;
	report(&h, 1, f, 1, 0, 10);
	sx = sy = 0;
	for(i = 0; i < nev; i++){
		sx += ev[i].x;
		sy += ev[i].y;
	}
	ok(sx >= 90 && sx <= 110 && sy == 0 && count(1) == 0, "one finger, 10mm right at 25mm/s: about 100 pixels right, no click");

	/* the same fast: further */
	nev = 0;
	f[0] = (F){1, 0, 1000, 1000, 1};
	report(&h, 1, f, 1, 0, 1000);
	for(i = 1; i <= 10; i++){
		f[0].x = 1000 + i*41;
		report(&h, 1, f, 1, 0, 4);	/* 250mm/s */
	}
	f[0].on = 0;
	report(&h, 1, f, 1, 0, 10);
	sx = 0;
	for(i = 0; i < nev; i++)
		sx += ev[i].x;
	ok(sx >= 270 && sx <= 310, "one finger, 10mm right at 250mm/s: about 300 pixels");

	/* a tap: down and up within 100ms, not moved */
	nev = 0;
	f[0] = (F){1, 1, 2000, 2000, 1};
	report(&h, 1, f, 1, 0, 1000);
	report(&h, 1, f, 1, 0, 50);
	f[0].on = 0;
	report(&h, 1, f, 1, 0, 50);
	ok(nev == 1 && ev[0].b == 1, "a tap: button 1 pressed (held a while: a drag may follow)");
	now += 250;
	hidtimer(&h);
	ok(nev == 2 && ev[1].b == 0, "no touch after it: hidtimer lets go of it after 200ms");

	/* a tap, a touch right after it, moved, lifted: a drag with button 1 */
	nev = 0;
	f[0] = (F){1, 20, 2000, 2000, 1};
	report(&h, 1, f, 1, 0, 1000);
	f[0].on = 0;
	report(&h, 1, f, 1, 0, 60);
	f[0] = (F){1, 21, 2000, 2000, 1};
	report(&h, 1, f, 1, 0, 80);	/* within 200ms */
	hidtimer(&h);			/* (too early to let go) */
	for(i = 1; i <= 10; i++){
		f[0].x = 2000 + i*41;
		report(&h, 1, f, 1, 0, 40);
	}
	f[0].on = 0;
	report(&h, 1, f, 1, 0, 10);
	now += 300;
	hidtimer(&h);
	sx = 0;
	for(i = 0; i < nev; i++)
		if(ev[i].b == 1)
			sx += ev[i].x;
	ok(ev[0].b == 1 && sx >= 90 && ev[nev-1].b == 0 && count(0) == 1,
		"tap, then touch and move within 200ms: button 1 held while dragging, let go at the end");

	/* a long touch is no tap */
	nev = 0;
	f[0] = (F){1, 2, 2000, 2000, 1};
	report(&h, 1, f, 1, 0, 1000);
	f[0].on = 0;
	report(&h, 1, f, 1, 0, 500);
	ok(nev == 0, "a touch of 500ms: no click");

	/* two fingers tapped: button 3 (mouse(3)'s 4) */
	nev = 0;
	f[0] = (F){1, 3, 1500, 2000, 1};
	f[1] = (F){1, 4, 2500, 2000, 1};
	report(&h, 2, f, 2, 0, 1000);
	f[0].on = f[1].on = 0;
	report(&h, 2, f, 2, 0, 80);
	ok(nev == 2 && ev[0].b == 4 && ev[1].b == 0, "two fingers tapped: button 3");

	/* two fingers 10mm down: scrolled down 4 times (2.5mm each), no motion */
	nev = 0;
	f[0] = (F){1, 5, 1500, 1000, 1};
	f[1] = (F){1, 6, 2500, 1000, 1};
	report(&h, 2, f, 2, 0, 1000);
	for(i = 1; i <= 20; i++){
		f[0].y = f[1].y = 1000 + i*32;	/* 4095 to 65mm: 63 a mm */
		report(&h, 2, f, 2, 0, 10);
	}
	f[0].on = f[1].on = 0;
	report(&h, 2, f, 2, 0, 10);
	sx = sy = 0;
	for(i = 0; i < nev; i++){
		sx += ev[i].x;
		sy += ev[i].y;
	}
	ok(count(16) == 4 && count(8) == 0 && sx == 0 && sy == 0, "two fingers 10mm down: 4 scroll steps down, the pointer stays");

	/* natural scrolling: the other way */
	nev = 0;
	h.tp.natural = 1;
	f[0] = (F){1, 7, 1500, 1000, 1};
	f[1] = (F){1, 8, 2500, 1000, 1};
	report(&h, 2, f, 2, 0, 1000);
	for(i = 1; i <= 20; i++){
		f[0].y = f[1].y = 1000 + i*32;
		report(&h, 2, f, 2, 0, 10);
	}
	f[0].on = f[1].on = 0;
	report(&h, 2, f, 2, 0, 10);
	h.tp.natural = 0;
	ok(count(8) == 4 && count(16) == 0, "natural: the same motion scrolls up");

	/* the pad pressed with one finger, then with two */
	nev = 0;
	f[0] = (F){1, 9, 2000, 2000, 1};
	report(&h, 1, f, 1, 1, 1000);
	report(&h, 1, f, 1, 0, 100);
	f[0].on = 0;
	report(&h, 1, f, 1, 0, 10);
	ok(nev == 2 && ev[0].b == 1 && ev[1].b == 0, "pad pressed with one finger: button 1, no tap after");
	nev = 0;
	f[0] = (F){1, 10, 1500, 2000, 1};
	f[1] = (F){1, 11, 2500, 2000, 1};
	report(&h, 2, f, 2, 1, 1000);
	report(&h, 2, f, 2, 0, 100);
	f[0].on = f[1].on = 0;
	report(&h, 2, f, 2, 0, 10);
	ok(nev == 2 && ev[0].b == 4 && ev[1].b == 0, "pad pressed with two fingers: button 3");

	/* a thumb at the bottom presses, the index finger drags: button 1 held, the pointer moves (not the menu's 3) */
	nev = 0;
	f[0] = (F){1, 14, 1000, 3500, 1};
	f[1] = (F){1, 15, 2000, 1000, 1};
	report(&h, 2, f, 2, 1, 1000);
	for(i = 1; i <= 10; i++){
		f[1].x = 2000 + i*41;
		report(&h, 2, f, 2, 1, 40);
	}
	report(&h, 2, f, 2, 0, 10);
	f[0].on = f[1].on = 0;
	report(&h, 2, f, 2, 0, 10);
	sx = 0;
	for(i = 0; i < nev; i++)
		if(ev[i].b == 1)
			sx += ev[i].x;
	ok(sx >= 90 && ev[nev-1].b == 0 && count(4) == 0 && count(16) == 0,
		"a thumb at the bottom presses, another finger drags: button 1 held, the pointer follows");

	/* a palm (confidence 0) beside a moving finger: still one finger's motion */
	nev = 0;
	f[0] = (F){1, 12, 1000, 1000, 1};
	f[1] = (F){1, 13, 3000, 3000, 0};
	report(&h, 2, f, 2, 0, 1000);
	for(i = 1; i <= 10; i++){
		f[0].x = 1000 + i*41;
		f[1].y = 3000 - i*20;
		report(&h, 2, f, 2, 0, 10);
	}
	f[0].on = f[1].on = 0;
	report(&h, 2, f, 2, 0, 10);
	sx = 0;
	for(i = 0; i < nev; i++)
		sx += ev[i].x;
	ok(sx >= 90 && count(8) == 0 && count(16) == 0, "a palm beside a finger: ignored, the finger moves the pointer");

	/* hybrid: three fingers in two reports (count 3, then 0): one frame */
	nev = 0;
	f[0] = (F){1, 1, 1000, 2000, 1};
	f[1] = (F){1, 2, 2000, 2000, 1};
	report(&h, 3, f, 2, 0, 1000);
	f[0] = (F){1, 3, 3000, 2000, 1};
	report(&h, 0, f, 1, 0, 1);
	f[0] = (F){0, 1, 1000, 2000, 1};
	f[1] = (F){0, 2, 2000, 2000, 1};
	report(&h, 3, f, 2, 0, 60);
	f[0] = (F){0, 3, 3000, 2000, 1};
	report(&h, 0, f, 1, 0, 1);
	ok(nev == 2 && ev[0].b == 2 && ev[1].b == 0, "three fingers in hybrid reports tapped: button 2");

	return nfail != 0;
}
