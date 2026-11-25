/*
 * 9space: an 802.11 interface whose radio is a program (etherwlan.c, the
 * interface; devwlan.c, #ω, the radio's side)
 */
enum {
	Nwlan	= 4,
	Hdrtx	= 12,	/* channel[2] rate[1] flags[1] aid[2] bssid[6] */
	Hdrrx	= 12,	/* channel[2] rssi[1] flags[1] tsf[8] */
	Ftune	= 1<<0,	/* tx: no frame, only tune to the channel */
	Ftsf	= 1<<0,	/* rx: tsf is the frame's timestamp */
	Maxframe	= 4096,
};

typedef struct Wlan Wlan;
struct Wlan {
	QLock;
	Ether	*edev;
	Wifi	*wifi;
	int	attached;
	Queue	*txq;		/* frames for the radio */
	Ref	radio;		/* the radio's data open */
	ulong	ntx, nrx, ndrop;
	uchar	caps[256];	/* the radio's capability elements (ctl caps), for wifi.c */
	int	ncaps;

	/*
	 * ctl "irq on": the frames received go to wifiiq from a timer
	 * function, at interrupt level, as a real radio's driver calls it
	 * from its interrupt (sys/src/build/host/tests/wlan)
	 */
	int	irq;
	Lock	irqlk;
	Block	*irqh, *irqt;
	Timer	irqtimer;
};

extern Wlan wlans[Nwlan];
extern int nwlan;
