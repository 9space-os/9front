typedef struct Wba Wba;
typedef struct Wkey Wkey;
typedef struct Wnode Wnode;
typedef struct Wifi Wifi;
typedef struct Wifipkt Wifipkt;

enum {
	Essidlen = 32,
};

/* cipher */
enum {
	TKIP	= 1,
	CCMP	= 2,
};

struct Wkey
{
	int		cipher;
	int		len;
	uvlong		tsc;
	uchar		key[];
};

/*
 * 9space: a block ack agreement the access point made (A-MPDU, received):
 * its window of sequence numbers from head, the frames held until those
 * before them are there (or the wait is over: since)
 */
enum {
	Nba	= 64,	/* the largest window: our answer's buffer size */
	Ntid	= 16,
};

struct Wba
{
	int	on;
	int	head;		/* the first sequence number of the window */
	int	win;		/* its size */
	int	n;		/* frames held */
	ulong	since;		/* ticks: since when the head is missing */
	ulong	held;		/* frames that came before ones missing */
	ulong	expired;	/* waits over: frames given up */
	Block	*buf[Nba];
};

struct Wnode
{
	uchar	bssid[Eaddrlen];
	char	ssid[Essidlen+2];

	char	*status;

	int	rsnelen;
	uchar	rsne[258];
	Wkey	*txkey[1];
	Wkey	*rxkey[5];

	int	aid;		/* association id */
	ulong	lastsend;
	ulong	lastseen;

	uchar	*minrate;	/* pointers into wifi->rates */
	uchar	*maxrate;
	uchar	*actrate;

	ulong	validrates;	/* bitmap on wifi->rates */
	ulong	basicrates;

	ulong	txcount;	/* statistics for rate adaption */
	ulong	txerror;

	/* stuff from beacon */
	uvlong	rs;
	uvlong	ts;
	uchar	dtimcount;
	uchar	dtimperiod;
	int	ival;
	int	cap;
	int	channel;
	int	brsnelen;
	uchar	brsne[258];

	/*
	 * 9space: what the access point's beacon offers (WMM, HT, VHT,
	 * HE) and whether we associated with WMM: our data frames are
	 * then QoS data
	 */
	uchar	wmm;
	uchar	ht;
	uchar	vht;
	uchar	he;
	uchar	qos;
	/*
	 * 9space: from its HT capabilities (45) and HT operation (61):
	 * the capability info, the MCS it receives (streams 1 and 2), the
	 * HT protection mode; for drivers that pick the HT rates
	 */
	ushort	htcap;
	uchar	htrxmcs[2];
	uchar	htprot;
	ushort	qseq[Ntid];	/* 9space: the QoS data's sequence numbers, per TID */
	ushort	txba;		/* 9space: the TIDs whose block ack agreement we made */
	uchar	batoken;	/* the last ADDBA request's dialog token */
	int	rssi;		/* 9space: its beacons' signal, dBm, smoothed (hasrssi) */
	uchar	hasrssi;
	ulong	seenstep;	/* 9space: wifi->scanstep when its beacon was last heard */
};

struct Wifi
{
	Ether	*ether;

	int	debug;

	RWLock	crypt;
	Queue	*iq;
	ulong	watchdog;
	ulong	lastauth;
	Ref	txseq;
	void	(*transmit)(Wifi*, Wnode*, Block*);

	/* for searching */
	uchar	bssid[Eaddrlen];
	char	essid[Essidlen+2];

	/* supported data rates by hardware */
	uchar	*rates;

	/*
	 * 9space: the channels to scan, in order (0 ends; nil: wifiscan2g,
	 * 2.4 GHz only) and the channel the radio was last tuned to (for
	 * beacons that do not tell theirs)
	 */
	uchar	*scan;
	int	channel;

	/*
	 * 9space: the radio's capability elements (HT 45, VHT 191, HE
	 * 255/35, ...: id, length, contents, one after another), sent in
	 * the association request where the access point offers the same
	 */
	uchar	caps[256];
	int	ncaps;

	/*
	 * 9space: the driver's radio sends QoS data (WMM, 802.11e): wifi.c
	 * then offers WMM in the association where the access point has
	 * it, and its data frames are QoS data. Without (iwl, wpi,
	 * rt2860: their firmware's stations are not QoS ones) plain data,
	 * as 9front
	 */
	int	wmm;

	/*
	 * 9space: the driver's radio answers aggregates with block acks
	 * (ba): wifi.c then agrees to the access point's ADDBA requests
	 * and holds the frames until they are in order (rxba, per TID),
	 * and asks for one itself (TID 0); else it declines them
	 */
	int	ba;
	int	baproc;
	/*
	 * 9space: the radio sends aggregates too (batx: wifi.c asks for
	 * a block ack agreement of its own, TID 0); baagree, where set, is
	 * called when one of the access point's is agreed (on) or ended,
	 * for the driver to have the radio answer with block acks; it
	 * returns -1 to decline
	 */
	int	batx;
	int	(*baagree)(Wifi*, int tid, int ssn, int win, int on);
	ulong	replays;	/* frames CCMP's (TKIP's) replay check dropped */

	/*
	 * 9space: the driver gives the signal (Bsignal): the scan goes once
	 * over all its channels (scanlen steps) after the first node that
	 * matches was heard, then chooses the strongest of those that match
	 */
	int	sigs;
	ulong	scanstep;	/* channels tuned to, counted */
	int	scanlen;	/* channels in the scan's list */
	ulong	candstep;	/* scanstep when the first that matches was heard */
	int	hascand;	/* candstep is set (0: a new choice) */
	Lock	balock;
	Wba	rxba[Ntid];

	/* effective base station */
	Wnode	*bss;

	Wnode	node[32];
};

struct Wifipkt
{
	uchar	fc[2];
	uchar	dur[2];
	uchar	a1[Eaddrlen];
	uchar	a2[Eaddrlen];
	uchar	a3[Eaddrlen];
	uchar	seq[2];
	uchar	a4[Eaddrlen];
};

enum {
	Btimestamp = 1<<15,
	Bsignal = 1<<14,	/* 9space: the signal, dBm (signed), the byte before the timestamp's 8 */
};

/* 9space: scan lists for wifi->scan */
extern uchar wifiscan2g[];	/* 2.4 GHz, channels 1-13 (the default) */
extern uchar wifiscan5g[];	/* 2.4 GHz and 5 GHz, 36-64, 100-144, 149-165 */

Wifi *wifiattach(Ether *ether, void (*transmit)(Wifi*, Wnode*, Block*));
void wifiiq(Wifi*, Block*);
void wifiradiogone(Wifi*);	/* 9space */
int wifihdrlen(Wifipkt*);
void wifitxfail(Wifi*, Block*);

char *wifistat(Wifi*, char*, char*);
long wifictl(Wifi*, void*, long);
void wificfg(Wifi*, char*);
