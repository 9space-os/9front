/*
 * SPDX-License-Identifier: MIT
 *
 * aux/kbdtype text: the runes of text typed, as kbd(3) c messages to
 * /dev/kbdin (each with its null byte, which echo cannot write)
 * aux/kbdtype -p hex, -r hex: the key of that rune pressed, let go
 * (kbdfs(8)'s r and R messages: kbdfs makes the k, K and c messages
 * of them), e.g. Kmod4 f868
 * For tests and scripts: sys/src/build/host/tests/ui, sys/src/build/host/tests/gpu's wsyssup.rc, browser/tests
 * (9space's; before, sys/src/build/host/tests/ui's uipeek -k, -p, -r).
 */
#include <u.h>
#include <libc.h>

static void
usage(void)
{
	fprint(2, "usage: aux/kbdtype text | -p hex | -r hex\n");
	exits("usage");
}

static void
send(int fd, char *m, int n)
{
	if(write(fd, m, n) != n)
		sysfatal("write /dev/kbdin: %r");
}

void
main(int argc, char **argv)
{
	char m[UTFmax+2], *p;
	Rune r;
	int fd, n, key;

	key = 0;
	ARGBEGIN{
	case 'p':
		key = 'r';	/* pressed */
		break;
	case 'r':
		key = 'R';	/* let go */
		break;
	default:
		usage();
	}ARGEND
	if(argc != 1)
		usage();
	if((fd = open("/dev/kbdin", OWRITE)) < 0)
		sysfatal("/dev/kbdin: %r");
	if(key){
		r = strtoul(argv[0], nil, 16);
		m[0] = key;
		n = runetochar(m+1, &r);
		m[1+n] = 0;
		send(fd, m, n+2);
		exits(nil);
	}
	for(p = argv[0]; *p; p += n){
		n = chartorune(&r, p);
		m[0] = 'c';
		memmove(m+1, p, n);
		m[1+n] = 0;
		send(fd, m, n+2);
	}
	exits(nil);
}
