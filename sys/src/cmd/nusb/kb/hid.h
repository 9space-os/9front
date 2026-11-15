/*
 * USB keyboard/mouse constants
 */
enum {

	Stack = 32 * 1024,
	Nkey = 64,

	/* HID class subclass protocol ids */
	PtrCSP		= 0x020103,	/* mouse.boot.hid */
	HidCSP		= 0x000003,	/* could be a trackpoint */
	PtrNonBootCSP	= 0x020003,
	KbdCSP		= 0x010103,	/* keyboard.boot.hid */

	/* Requests */
	Getreport = 0x01,
	Setreport = 0x09,
	Getproto	= 0x03,
	Setidle		= 0x0a,
	Setproto	= 0x0b,

	/* protocols for SET_PROTO request */
	Bootproto	= 0,
	Reportproto	= 1,

	/* protocols for SET_REPORT request */
	Reportout = 0x0200,
};

/*
 * USB HID report descriptor item tags
 */ 
enum {
	/* main items */
	Input	= 8,
	Output,
	Collection,
	Feature,

	CollectionEnd,

	/* global items */
	UsagPg = 0,
	LogiMin,
	LogiMax,
	PhysMin,
	PhysMax,
	UnitExp,
	UnitTyp,
	RepSize,
	RepId,
	RepCnt,

	Push,
	Pop,

	/* local items */
	Usage	= 0,
	UsagMin,
	UsagMax,
	DesgIdx,
	DesgMin,
	DesgMax,
	StrgIdx,
	StrgMin,
	StrgMax,

	Delim,
};

/* main item flags */
enum {
	Fdata	= 0<<0,	Fconst	= 1<<0,
	Farray	= 0<<1,	Fvar	= 1<<1,
	Fabs	= 0<<2,	Frel	= 1<<2,
	Fnowrap	= 0<<3,	Fwrap	= 1<<3,
	Flinear	= 0<<4,	Fnonlin	= 1<<4,
	Fpref	= 0<<5,	Fnopref	= 1<<5,
	Fnonull	= 0<<6,	Fnullst	= 1<<6,
};

/*
 * Plan 9 keyboard driver constants.
 */
enum {
	/* Scan codes (see kbd.c) */
	SCesc1		= 0xe0,		/* first of a 2-character sequence */
	SCesc2		= 0xe1,
	Consumer	= 0x100,	/* the key is on consumer page */
	Keyup		= 0x80,		/* flag bit */
	Keymask		= 0x7f,		/* regular scan code bits */
};

#define isext(sc)	((sc) >= 0x80)

typedef struct Hidreport Hidreport;
typedef struct Hidslot Hidslot;
struct Hidslot
{
	int	valid;
	int	usage;
	int	id;
	int	oor;

	int	abs;	/* for xyz */

	int	x;
	int	y;
	int	z;

	int	b;
	int	m;

	int	w;
	int	h;
};

struct Hidreport
{
	int	ns;
	Hidslot	s[16];

	int	nk;
	ushort	k[Nkey];

	int	o;
	uchar	*e;
	uchar	p[512];
};

/*
 * nfront: a HID device's reports to keys and mouse(3) events, as nusb/kb
 * had them; shared with aux/i2chid (I²C-HID).  hidinput takes one
 * input report and calls keys with the scan codes released and pressed
 * (kbdfs(8)'s, see hidscan), mouse with an event (abs: x, y absolute,
 * 0 to 1<<31); the state between reports is in the Hid.  hidtouchpad
 * puts a precision touchpad into its mode (setfeature: the transport's
 * SET_REPORT of a feature report, its id first): then hidinput makes
 * mouse(3)'s events of its fingers; hidtimer, called every few
 * milliseconds, lets go of a tap's button when no drag followed it.
 */
typedef struct Hid Hid;
typedef struct Hidfinger Hidfinger;
typedef struct Hidtouch Hidtouch;

enum {
	Nfinger	= 5,
};

struct Hidfinger
{
	int	id;
	int	tip;
	int	conf;
	int	x;
	int	y;
};

/*
 * a precision touchpad (Microsoft's, Input Mode 3: the fingers'
 * positions, not a mouse's motion) and what its fingers do
 */
struct Hidtouch
{
	int	on;
	int	natural;	/* scroll as the fingers move the page */
	int	xmin, xmax, ymin, ymax;	/* the fingers' logical range */
	int	w, h;		/* the pad's size, in 10µm */

	/* a frame in several reports (hybrid mode) */
	Hidfinger	f[Nfinger];
	int	nf;
	int	want;
	int	pbutton;

	/* the last frame, the gestures */
	Hidfinger	last[Nfinger];
	int	nlast;
	int	button;		/* the pad pressed: mouse(3)'s button */
	int	fx, fy;		/* motion not given yet, 1/1000 pixel */
	int	scroll;		/* two fingers' motion, 10µm */
	vlong	tlast;		/* the last frame's time, ms */
	vlong	t0;		/* a touch: since, the most fingers, moved */
	int	maxn;
	int	moved;
	int	clicked;
	int	tapb;		/* a tap's button, held a while: a drag may follow */
	vlong	tapat;
	int	drag;		/* the button a drag after a tap holds */
	int	b;		/* the buttons given */
};

struct Hid
{
	uchar	*rep;		/* report descriptor */
	int	nrep;
	void	*aux;
	void	(*keys)(Hid*, uchar *up, int nup, uchar *down, int ndown);
	void	(*mouse)(Hid*, int abs, int x, int y, int b);

	Hidreport	r;
	ushort	lastk[Nkey];
	int	nlastk;
	Hidslot	lasts[16];
	int	lastb;

	Hidtouch	tp;
};

extern int hiddebug;

void	hidinput(Hid*, uchar *p, int n);
int	hidtouchpad(Hid*, int (*setfeature)(Hid*, uchar *rep, int n));
void	hidtimer(Hid*);
int	hidscan(uchar *buf, uchar sc, int up);
