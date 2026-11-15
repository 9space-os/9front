/*
 * USB Human Interaction Device: keyboard and mouse.
 *
 * If there's no usb keyboard, it tries to setup the mouse, if any.
 * It should be started at boot time.
 *
 * Mouse events are converted to the format of mouse(3)
 * on mousein file.
 * Keyboard keycodes are translated to scan codes and sent to kbdfs(8)
 * on kbin file.
 *
 */

#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include "usb.h"
#include "hid.h"

enum
{
	Awakemsg = 0xdeaddead,
	Diemsg = 0xbeefbeef,
	Rawon = 0x0defaced,
	Dwcidle = 8,
};

char user[] = "kb";

typedef struct Hiddev Hiddev;
struct Hiddev
{
	Dev*	dev;		/* usb device*/
	Dev*	ep;		/* endpoint to get events */

	int	minfd;
	int	kinfd;

	int	idle;

	Channel	*repeatc;	/* only for keyboard */

	/* report descriptor */
	int	nrep;

	/*
	 * use odd size as some devices ignore the high byte of
	 * wLength in control transfer reads.
	 */
	uchar	rep[1024-1];

	Hid	hid;
};


static uchar kbdbootrep[] = {
0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07,
0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0,
};

static uchar ptrbootrep[] = {
0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 
0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 
0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 
0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 
0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 
0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 
0x81, 0x06, 0xc0, 0x09, 0x3c, 0x15, 0x00, 0x25, 
0x01, 0x75, 0x01, 0x95, 0x01, 0xb1, 0x22, 0x95, 
0x07, 0xb1, 0x01, 0xc0, 
};

static int debug = 0;
static int kbdelay = 500;
static int kbrepeat = 100;
static int havekbd;


static int
setproto(Hiddev *f, Iface *iface)
{
	int proto;

	usbcmd(f->dev, Rh2d|Rclass|Riface, Setidle, f->idle<<8, iface->id, nil, 0);

	f->nrep = usbcmd(f->dev, Rd2h|Rstd|Riface, Rgetdesc, Dreport<<8, iface->id,
		f->rep, sizeof(f->rep));
	if(f->nrep > 0){
		if(debug){
			int i;

			fprint(2, "report descriptor:");
			for(i = 0; i < f->nrep; i++){
				if(i%8 == 0)
					fprint(2, "\n\t");
				fprint(2, "%#2.2ux ", f->rep[i]);
			}
			fprint(2, "\n");
		}
		proto = Reportproto;
	} else {
		switch(iface->csp){
		case KbdCSP:
			f->nrep = sizeof(kbdbootrep);
			memmove(f->rep, kbdbootrep, f->nrep);
			break;
		case PtrCSP:
			f->nrep = sizeof(ptrbootrep);
			memmove(f->rep, ptrbootrep, f->nrep);
			break;
		default:
			werrstr("no report descriptor");
			return -1;
		}
		proto = Bootproto;
	}

	/*
	 * if a HID's subclass code is 1 (boot mode), it will support
	 * setproto, otherwise it is not guaranteed to.
	 */
	if(Subclass(iface->csp) != 1)
		return 0;
 
	return usbcmd(f->dev, Rh2d|Rclass|Riface, Setproto, proto, iface->id, nil, 0);
}

static void
hdfree(Hiddev *f)
{
	if(f->kinfd >= 0)
		close(f->kinfd);
	if(f->minfd >= 0)
		close(f->minfd);
	if(f->ep != nil)
		closedev(f->ep);
	if(f->dev != nil)
		closedev(f->dev);
	free(f);
}

static void
hdfatal(Hiddev *f, char *sts)
{
	if(sts != nil)
		fprint(2, "%s: fatal: %s\n", argv0, sts);
	else
		fprint(2, "%s: exiting\n", argv0);
	if(f->repeatc != nil)
		sendul(f->repeatc, Diemsg);
	hdfree(f);
	threadexits(sts);
}

static void
putscan(Hiddev *f, uchar sc, uchar up)
{
	uchar s[2];
	int n;

	if((n = hidscan(s, sc, up)) > 0)
		write(f->kinfd, s, n);
}

static void
sleepproc(void* a)
{
	Channel *c = a;
	int ms;

	threadsetname("sleepproc");
	while((ms = recvul(c)) > 0)
		sleep(ms);
	chanfree(c);
}

static void
repeatproc(void* arg)
{
	Hiddev *f = arg;
	Channel *repeatc, *sleepc;
	ulong l, t;
	uchar sc;
	Alt a[3];

	repeatc = f->repeatc;
	threadsetname("repeatproc");
	
	sleepc = chancreate(sizeof(ulong), 0);
	if(sleepc != nil)
		proccreate(sleepproc, sleepc, Stack);

	a[0].c = repeatc;
	a[0].v = &l;
	a[0].op = CHANRCV;
	a[1].c = sleepc;
	a[1].v = &t;
	a[1].op = sleepc!=nil ? CHANSND : CHANNOP;
	a[2].c = nil;
	a[2].v = nil;
	a[2].op = CHANEND;

	l = Awakemsg;
	while(l != Diemsg){
		if(l == Awakemsg){
			l = recvul(repeatc);
			continue;
		}
		sc = l & 0xff;
		t = kbdelay;
		if(alt(a) == 1){
			t = kbrepeat;
			while(alt(a) == 1)
				putscan(f, sc, 0);
		}
	}
	if(sleepc != nil)
		sendul(sleepc, 0);
	chanfree(repeatc);
	threadexits(nil);
}

static void
stoprepeat(Hiddev *f)
{
	sendul(f->repeatc, Awakemsg);
}

static void
startrepeat(Hiddev *f, uchar sc)
{
	sendul(f->repeatc, sc);
}

static void
sethipri(void)
{
	char fn[64];
	int fd;

	snprint(fn, sizeof(fn), "/proc/%d/ctl", getpid());
	fd = open(fn, OWRITE);
	if(fd < 0)
		return;
	fprint(fd, "pri 13");
	close(fd);
}

static void
kbkeys(Hid *h, uchar *uks, int nuks, uchar *dks, int ndks)
{
	Hiddev *f = h->aux;
	int i, stopped;

	if(f->kinfd < 0){
		f->kinfd = open("/dev/kbin", OWRITE);
		if(f->kinfd < 0)
			hdfatal(f, "open /dev/kbin");

		f->repeatc = chancreate(sizeof(ulong), 0);
		if(f->repeatc == nil)
			hdfatal(f, "chancreate failed");
		proccreate(repeatproc, f, Stack);
	}

	/*
	 * stop the repeats first to avoid race condition when
	 * the key is released but the repeat happens right after
	 */
	for(stopped = i = 0; i < nuks; i++){
		if(ndks == 0 || memchr(dks, uks[i], ndks) != nil){
			stoprepeat(f);
			stopped = 1;
			break;
		}
	}
	for(i = 0; i < nuks; i++)
		putscan(f, uks[i], Keyup);
	for(i = 0; i < ndks; i++)
		putscan(f, dks[i], 0);

	if(stopped == 0 && ndks > 0)
		startrepeat(f, dks[ndks-1]);
}

static void
kbmouse(Hid *h, int abs, int x, int y, int b)
{
	Hiddev *f = h->aux;
	char mbuf[80];

	if(f->minfd < 0){
		f->minfd = open("/dev/mousein", OWRITE);
		if(f->minfd < 0)
			hdfatal(f, "open /dev/mousein");
	}
	seprint(mbuf, mbuf+sizeof(mbuf), "%c%11d %11d %11d", "ma"[abs], x, y, b);
	write(f->minfd, mbuf, strlen(mbuf));
}

static void
readerproc(void* a)
{
	char	err[ERRMAX];
	uchar	buf[sizeof ((Hidreport*)0)->p];
	int	c;
	Hiddev *f = a;

	threadsetname("readerproc %s", f->ep->dir);
	sethipri();

	f->hid.rep = f->rep;
	f->hid.nrep = f->nrep;
	f->hid.aux = f;
	f->hid.keys = kbkeys;
	f->hid.mouse = kbmouse;
	for(;;){
		if(f->ep == nil)
			hdfatal(f, nil);
		if(f->ep->maxpkt < 1 || f->ep->maxpkt > sizeof buf)
			hdfatal(f, "hid: weird maxpkt");

		c = read(f->ep->dfd, buf, f->ep->maxpkt);
		if(c <= 0){
			if(c < 0)
				rerrstr(err, sizeof(err));
			else
				strcpy(err, "zero read");

			fprint(2, "%s: hid: %s: read: %s\n", argv0, f->ep->dir, err);

			/* reset the whole device */
			devctl(f->dev, "reset");

			hdfatal(f, err);
		}
		hidinput(&f->hid, buf, c);
	}
}


static void
gaomons620(Hiddev *f)
{
	static uchar descr[] = {
		0x05, 0x0D,	/* Usage Page (Digitizer),	*/
		0x09, 0x01,	/* Usage (Digitizer),		*/
		0xA1, 0x01,	/* Collection (Application),	*/
		0x85, 0x08,	/* Report ID (8),		*/
		0x09, 0x20,	/* Usage (Stylus),		*/
		0xA0,		/* Collection (Physical),	*/
		0x14,		/*	Logical Minimum (0),	*/
		0x25, 0x01,	/*	Logical Maximum (1),	*/
		0x75, 0x01,	/*	Report Size (1),	*/
		0x09, 0x42,	/*	Usage (Tip Switch),	*/
		0x09, 0x44,	/*	Usage (Barrel Switch),	*/
		0x09, 0x45,	/*	Usage (Eraser),		*/
		0x95, 0x03,	/*	Report Count (3),	*/
		0x81, 0x02,	/*	Input (Variable),	*/

		0x95, 0x04,	/*	Report Count (4),	*/
		0x81, 0x03,	/*	Input (Constant, Variable), */

		0x09, 0x32,	/*	Usage (In Range),	*/
		0x95, 0x01,	/*	Report Count (1),	*/
		0x81, 0x02,	/*	Input (Variable),	*/

		0xA4,		/*	Push,			*/
		0x05, 0x01,	/*	Usage Page (Desktop),	*/
		0x65, 0x13,	/*	Unit (Inch),		*/
		0x55, 0xFD,	/*	Unit Exponent (-3),	*/
		0x75, 0x10,	/*	Report Size (16),	*/
		0x34,		/*	Physical Minimum (0),	*/
		0x09, 0x30,	/*	Usage (X),		*/
		0x27, 0x00, 0x80, 0x00, 0x00,	/*	Logical Maximum, */
		0x47, 0x00, 0x80, 0x00, 0x00,	/*	Physical Maximum, */
		0x81, 0x02,	/*	Input (Variable),	*/

		0x09, 0x31,	/*	Usage (Y),		*/
		0x27, 0x00, 0x50, 0x00, 0x00,	/*	Logical Maximum, */
		0x47, 0x00, 0x50, 0x00, 0x00,	/*	Physical Maximum, */
		0x81, 0x02,	/*	Input (Variable),	*/

		0xB4,		/*	Pop,			*/
		0x09, 0x30,	/*	Usage (Tip Pressure),	*/
		0x75, 0x10,	/*	Report Size (16),	*/
		0x27, 0x00, 0x00, 0x01, 0x00,	/*	Logical Maximum, */
		0x81, 0x02,	/*	Input (Variable),	*/
		0xC0,		/* End Collection,		*/
		0xC0		/* End Collection		*/
	};
	static int idx[] = { 0x64, 0x65, 0x6e, 0x79, 0x7a, 0x7b, 0xc8, 0xc9 };
	Dev *d = f->dev;	
	int i;

	/* we have to fetch a bunch of strings to switch to non-phone mode */
	for(i=0; i < nelem(idx); i++)
		free(loaddevstr(d, idx[i]));

	/* override report descriptor */
	memmove(f->rep, descr, f->nrep = sizeof(descr));
}

static void
quirks(Hiddev *f)
{
	Dev *d;
	
	d = f->dev;

	if(d->usb->vid == 0x256c && d->usb->did == 0x006d){
		gaomons620(f);
		return;
	}
	
	/* Elecom trackball report descriptor lies by
	 * omission, failing to mention all its buttons.
	 * We patch the descriptor with a correct count
	 * which lets us parse full reports. Tested with:
	 *   Elecom HUGE (M-HT1DRBK, M-HT1URBK) */
	if(d->usb->vid == 0x056e && d->usb->did == 0x010c){
		if(f->nrep < 32
		|| f->rep[12] != 0x95
		|| f->rep[14] != 0x75
		|| f->rep[15] != 0x01
		|| f->rep[20] != 0x29
		|| f->rep[30] != 0x75)
		return;

		f->rep[13] = 8;
		f->rep[21] = 8;
		f->rep[31] = 0;
		return;
	}
}

static int
hdsetup(Dev *d, Ep *ep)
{
	Hiddev *f;
	uchar desc[512];
	int n;

	f = emallocz(sizeof(Hiddev), 1);
	f->minfd = -1;
	f->kinfd = -1;
	incref(d);
	f->dev = d;
	/*
	 * DWC OTG controller misses some split transaction inputs.
	 * Set nonzero idle time to return more frequent reports
	 * of keyboard state, to avoid losing key up/down events.
	 */
	n = read(d->cfd, desc, sizeof(desc) - 1);
	if(n > 0){
		desc[n] = 0;
		if(strstr((char*)desc, "dwcotg") != nil)
			f->idle = Dwcidle;
	}
	if(setproto(f, ep->iface) < 0){
		fprint(2, "%s: %s: setproto: %r\n", argv0, d->dir);
		goto Err;
	}
	f->ep = openep(f->dev, ep);
	if(f->ep == nil){
		fprint(2, "%s: %s: openep %d: %r\n", argv0, d->dir, ep->id);
		goto Err;
	}
	if(opendevdata(f->ep, OREAD) < 0){
		fprint(2, "%s: %s: opendevdata: %r\n", argv0, f->ep->dir);
		goto Err;
	}
	quirks(f);
	procrfork(readerproc, f, Stack, RFNOTEG);
	return 0;
Err:
	hdfree(f);
	return -1;
}

static void
fsread(Req *r)
{
	char msg[48], *s, *e;

	s = msg;
	e = msg+sizeof(msg);
	*s = 0;
	if(havekbd)
		e = seprint(s, e, "repeat %d\ndelay %d\n", kbrepeat, kbdelay);
	USED(e);
	readstr(r, msg);
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	char msg[256], *f[4];
	void *data;
	int nf, sz;
	Dev *dev;

	dev = r->fid->file->aux;
	data = r->ifcall.data;
	sz = r->ifcall.count;
	if(r->fid->aux == (void*)Rawon){
		if(sz == 6 && memcmp(data, "rawoff", 6) == 0)
			r->fid->aux = nil;
		else if(usbcmd(dev, Rh2d|Rclass|Riface, Setreport, Reportout, 0, data, sz) < 0){
			responderror(r);
			return;
		}
	}else{
		snprint(msg, sizeof(msg), "%.*s", utfnlen(data, sz), data);
		nf = tokenize(msg, f, nelem(f));
		if(nf == 1 && strcmp(f[0], "rawon") == 0)
			r->fid->aux = (void*)Rawon;
		else if(nf == 2 && strcmp(f[0], "repeat") == 0)
			kbrepeat = atoi(f[1]);
		else if(nf == 2 && strcmp(f[0], "delay") == 0)
			kbdelay = atoi(f[1]);
		else if(nf == 2 && strcmp(f[0], "debug") == 0)
			hiddebug = debug = atoi(f[1]);
		else{
			respond(r, "invalid ctl message");
			return;
		}
	}
	r->ofcall.count = sz;
	respond(r, nil);
}

static Srv fs = {
	.read = fsread,
	.write = fswrite,
};

static void
usage(void)
{
	fprint(2, "usage: %s [-d] devid\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char* argv[])
{
	int i, n;
	Dev *d;
	Ep *ep;
	Usbdev *ud;
	char buf[32];

	ARGBEGIN{
	case 'd':
		hiddebug = ++debug;
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 1)
		usage();
	d = getdev(*argv);
	if(d == nil)
		sysfatal("getdev: %r");
	ud = d->usb;
	for(i = n = 0; i < nelem(ud->ep); i++){
		if((ep = ud->ep[i]) == nil)
			continue;
		if(ep->type != Eintr || ep->dir != Ein)
			continue;
		switch(ep->iface->csp){
		case KbdCSP:
			havekbd = 1;
		case PtrCSP:
		case PtrNonBootCSP:
		case HidCSP:
			n += hdsetup(d, ep) == 0;
			break;
		}
	}
	closedev(d);
	if(n > 0){
		fs.tree = alloctree(user, "usb", DMDIR|0555, nil);
		snprint(buf, sizeof buf, "hidU%sctl", d->hname);
		createfile(fs.tree->root, buf, user, 0666, d);
		snprint(buf, sizeof buf, "%s.hid", d->hname);
		threadpostsharesrv(&fs, nil, "usb", buf);
	}
	threadexits(nil);
}
