/* sys/src/build/host/tests/hid: what hid.c takes of libc; time is the test's (nsec) */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define fprint dprintf
extern vlong nsec(void);
extern void werrstr(char*, ...);
