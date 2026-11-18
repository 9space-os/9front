#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

extern Memimage* gscreen;

/*
 *  mouse types
 */
enum
{
	Mouseother=	0,
	Mouseserial=	1,
	MousePS2=	2,
};

/*
 * bytes which correspond
 * to mouse buttons
 */
enum
{
	Mousereleas= 0x8, 
	Mouseleft= 0x9, 
	Mousemid= 0xc, 
	Mouseright= 0xa
};

static QLock mousectlqlock;
static int mousetype;
static int intellimouse;
static int packetsize;
static int resolution;
static int accelerated;
static int mousehwaccel;
static int synaptic;
static volatile int disabletouch;
static int midscroll;	/* 9space: the middle button and motion scroll */
static char mouseport[5];

enum
{
	CMaccelerated,
	CMhwaccel,
	CMintellimouse,
	CMlinear,
	CMps2,
	CMps2intellimouse,
	CMres,
	CMreset,
	CMserial,
	CMtouchpad,
	CMsynaptic,
	CMmidscroll,
	CMtap,
};

static Cmdtab mousectlmsg[] =
{
	CMaccelerated,		"accelerated",		0,
	CMhwaccel,		"hwaccel",		2,
	CMintellimouse,		"intellimouse",		1,
	CMlinear,		"linear",		1,
	CMps2,			"ps2",			1,
	CMps2intellimouse,	"ps2intellimouse",	1,
	CMres,			"res",			0,
	CMreset,		"reset",		1,
	CMserial,		"serial",		0,
	CMtouchpad,		"touchpad",		2,
	CMsynaptic,		"synaptic",		1,
	CMmidscroll,		"midscroll",		2,
	CMtap,			"tap",			2,
};

/*
 *  ps/2 mouse message is three bytes
 *
 *	byte 0 -	0 0 SDY SDX 1 M R L
 *	byte 1 -	DX
 *	byte 2 -	DY
 *
 *  shift & right button is the same as middle button
 *
 * Intellimouse and AccuPoint with extra buttons deliver
 *	byte 3 -	00 or 01 or FF according to extra button state.
 * extra buttons are mapped in this code to buttons 4 and 5.
 * AccuPoint generates repeated events for these buttons;
*  it and Intellimouse generate 'down' events only, so
 * user-level code is required to generate button 'up' events
 * if they are needed by the application.
 * Also on laptops with AccuPoint AND external mouse, the
 * controller may deliver 3 or 4 bytes according to the type
 * of the external mouse; code must adapt.
 *
 * On the NEC Versa series (and perhaps others?) we seem to
 * lose a byte from the packet every once in a while, which
 * means we lose where we are in the instruction stream.
 * To resynchronize, if we get a byte more than two seconds
 * after the previous byte, we assume it's the first in a packet.
 */
/*
 * 9space: scrolling by the middle button, as a TrackPoint's (libinput's
 * on-button scrolling, OpenBSD's wsmouse WSMOUSECFG_*: the same idea):
 * with midscroll on, the middle button held and moved scrolls (buttons
 * 8 and 16, one per Scrollstep of motion up or down, the pointer stays);
 * pressed and released without motion it is a middle click then, on its
 * release.  Off by default: rio's menu and acme's sweeps hold the middle
 * button and move.
 */
enum {
	Scrollstep	= 8,
	Scrollslop	= 3,	/* motion below this is still a click */
};

static void
midscrolltrack(int dx, int dy, int buttons, ulong ms)
{
	static int held, scrolling, moved, acc;

	if(buttons & 2){
		if(!held){
			held = 1;
			scrolling = 0;
			moved = 0;
			acc = 0;
		}
		moved += abs(dx) + abs(dy);
		if(!scrolling && moved > Scrollslop)
			scrolling = 1;
		if(scrolling){
			acc += dy;
			while(acc <= -Scrollstep){
				mousetrack(0, 0, (buttons&~2) | 8, ms);
				mousetrack(0, 0, buttons&~2, ms);
				acc += Scrollstep;
			}
			while(acc >= Scrollstep){
				mousetrack(0, 0, (buttons&~2) | 16, ms);
				mousetrack(0, 0, buttons&~2, ms);
				acc -= Scrollstep;
			}
		}
		return;
	}
	if(held){
		held = 0;
		if(!scrolling){
			mousetrack(0, 0, buttons | 2, ms);
			mousetrack(0, 0, buttons, ms);
		}
	}
	mousetrack(dx, dy, buttons, ms);
}

static void
ps2mouseputc(int c, int shift)
{
	static short msg[4];
	static int nb;
	static uchar b[] = {0, 1, 4, 5, 2, 3, 6, 7, 0, 1, 2, 3, 2, 3, 6, 7 };
	int buttons, dx, dy;

	/*
	 * Resynchronize in stream with timing; see comment above.
	 */
	if(lastmousems() > 2000)
		nb = 0;

	/*
	 *  check byte 0 for consistency
	 */
	if(nb==0 && (c&0xc8)!=0x08){
		if(intellimouse && (c==0x00 || c==0x01 || c==0xFF)){
			/* last byte of 4-byte packet */
			packetsize = 4;
		}
		return;
	}

	msg[nb] = c;
	if(++nb >= packetsize){
		nb = 0;
		if(msg[0] & 0x10)
			msg[1] |= 0xFF00;
		if(msg[0] & 0x20)
			msg[2] |= 0xFF00;

		buttons = b[(msg[0]&7) | (shift ? 8 : 0)];
		if(intellimouse && packetsize==4){
			if((msg[3]&0xc8) == 0x08){
				/* first byte of 3-byte packet */
				packetsize = 3;
				msg[0] = msg[3];
				nb = 1;
				/* fall through to emit previous packet */
			}else{
				/* The AccuPoint on the Toshiba 34[48]0CT
				 * encodes extra buttons as 4 and 5. They repeat
				 * and don't release, however, so user-level
				 * timing code is required. Furthermore,
				 * intellimice with 3buttons + scroll give a
				 * two's complement number in the lower 4 bits
				 * (bit 4 is sign extension) that describes
				 * the amount the scroll wheel has moved during
				 * the last sample. Here we use only the sign to
				 * decide whether the wheel is moving up or down
				 * and generate a single button 4 or 5 click
				 * accordingly.
				 */
				if((msg[3] >> 3) & 1)
					buttons |= 1<<3;
				else if(msg[3] & 0x7)
					buttons |= 1<<4;
			}
		}
		dx = msg[1];
		dy = -msg[2];
		if(midscroll){
			midscrolltrack(dx, dy, buttons, TK2MS(MACHP(0)->PMach.ticks));
			return;
		}
		mousetrack(dx, dy, buttons, TK2MS(MACHP(0)->PMach.ticks));
	}
}

/*
 * 9space: a Synaptics touchpad in absolute and W mode (setabs), as a
 * laptop's pad is used: one finger moves the pointer by its motion,
 * two scroll (buttons 8 and 16), a click of the pad is button 1 with
 * one finger on it, 3 (right) with two, 2 (middle) with three; its
 * TrackPoint's packets pass through (W 3), and on pads that wire the
 * TrackPoint's buttons to themselves those come as its buttons.
 * After OpenBSD's pms.c (pms_proc_synaptics, synaptics_get_hwinfo;
 * Copyright (c) 1994 Charles M. Hannum, (c) 1992, 1993 Erik Forsberg,
 * BSD) and its pmsreg.h's values; 9front's synmouseputc mapped the pad
 * onto the screen (absolute) instead.
 */
enum {
	/* queries (pmsreg.h) */
	SynQidentify	= 0x00,
	SynQcaps	= 0x02,
	SynQmodel	= 0x03,
	SynQextmodel	= 0x09,
	SynQextcaps	= 0x0c,
	SynQmodes	= 0x01,
	SynQext2caps	= 0x10,

	SynCapext	= 1<<23,
	SynCapmulti	= 1<<1,
	SynCappass	= 1<<7,
	SynExtclickpad	= 1<<20,
	SynExt2cap	= 1<<17,	/* in the modes: ext2 caps queried */
	SynExt2stick	= 1<<16,	/* the TrackPoint's buttons on the pad */

	Syntouch	= 30,	/* z of a finger on it */
	Synpalm		= 13,	/* a width this wide is a palm */
	Syndiv		= 6,	/* pad units per pointer unit */
	Synscroll	= 70,	/* pad units of two fingers per wheel click */
	Syntapms	= 180,	/* a touch this short, */
	Syntapmove	= 60,	/* this still (pad units), is a tap */
};

static struct {
	uint	id, caps, model, extmodel, extcaps, modes, ext2caps;
	int	lastx, lasty, havepos;	/* the finger's last place */
	int	fingers;
	int	remx, remy, acc;	/* motion and scroll not yet sent */
	int	click;		/* the pad's click: the buttons it became */
	int	stick;		/* the TrackPoint's buttons wired to the pad */
	int	notap;		/* ctl tap off */
	ulong	touchms;	/* this touch's start */
	int	touchx, touchy, touchmax, touchmoved, touchclicked;
} syn;

static int
synquery(int q, uint *v)
{
	uchar cmd[9], r[3];
	int i;

	/* the query's number as four resolution commands, then a status request */
	for(i = 0; i < 4; i++){
		cmd[2*i] = 0xE8;
		cmd[2*i+1] = (q >> (6 - 2*i)) & 3;
	}
	cmd[8] = 0xE9;
	if(i8042auxquery(cmd, 9, r, 3) < 0)
		return -1;
	*v = r[0]<<16 | r[1]<<8 | r[2];
	return 0;
}

static void
synhwinfo(void)
{
	memset(&syn, 0, sizeof syn);
	if(synquery(SynQidentify, &syn.id) < 0 || ((syn.id >> 8) & 0xff) != 0x47){
		print("synaptics: no touchpad (identify %6.6ux)\n", syn.id);
		return;
	}
	synquery(SynQcaps, &syn.caps);
	synquery(SynQmodel, &syn.model);
	if(((syn.caps >> 20) & 7) >= 1)
		synquery(SynQextmodel, &syn.extmodel);
	if(((syn.caps >> 20) & 7) >= 4)
		synquery(SynQextcaps, &syn.extcaps);
	if(((syn.id & 0xf) << 8 | ((syn.id >> 16) & 0xff)) >= 0x705){
		synquery(SynQmodes, &syn.modes);
		if(syn.modes & SynExt2cap)
			synquery(SynQext2caps, &syn.ext2caps);
	}
	print("synaptics: %s, firmware %d.%d, caps %6.6ux model %6.6ux ext model %6.6ux ext caps %6.6ux ext2 caps %6.6ux\n",
		syn.extcaps & SynExtclickpad ? "clickpad" : "touchpad",
		syn.id & 0xf, (syn.id >> 16) & 0xff,
		syn.caps, syn.model, syn.extmodel, syn.extcaps, syn.ext2caps);
}

/* Plan 9's buttons of a TrackPoint packet's 3 bits (left, right, middle) */
static int
synbuttons(int bits)
{
	return (bits & 1) | (bits & 2) << 1 | (bits & 4) >> 1;
}

static void
synpacket(uchar *p, int shift)
{
	int w, x, y, z, fingers, buttons, dx, dy, click;
	ulong ms;

	ms = TK2MS(MACHP(0)->PMach.ticks);
	w = ((p[0] & 0x30) >> 2) | ((p[0] & 0x04) >> 1) | ((p[3] & 0x04) >> 2);
	z = p[2];
	if((syn.caps & SynCapext) == 0){
		/* no W mode: bit 3 is a touch */
		if(w & 8)
			w = 4;
		else
			z = w = 0;
	}

	/* the TrackPoint */
	if(w == 3){
		dx = p[4] - ((p[1] & 0x10) ? 256 : 0);
		dy = -(p[5] - ((p[1] & 0x20) ? 256 : 0));
		buttons = synbuttons(p[1] & 7) | syn.stick;
		if(shift && (buttons & 4))
			buttons = (buttons & ~4) | 2;
		if(midscroll)
			midscrolltrack(dx, dy, buttons, ms);
		else
			mousetrack(dx, dy, buttons, ms);
		return;
	}
	if(w == 2)
		return;		/* extended W mode, not asked for */

	/* the TrackPoint's buttons on the pad (pmsreg.h's BUTTONS_STICK) */
	if(((syn.extmodel >> 12) & 0xf) != 0 && ((p[0] ^ p[3]) & 2)
	&& (syn.ext2caps & SynExt2stick)){
		syn.stick = synbuttons((p[4] & 1) | (p[5] & 1) << 1 | (p[4] & 2) << 1);
		mousetrack(0, 0, syn.stick, ms);
		return;
	}

	if(disabletouch)
		return;

	x = (p[3] & 0x10) << 8 | (p[1] & 0x0f) << 8 | p[4];
	y = (p[3] & 0x20) << 7 | (p[1] & 0xf0) << 4 | p[5];

	if(z < Syntouch)
		fingers = 0;
	else if((syn.caps & SynCapmulti) && w == 0)
		fingers = 2;
	else if((syn.caps & SynCapmulti) && w == 1)
		fingers = 3;
	else if(w >= Synpalm)
		fingers = 0;	/* a palm */
	else
		fingers = 1;

	/* the pad's own buttons; a clickpad's click by the fingers on it */
	buttons = ((p[0] & p[3]) & 1) ? 1 : 0;
	buttons |= ((p[0] & p[3]) & 2) ? 4 : 0;
	click = (syn.extcaps & SynExtclickpad) && ((p[0] ^ p[3]) & 1);
	if(click && syn.click == 0)
		syn.click = fingers >= 3 ? 2 : fingers == 2 ? 4 : 1;
	else if(!click)
		syn.click = 0;
	buttons |= syn.click | syn.stick;

	/*
	 * a tap: a short touch that hardly moved and did not press the
	 * pad clicks as the press would (the most fingers it had)
	 */
	if(fingers > 0 && syn.fingers == 0){
		syn.touchms = ms;
		syn.touchx = x;
		syn.touchy = y;
		syn.touchmax = fingers;
		syn.touchmoved = 0;
		syn.touchclicked = 0;
	} else if(fingers > 0){
		if(fingers > syn.touchmax)
			syn.touchmax = fingers;
		if(abs(x - syn.touchx) + abs(y - syn.touchy) > Syntapmove)
			syn.touchmoved = 1;
	}
	if(syn.click)
		syn.touchclicked = 1;
	if(fingers == 0 && syn.fingers > 0 && !syn.notap
	&& !syn.touchmoved && !syn.touchclicked && ms - syn.touchms < Syntapms){
		click = syn.touchmax >= 3 ? 2 : syn.touchmax == 2 ? 4 : 1;
		mousetrack(0, 0, buttons | click, ms);
		mousetrack(0, 0, buttons, ms);
	}

	dx = dy = 0;
	if(fingers != syn.fingers){
		/* another number of fingers: their place anew */
		syn.fingers = fingers;
		syn.havepos = 0;
		syn.acc = 0;
	} else if(syn.havepos && (fingers == 1 || syn.click != 0)){
		/*
		 * one finger moves; so does any while the pad is pressed:
		 * a menu is held and dragged (rio's, wsys')
		 */
		syn.remx += x - syn.lastx;
		syn.remy += y - syn.lasty;
		dx = syn.remx / Syndiv;
		dy = -(syn.remy / Syndiv);
		syn.remx %= Syndiv;
		syn.remy %= Syndiv;
	} else if(syn.havepos && fingers == 2 && syn.click == 0){
		/* two fingers up: the wheel up (button 8) */
		syn.acc += y - syn.lasty;
		while(syn.acc >= Synscroll){
			mousetrack(0, 0, buttons | 8, ms);
			mousetrack(0, 0, buttons, ms);
			syn.acc -= Synscroll;
		}
		while(syn.acc <= -Synscroll){
			mousetrack(0, 0, buttons | 16, ms);
			mousetrack(0, 0, buttons, ms);
			syn.acc += Synscroll;
		}
	}
	if(fingers > 0){
		syn.lastx = x;
		syn.lasty = y;
		syn.havepos = 1;
	}
	mousetrack(dx, dy, buttons, ms);
}

static void
synmouseputc(int c, int shift)
{
	static uchar msg[6];
	static int nb;

	/* a new packet after a pause; else the packet's bytes as pms_sync_synaptics checks them */
	if(lastmousems() > 500)
		nb = 0;
	c &= 0xff;
	if(nb == 0 && (c & 0xc8) != 0x80)
		return;
	if(nb == 3 && (c & 0xc8) != 0xc0){
		nb = 0;
		return;
	}
	msg[nb++] = c;
	if(nb < 6)
		return;
	nb = 0;
	synpacket(msg, shift);
}

/*
 *  set up a ps2 mouse
 */
static void
ps2mouse(void)
{
	if(mousetype == MousePS2)
		return;

	mousetype = MousePS2;
	packetsize = 3;
	mousehwaccel = 0;

	i8042auxenable(ps2mouseputc);
	i8042auxcmd(0xEA);	/* set stream mode */
}

/*
 * The PS/2 Trackpoint multiplexor on the IBM Thinkpad T23 ignores
 * acceleration commands.  It is supposed to pass them on
 * to the attached device, but my Logitech mouse is simply
 * not behaving any differently.  For such devices, we allow
 * the user to use "hwaccel off" to tell us to back off to
 * software acceleration even if we're using the PS/2 port.
 * (Serial mice are always software accelerated.)
 * For more information on the Thinkpad multiplexor, see
 * http://wwwcssrv.almaden.ibm.com/trackpoint/
 */
static void
setaccelerated(int x)
{
	accelerated = x;
	if(mousehwaccel){
		switch(mousetype){
		case MousePS2:
			i8042auxcmd(0xE7);
			return;
		}
	}
	mouseaccelerate(x);
}

static void
setlinear(void)
{
	accelerated = 0;
	if(mousehwaccel){
		switch(mousetype){
		case MousePS2:
			i8042auxcmd(0xE6);
			return;
		}
	}
	mouseaccelerate(0);
}

static void
setres(int n)
{
	resolution = n;
	switch(mousetype){
	case MousePS2:
		i8042auxcmd(0xE8);
		i8042auxcmd(n);
		break;
	}
}

static void
setintellimouse(void)
{
	intellimouse = 1;
	packetsize = 4;
	switch(mousetype){
	case MousePS2:
		i8042auxcmd(0xF3);	/* set sample */
		i8042auxcmd(0xC8);
		i8042auxcmd(0xF3);	/* set sample */
		i8042auxcmd(0x64);
		i8042auxcmd(0xF3);	/* set sample */
		i8042auxcmd(0x50);
		break;
	case Mouseserial:
		uartsetmouseputc(mouseport, m5mouseputc);
		break;
	}
}

static void
resetmouse(void)
{
	packetsize = 3;
	switch(mousetype){
	case MousePS2:
		i8042auxcmd(0xF6);
		i8042auxcmd(0xEA);	/* streaming */
		i8042auxcmd(0xE8);	/* set resolution */
		i8042auxcmd(3);
		break;
	}
}

static void
setstream(int on)
{
	int i;

	switch(mousetype){
	case MousePS2:
		/*
		 * disabling streaming can fail when
		 * a packet is currently transmitted.
		 */
		for(i=0; i<4; i++){
			if(i8042auxcmd(on ? 0xF4 : 0xF5) != -1)
				break;
			tsleep(&up->sleep, return0, 0, 50);
		}
		break;
	}
}

static void
disstream(void)
{
	for(int i=0; i<4; i++){
		if(i8042auxcmd(0xF5) != -1)
			break;
		tsleep(&up->sleep, return0, 0, 50);
	}
}

static void
setabs(void)
{
	disstream();

	i8042auxcmd(0xE8);
	i8042auxcmd(2);
	i8042auxcmd(0xE8);
	i8042auxcmd(0);
	i8042auxcmd(0xE8);
	i8042auxcmd(0);
	i8042auxcmd(0xE8);
	i8042auxcmd(1);
	i8042auxcmd(0xF3);
	i8042auxcmd(0x14);
}

void
mousectl(Cmdbuf *cb)
{
	Cmdtab *ct;

	qlock(&mousectlqlock);
	if(waserror()){
		qunlock(&mousectlqlock);
		nexterror();
	}

	ct = lookupcmd(cb, mousectlmsg, nelem(mousectlmsg));
	switch(ct->index){
	case CMaccelerated:
		setstream(0);
		setaccelerated(cb->nf == 1 ? 1 : atoi(cb->f[1]));
		setstream(1);
		break;
	case CMintellimouse:
		setstream(0);
		setintellimouse();
		setstream(1);
		break;
	case CMlinear:
		setstream(0);
		setlinear();
		setstream(1);
		break;
	case CMps2:
		intellimouse = 0;
		ps2mouse();
		setstream(1);
		break;
	case CMsynaptic:
		intellimouse = 0;
		disstream();
		synhwinfo();	/* 9space */
		setabs();
		synaptic = 1;
		packetsize = 6;
		mousehwaccel = 0;
		i8042auxenable(synmouseputc);
		i8042auxcmd(0xF3);
		i8042auxcmd(0xC8);
		i8042auxcmd(0xF3);
		i8042auxcmd(0x64);
		i8042auxcmd(0xF3);
		i8042auxcmd(0x50);
		i8042auxcmd(0xF4);
		break;
	case CMps2intellimouse:
		ps2mouse();
		setintellimouse();
		setstream(1);
		break;
	case CMres:
		setstream(0);
		if(cb->nf >= 2)
			setres(atoi(cb->f[1]));
		else
			setres(1);
		setstream(1);
		break;
	case CMreset:
		resetmouse();
		if(accelerated)
			setaccelerated(accelerated);
		if(resolution)
			setres(resolution);
		if(intellimouse)
			setintellimouse();
		setstream(1);
		break;
	case CMserial:
		if(mousetype == Mouseserial)
			error(Emouseset);

		if(cb->nf > 2){
			if(strcmp(cb->f[2], "M") == 0)
				uartmouse(cb->f[1], m3mouseputc, 0);
			else if(strcmp(cb->f[2], "MI") == 0)
				uartmouse(cb->f[1], m5mouseputc, 0);
			else
				uartmouse(cb->f[1], mouseputc, cb->nf == 1);
		} else
			uartmouse(cb->f[1], mouseputc, cb->nf == 1);

		mousetype = Mouseserial;
		strncpy(mouseport, cb->f[1], sizeof(mouseport)-1);
		mouseport[sizeof(mouseport)-1] = 0;
		packetsize = 3;
		break;
	case CMhwaccel:
		if(strcmp(cb->f[1], "on")==0)
			mousehwaccel = 1;
		else if(strcmp(cb->f[1], "off")==0)
			mousehwaccel = 0;
		else
			cmderror(cb, "bad mouse control message");
		break;
	case CMtouchpad:
		if(strcmp(cb->f[1], "off")==0){
			if(synaptic==1)
				disabletouch = 1;
		}else if(strcmp(cb->f[1], "on")==0){
			if(synaptic==1)
				disabletouch = 0;
		}else
			cmderror(cb, "bad mouse control message");
		break;
	case CMtap:
		if(strcmp(cb->f[1], "on") == 0)
			syn.notap = 0;
		else if(strcmp(cb->f[1], "off") == 0)
			syn.notap = 1;
		else
			cmderror(cb, "bad mouse control message");
		break;
	case CMmidscroll:
		if(strcmp(cb->f[1], "on") == 0)
			midscroll = 1;
		else if(strcmp(cb->f[1], "off") == 0)
			midscroll = 0;
		else
			cmderror(cb, "bad mouse control message");
		break;
	}

	qunlock(&mousectlqlock);
	poperror();
}
