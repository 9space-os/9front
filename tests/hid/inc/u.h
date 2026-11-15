/* tests/hid: Plan 9's types for hid.c compiled on the host */
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef long long vlong;
typedef unsigned long long uvlong;
#define nil ((void*)0)
#define USED(...) ((void)(0, ##__VA_ARGS__))
#define nelem(x) (sizeof(x)/sizeof((x)[0]))
