#include <u.h>
#include <libc.h>
#include <keyboard.h>
#include <plumb.h>

static int lightstep = 5, volstep = 3;
static int light, vol, actl;
static int muted = -1;	/* the master volume before a mute here, -1 not muted */

/* the master volume as /dev/volume says ("master l [r]"), -1 if it does not */
static int
mastervol(void)
{
	char b[1024], *l, *f[4];
	int n;

	if((n = pread(vol, b, sizeof(b)-1, 0)) <= 0)
		return -1;
	b[n] = 0;
	for(l = strtok(b, "\n"); l != nil; l = strtok(nil, "\n"))
		if(tokenize(l, f, nelem(f)) >= 2 && strcmp(f[0], "master") == 0)
			return atoi(f[1]);
	return -1;
}

/*
 * the volume up or down by d: the mixer's, else master's from what it is
 * (audio(3) takes "master +3" as 3, not as 3 more)
 */
static void
volume(int d)
{
	int v;

	if(fprint(vol, "mix %+d", d) >= 0)
		return;
	if((v = mastervol()) < 0)
		return;
	v += d;
	if(v < 0)
		v = 0;
	if(v > 100)
		v = 100;
	fprint(vol, "master %d", v);
	muted = -1;
}

/* (un)mute: the audio device's toggle, else master to 0 and back */
static void
mute(void)
{
	int v;

	if(fprint(actl, "master toggle") >= 0)
		return;
	if(muted >= 0){
		fprint(vol, "master %d", muted);
		muted = -1;
	}else if((v = mastervol()) > 0){
		fprint(vol, "master 0");
		muted = v;
	}
}

static void
aplumb(char *s)
{
	int f;

	if((f = plumbopen("send", OWRITE)) >= 0){
		plumbsendtext(f, "shortcuts", "audio", "/", s);
		close(f);
	}
}

static void
process(char *s)
{
	char b[128], *p;
	int n, o, skip;
	Rune r;

	o = 0;
	b[o++] = *s;

	for(p = s+1; *p != 0; p += n){
		if((n = chartorune(&r, p)) == 1 && r == Runeerror){
			/* bail out */
			n = strlen(p);
			memmove(b+o, p, n);
			o += n;
			p += n;
			break;
		}

		if(skip = (*s == 'c')){
			if(r == Kbrtdn)
				fprint(light, "lcd %+d", -lightstep);
			else if(r == Kbrtup)
				fprint(light, "lcd %+d", lightstep);
			else if(r == Kmute)
				mute();
			else if(r == Ksbwd)
				aplumb("key <");
			else if(r == Ksfwd)
				aplumb("key >");
			else if(r == Kpause)
				aplumb("key p");
			else if(r == Kvoldn)
				volume(-volstep);
			else if(r == Kvolup)
				volume(volstep);
			else{
				skip = 0;
			}
		}

		if(!skip){
			memmove(b+o, p, n);
			o += n;
		}
	}

	/* all runes filtered out - ignore completely */
	if(o == 1 && p-s > 1)
		return;

	b[o++] = 0;
	if(write(1, b, o) != o)
		exits(nil);
}

static void
usage(void)
{
	fprint(2, "usage: [-l light_step] [-v vol_step] %s\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char b[128];
	int i, j, n;

	ARGBEGIN{
	case 'l':
		lightstep = atoi(EARGF(usage()));
		break;
	case 'v':
		volstep = atoi(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND

	light = open("/dev/light", OWRITE);
	/* read too: the master volume to step from (mastervol) */
	if((vol = open("/dev/volume", ORDWR)) < 0)
		vol = open("/dev/volume", OWRITE);
	actl = open("/dev/audioctl", OWRITE);
	for(i = 0;;){
		if((n = read(0, b+i, sizeof(b)-i)) <= 0)
			break;
		n += i;
		for(j = 0; j < n; j++){
			if(b[j] == 0){
				process(b+i);
				i = j+1;
			}
		}
		memmove(b, b+i, j-i);
		i -= j;
	}

	exits(nil);
}
