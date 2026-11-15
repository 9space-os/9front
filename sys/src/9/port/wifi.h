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
};

/* nfront: scan lists for wifi->scan */
extern uchar wifiscan2g[];	/* 2.4 GHz, channels 1-13 (the default) */
extern uchar wifiscan5g[];	/* 2.4 GHz and 5 GHz, 36-64, 100-144, 149-165 */

Wifi *wifiattach(Ether *ether, void (*transmit)(Wifi*, Wnode*, Block*));
void wifiiq(Wifi*, Block*);
int wifihdrlen(Wifipkt*);
void wifitxfail(Wifi*, Block*);

char *wifistat(Wifi*, char*, char*);
long wifictl(Wifi*, void*, long);
void wificfg(Wifi*, char*);
