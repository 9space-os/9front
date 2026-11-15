/*
 * nfront: #γ, a GPIO controller's pins and their interrupts, for the
 * programs that drive what is wired to them: aux/acpi (GPIO-signaled
 * ACPI events, _AEI: a laptop's power button) and the drivers of devices
 * whose interrupt is a pin (an I2C touchpad's GpioInt).
 *
 *	#γ/gpio/ctl	read: "pin N int|out mode polarity level register" per pin in use
 *			write: N int level|edge high|low|both	(its interrupt, masked
 *						until a reader of event acks it)
 *			       N out 0|1	(drive it)
 *			       N off		(no interrupt, not driven)
 *	#γ/gpio/event	write: watch N...	(the pins this open file hears of,
 *						their interrupts unmasked)
 *			       ack N		(N's interrupt again)
 *			read: "N\n" per pin whose interrupt came; it is masked
 *			until acked (a level stays until its cause is handled)
 *
 * Each open of event hears of its own pins.  The controller: AMD's
 * (AMDI0030, AMD0030, the FCH's), found in ACPI at boot (archacpi.c:
 * enumgpio calls amdgpioinit), 184 pins, the registers and the interrupt
 * as OpenBSD's amdgpio drives them (sys/dev/acpi/amdgpio.c,v 1.12):
 *
 * Copyright (c) 2016 Mark Kettenis
 * Copyright (c) 2019 James Hastings
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

enum {
	/* a pin's register, at pin*4 */
	ConfLevel	= 1<<8,
	ConfActlo	= 1<<9,
	ConfActboth	= 1<<10,
	ConfPolmask	= 3<<9,
	ConfIntEn	= 1<<11,
	ConfIntMask	= 1<<12,
	ConfRxstate	= 1<<16,
	ConfTxstate	= 1<<22,
	ConfTxstateEn	= 1<<23,
	ConfIntSts	= 1<<28,

	IrqMaster	= 0xfc,		/* pin 63's place */
	IrqMasterEoi	= 1<<29,
	IrqSts		= 0x2f8,	/* a bit for four pins, 46 of them */
	IrqBits		= 46,

	Npins		= 184,
	Nbytes		= (Npins+7)/8,

	Qdir		= 0,
	Qgpio,
	Qctl,
	Qevent,

	/* a pin's use */
	Unused		= 0,
	Uint,
	Uout,
};

typedef struct Watch Watch;
struct Watch {
	Watch	*next;
	uchar	want[Nbytes];
	uchar	pend[Nbytes];
	int	npend;
	Rendez	r;
};

static struct {
	Lock;
	QLock	wl;		/* the watchers' list, changed */
	u32int	*regs;
	int	irq;
	int	on;		/* interrupt enabled */
	uchar	use[Npins];
	Watch	*watch;
	ulong	nintr;
} gpio;

static Dirtab gpiodir[] = {
	"ctl",		{Qctl},		0,	0660,
	"event",	{Qevent},	0,	0660,
};

static u32int
rd(int pin)
{
	return gpio.regs[pin];
}

static void
wr(int pin, u32int v)
{
	gpio.regs[pin] = v;
}

static int
validpin(int pin)
{
	return pin >= 0 && pin < Npins && pin != IrqMaster/4;
}

/* the pin's interrupt masked or not; gpio locked */
static void
pinmask(int pin, int on)
{
	u32int v;

	v = rd(pin);
	if(on)
		v |= ConfIntEn|ConfIntMask;
	else
		v &= ~(ConfIntEn|ConfIntMask);
	wr(pin, v);
}

static void
gpiointr(Ureg*, void*)
{
	uvlong sts;
	u32int v;
	int i, j, pin;
	Watch *w;

	ilock(&gpio);
	gpio.nintr++;
	sts = (uvlong)gpio.regs[(IrqSts+4)/4] << 32 | gpio.regs[IrqSts/4];
	for(i = 0; i < IrqBits; i++){
		if((sts & 1ULL<<i) == 0)
			continue;
		for(j = 0; j < 4; j++){
			pin = i*4 + j;
			if(!validpin(pin))
				continue;
			v = rd(pin);
			if((v & ConfIntSts) == 0)
				continue;
			/*
			 * masked until a reader acks it (its cause handled, or
			 * nobody listens: masked as amdgpio does), the status cleared
			 */
			for(w = gpio.watch; w != nil; w = w->next)
				if(w->want[pin/8] & 1<<pin%8 && (w->pend[pin/8] & 1<<pin%8) == 0){
					w->pend[pin/8] |= 1<<pin%8;
					w->npend++;
				}
			wr(pin, v & ~(ConfIntEn|ConfIntMask));
		}
	}
	v = gpio.regs[IrqMaster/4];
	gpio.regs[IrqMaster/4] = v | IrqMasterEoi;
	for(w = gpio.watch; w != nil; w = w->next)
		if(w->npend > 0)
			wakeup(&w->r);
	iunlock(&gpio);
}

/* called by archacpi.c for AMDI0030 at boot: its registers and interrupt */
void
amdgpioinit(uvlong pa, ulong len, int irq)
{
	if(gpio.regs != nil || len < Npins*4)
		return;
	if((gpio.regs = vmap(pa, len)) == nil){
		print("gpio: can't map %#llux\n", pa);
		return;
	}
	gpio.irq = irq;
	print("gpio: amd %#llux irq %d, %d pins\n", pa, irq, Npins);
}

static void
gpioon(void)
{
	qlock(&gpio.wl);
	if(!gpio.on){
		intrenable(gpio.irq, gpiointr, nil, BUSUNKNOWN, "gpio");
		gpio.on = 1;
	}
	qunlock(&gpio.wl);
}

static int
gpiogen(Chan *c, char*, Dirtab*, int, int s, Dir *dp)
{
	Qid q;

	if(s == DEVDOTDOT){
		mkqid(&q, Qdir, 0, QTDIR);
		devdir(c, q, "#γ", 0, eve, 0555, dp);
		return 1;
	}
	switch((ulong)c->qid.path){
	case Qdir:
		if(s != 0)
			return -1;
		mkqid(&q, Qgpio, 0, QTDIR);
		devdir(c, q, "gpio", 0, eve, 0555, dp);
		return 1;
	case Qgpio:
	case Qctl:
	case Qevent:
		if(s >= nelem(gpiodir))
			return -1;
		devdir(c, gpiodir[s].qid, gpiodir[s].name, 0, eve, gpiodir[s].perm, dp);
		return 1;
	}
	return -1;
}

static Chan*
gpioattach(char *spec)
{
	if(gpio.regs == nil)
		error("no gpio controller");
	return devattach(L'γ', spec);
}

static Walkqid*
gpiowalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, gpiogen);
}

static int
gpiostat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, nil, 0, gpiogen);
}

static Chan*
gpioopen(Chan *c, int omode)
{
	Watch *w;

	c = devopen(c, omode, nil, 0, gpiogen);
	if((ulong)c->qid.path == Qevent){
		w = smalloc(sizeof *w);
		qlock(&gpio.wl);
		ilock(&gpio);
		w->next = gpio.watch;
		gpio.watch = w;
		iunlock(&gpio);
		qunlock(&gpio.wl);
		c->aux = w;
		gpioon();
	}
	return c;
}

static void
gpioclose(Chan *c)
{
	Watch *w, *o, **l;
	int pin, others;

	if((c->flag & COPEN) == 0 || (ulong)c->qid.path != Qevent || (w = c->aux) == nil)
		return;
	qlock(&gpio.wl);
	ilock(&gpio);
	for(l = &gpio.watch; *l != nil; l = &(*l)->next)
		if(*l == w){
			*l = w->next;
			break;
		}
	/* its pins nobody else hears of: masked */
	for(pin = 0; pin < Npins; pin++){
		if((w->want[pin/8] & 1<<pin%8) == 0)
			continue;
		others = 0;
		for(o = gpio.watch; o != nil; o = o->next)
			if(o->want[pin/8] & 1<<pin%8)
				others = 1;
		if(!others && gpio.use[pin] == Uint)
			pinmask(pin, 0);
	}
	iunlock(&gpio);
	qunlock(&gpio.wl);
	free(w);
	c->aux = nil;
}

static int
haspend(void *a)
{
	return ((Watch*)a)->npend > 0;
}

static long
gpioread(Chan *c, void *a, long n, vlong off)
{
	char *buf, *s, *e;
	Watch *w;
	u32int v;
	int pin;

	switch((ulong)c->qid.path){
	case Qdir:
	case Qgpio:
		return devdirread(c, a, n, nil, 0, gpiogen);
	case Qctl:
		buf = smalloc(READSTR);
		if(waserror()){
			free(buf);
			nexterror();
		}
		s = buf;
		e = buf + READSTR;
		for(pin = 0; pin < Npins && s < e; pin++){
			if(gpio.use[pin] == Unused)
				continue;
			v = rd(pin);
			s = seprint(s, e, "pin %d %s %s %s %d %#.8ux\n", pin,
				gpio.use[pin] == Uint ? "int" : "out",
				v & ConfLevel ? "level" : "edge",
				(v & ConfPolmask) == ConfActboth ? "both" : v & ConfActlo ? "low" : "high",
				(v & ConfRxstate) != 0, v);
		}
		seprint(s, e, "interrupts %lud\n", gpio.nintr);
		n = readstr(off, a, n, buf);
		poperror();
		free(buf);
		return n;
	case Qevent:
		w = c->aux;
		sleep(&w->r, haspend, w);
		s = buf = smalloc(READSTR);
		e = buf + READSTR;
		ilock(&gpio);
		for(pin = 0; pin < Npins && e - s > 8; pin++)
			if(w->pend[pin/8] & 1<<pin%8){
				s = seprint(s, e, "%d\n", pin);
				w->pend[pin/8] &= ~(1<<pin%8);
				w->npend--;
			}
		iunlock(&gpio);
		if(n > s - buf)
			n = s - buf;
		memmove(a, buf, n);
		free(buf);
		return n;
	}
	error(Egreg);
	return 0;
}

static long
gpiowrite(Chan *c, void *a, long n, vlong)
{
	Cmdbuf *cb;
	Watch *w;
	u32int v;
	int i, pin;

	cb = parsecmd(a, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	switch((ulong)c->qid.path){
	case Qctl:
		if(cb->nf < 2 || !validpin(pin = strtol(cb->f[0], nil, 0)))
			error(Ebadarg);
		ilock(&gpio);
		v = rd(pin);
		if(cb->nf == 4 && strcmp(cb->f[1], "int") == 0){
			/* as amdgpio_intr_establish; masked until acked */
			v &= ~(ConfPolmask|ConfLevel|ConfTxstateEn|ConfIntEn|ConfIntMask);
			if(strcmp(cb->f[2], "level") == 0)
				v |= ConfLevel;
			else if(strcmp(cb->f[2], "edge") != 0){
				iunlock(&gpio);
				error(Ebadarg);
			}
			if(strcmp(cb->f[3], "low") == 0)
				v |= ConfActlo;
			else if(strcmp(cb->f[3], "both") == 0)
				v |= ConfActboth;
			else if(strcmp(cb->f[3], "high") != 0){
				iunlock(&gpio);
				error(Ebadarg);
			}
			wr(pin, v | ConfIntSts);	/* a status of before cleared */
			gpio.use[pin] = Uint;
		} else if(cb->nf == 3 && strcmp(cb->f[1], "out") == 0){
			v |= ConfTxstateEn;
			if(atoi(cb->f[2]))
				v |= ConfTxstate;
			else
				v &= ~ConfTxstate;
			wr(pin, v);
			gpio.use[pin] = Uout;
		} else if(cb->nf == 2 && strcmp(cb->f[1], "off") == 0){
			wr(pin, v & ~(ConfIntEn|ConfIntMask|ConfTxstateEn));
			gpio.use[pin] = Unused;
		} else {
			iunlock(&gpio);
			error(Ebadarg);
		}
		iunlock(&gpio);
		break;
	case Qevent:
		w = c->aux;
		if(cb->nf < 2)
			error(Ebadarg);
		if(strcmp(cb->f[0], "watch") == 0){
			for(i = 1; i < cb->nf; i++){
				if(!validpin(pin = strtol(cb->f[i], nil, 0)))
					error(Ebadarg);
				ilock(&gpio);
				w->want[pin/8] |= 1<<pin%8;
				if(gpio.use[pin] == Uint)
					pinmask(pin, 1);	/* heard of now: unmasked */
				iunlock(&gpio);
			}
		} else if(strcmp(cb->f[0], "ack") == 0){
			for(i = 1; i < cb->nf; i++){
				if(!validpin(pin = strtol(cb->f[i], nil, 0)) || gpio.use[pin] != Uint)
					error(Ebadarg);
				ilock(&gpio);
				pinmask(pin, 1);
				iunlock(&gpio);
			}
		} else
			error(Ebadarg);
		break;
	default:
		error(Egreg);
	}
	poperror();
	free(cb);
	return n;
}

Dev gpiodevtab = {
	L'γ',
	"gpio",

	devreset,
	devinit,
	devshutdown,
	gpioattach,
	gpiowalk,
	gpiostat,
	gpioopen,
	devcreate,
	gpioclose,
	gpioread,
	devbread,
	gpiowrite,
	devbwrite,
	devremove,
	devwstat,
};
