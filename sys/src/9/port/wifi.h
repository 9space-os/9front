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
 * nfront: a block ack agreement the access point made (A-MPDU, received):
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
	 * nfront: what the access point's beacon offers (WMM, HT, VHT,
	 * HE) and whether we associated with WMM: our data frames are
	 * then QoS data
	 */
	uchar	wmm;
	uchar	ht;
	uchar	vht;
	uchar	he;
	uchar	qos;
	ushort	qseq[Ntid];	/* nfront: the QoS data's sequence numbers, per TID */
	ushort	txba;		/* nfront: the TIDs whose block ack agreement we made */
	uchar	batoken;	/* the last ADDBA request's dialog token */
	int	rssi;		/* nfront: its beacons' signal, dBm, smoothed (hasrssi) */
	uchar	hasrssi;
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
	 * nfront: the channels to scan, in order (0 ends; nil: wifiscan2g,
	 * 2.4 GHz only) and the channel the radio was last tuned to (for
	 * beacons that do not tell theirs)
	 */
	uchar	*scan;
	int	channel;

	/*
	 * nfront: the radio's capability elements (HT 45, VHT 191, HE
	 * 255/35, ...: id, length, contents, one after another), sent in
	 * the association request where the access point offers the same
	 */
	uchar	caps[256];
	int	ncaps;

	/*
	 * nfront: the driver's radio answers aggregates with block acks
	 * (ba): wifi.c then agrees to the access point's ADDBA requests
	 * and holds the frames until they are in order (rxba, per TID),
	 * and asks for one itself (TID 0); else it declines them
	 */
	int	ba;
	int	baproc;
	ulong	replays;	/* frames CCMP's (TKIP's) replay check dropped */

	/*
	 * nfront: the driver gives the signal (Bsignal): the scan goes once
	 * over its channels (scanpass) before choosing, then the strongest
	 * node of those that match
	 */
	int	sigs;
	int	scanpass;
	int	candpass;	/* the pass the first that matches was heard in (-1: none yet) */
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
	Bsignal = 1<<14,	/* nfront: the signal, dBm (signed), the byte before the timestamp's 8 */
};

/* nfront: scan lists for wifi->scan */
extern uchar wifiscan2g[];	/* 2.4 GHz, channels 1-13 (the default) */
extern uchar wifiscan5g[];	/* 2.4 GHz and 5 GHz, 36-64, 100-144, 149-165 */

Wifi *wifiattach(Ether *ether, void (*transmit)(Wifi*, Wnode*, Block*));
void wifiiq(Wifi*, Block*);
void wifiradiogone(Wifi*);	/* nfront */
int wifihdrlen(Wifipkt*);
void wifitxfail(Wifi*, Block*);

char *wifistat(Wifi*, char*, char*);
long wifictl(Wifi*, void*, long);
void wificfg(Wifi*, char*);
