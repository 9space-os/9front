#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "riscv.h"

/*
 * calls to the supervisor binary interface (OpenSBI)
 */

void
sbisettimer(uvlong when)
{
	if(sbicall(SBITIME, 0, when, 0, 0, nil) < 0)
		sbicall(SBILEGACYTIMER, 0, when, 0, 0, nil);
}

void
sbisendipi(int hart)
{
	/* hart mask 1<<(hart-base), base */
	sbicall(SBIIPI, 0, 1, hart, 0, nil);
}

int
sbihartstart(int hart, uintptr pc, uintptr opaque)
{
	if(sbicall(SBIHSM, 0, hart, pc, opaque, nil) < 0)
		return -1;
	return 0;
}

void
sbihartstop(void)
{
	sbicall(SBIHSM, 1, 0, 0, 0, nil);
}

void
sbireset(int type)
{
	sbicall(SBISRST, 0, type, 0, 0, nil);
}

/*
 * the firmware's console (the legacy putchar), on the uart the firmware
 * set up: main's console until uartconsinit, so that what goes wrong
 * before it (bootargsinit, meminit, a panic) is seen (as linux's earlycon=sbi)
 */
void
sbiputs(char *s, int n)
{
	for(; n > 0; n--, s++){
		if(*s == '\n')
			sbicall(SBILEGACYPUTC, 0, '\r', 0, 0, nil);
		sbicall(SBILEGACYPUTC, 0, *s, 0, 0, nil);
	}
}
