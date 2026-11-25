/*
 * factotum's wpapsk (the WPA pairwise transient key aux/wpa asks for),
 * run inside 9front (sys/src/build/host/tests/guest/run.sh): a factotum of its own, a key,
 * then the PTK for two challenges through its rpc file, compared with
 * values computed on the host from IEEE 802.11i (Python's hashlib and
 * hmac: PMK = PBKDF2-SHA1(passphrase, ssid, 4096, 32); PTK =
 * PRF-512(PMK, "Pairwise key expansion", Min(AA,SPA) || Max(AA,SPA) ||
 * Min(ANonce,SNonce) || Max(ANonce,SNonce)); the PBKDF2 path checked
 * there against the standard's H.4 vector "password"/"IEEE").
 *
 * The nonces of the first challenge are equal in their first 6 bytes:
 * factotum ordered them by 6 bytes (the address length) instead of 32
 * and got another key.
 *
 * usage: wpatest [factotum] (default /bin/auth/factotum: another one, an
 * older build, to show what it got wrong)
 *
 * Output: "ok name" or "FAIL name: detail" per check, then
 * "wpatest: N ok, M failed".
 */
#include <u.h>
#include <libc.h>
#include <auth.h>

enum {
	Eaddrlen = 6,
	Noncelen = 32,
	PTKlen = 64,
};

static int nok, nfail;

static void
check(char *name, int ok, char *fmt, ...)
{
	char buf[256];
	va_list arg;

	if(ok){
		nok++;
		print("ok %s\n", name);
		return;
	}
	nfail++;
	buf[0] = 0;
	if(fmt != nil){
		va_start(arg, fmt);
		vsnprint(buf, sizeof buf, fmt, arg);
		va_end(arg);
	}
	print("FAIL %s: %s\n", name, buf);
}

static struct {
	char	*name;
	char	*snonce;
	char	*anonce;
	char	*ptk;
} cases[] = {
	{
		"wpapsk ptk, nonces equal in their first 6 bytes",
		"111111111111ffffffffffffffffffffffffffffffffffffffffffffffffffff",
		"1111111111110000000000000000000000000000000000000000000000000000",
		"27177eeefbd8b918d06408cc0c250ab78915097dfa5c4b0a5b955949bb2d1daa"
		"9a4d8f4379adec35e718ca993dcdd0e6e807ec85637a2a255a9214527a39a790",
	},
	{
		"wpapsk ptk, nonces differing in their first byte",
		"2233333333333333333333333333333333333333333333333333333333333333",
		"4455555555555555555555555555555555555555555555555555555555555555",
		"a680a8d48b5ef6b59740f7df27110709479d69755e27859d122c6087e2b2da67"
		"16a5d3683eb4fba00a8d13c8a541f8908cba6ebd41b922504b621d33e97289a1",
	},
};

static char mtpt[] = "/tmp/wpatest.ft";
static char *factotum = "/bin/auth/factotum";
static char essid[] = "9space-test";
static char smac[] = "020000000001";
static char amac[] = "020000000002";

static int
unhex(uchar *d, char *s, int n)
{
	return dec16(d, n, s, strlen(s)) == n ? 0 : -1;
}

/* a factotum of our own, mounted on mtpt, with the test's key */
static int
startfactotum(void)
{
	char srv[64], path[128];
	int fd, i;

	snprint(srv, sizeof srv, "wpatest.%d", getpid());
	if((fd = create(mtpt, OREAD, DMDIR|0700)) >= 0)
		close(fd);
	switch(fork()){
	case -1:
		return -1;
	case 0:
		execl(factotum, "factotum", "-n", "-s", srv, "-m", mtpt, nil);
		_exits("exec");
	}
	waitpid();	/* factotum forks itself into the background */
	snprint(path, sizeof path, "%s/factotum/ctl", mtpt);
	for(i = 0; i < 50 && access(path, AEXIST) < 0; i++)
		sleep(100);
	if((fd = open(path, OWRITE)) < 0)
		return -1;
	if(fprint(fd, "key proto=wpapsk role=client essid=%s !password='9space wpa test'\n", essid) < 0){
		close(fd);
		return -1;
	}
	close(fd);
	snprint(path, sizeof path, "/srv/%s", srv);
	remove(path);	/* the mount stays */
	return 0;
}

static void
testptk(int i)
{
	uchar ch[2*Eaddrlen + 2*Noncelen], want[PTKlen], *p;
	char path[128], start[128];
	AuthRpc *rpc;
	int fd, r;

	p = ch;
	if(unhex(p, smac, Eaddrlen) < 0 || unhex(p+Eaddrlen, amac, Eaddrlen) < 0
	|| unhex(p+2*Eaddrlen, cases[i].snonce, Noncelen) < 0
	|| unhex(p+2*Eaddrlen+Noncelen, cases[i].anonce, Noncelen) < 0
	|| unhex(want, cases[i].ptk, PTKlen) < 0){
		check(cases[i].name, 0, "bad test vector");
		return;
	}
	snprint(path, sizeof path, "%s/factotum/rpc", mtpt);
	if((fd = open(path, ORDWR)) < 0){
		check(cases[i].name, 0, "open %s: %r", path);
		return;
	}
	rpc = auth_allocrpc(fd);
	snprint(start, sizeof start, "proto=wpapsk role=client essid=%s", essid);
	if((r = auth_rpc(rpc, "start", start, strlen(start))) != ARok)
		check(cases[i].name, 0, "start: %d %r", r);
	else if((r = auth_rpc(rpc, "write", ch, sizeof ch)) != ARok)
		check(cases[i].name, 0, "write: %d %r", r);
	else if((r = auth_rpc(rpc, "read", nil, 0)) != ARok)
		check(cases[i].name, 0, "read: %d %r", r);
	else if(rpc->narg != PTKlen)
		check(cases[i].name, 0, "ptk of %d bytes", rpc->narg);
	else
		check(cases[i].name, memcmp(rpc->arg, want, PTKlen) == 0, "%.*H", PTKlen, rpc->arg);
	auth_freerpc(rpc);
	close(fd);
}

void
main(int argc, char **argv)
{
	int i;

	if(argc > 1)
		factotum = argv[1];
	fmtinstall('H', encodefmt);
	rfork(RFNAMEG);
	if(startfactotum() < 0)
		check("wpapsk: a factotum of our own", 0, "%r");
	else
		for(i = 0; i < nelem(cases); i++)
			testptk(i);
	print("wpatest: %d ok, %d failed\n", nok, nfail);
	exits(nfail ? "failed" : nil);
}
