/*
 *
 * External varibles - most are in glob.c.
 *
 */

extern char	**argv;			/* global so everyone can use them */
extern int	argc;

extern int	x_stat;			/* program exit status */
extern int	debug;			/* debug flag */
extern int	ignore;			/* what we do with FATAL errors */

extern long	lineno;			/* line number */
extern long	position;		/* byte position */
extern char	*prog_name;		/* and program name - for errors */
extern char	*temp_file;		/* temporary file - for some programs */
extern char	*fontencoding;		/* text font encoding scheme */

extern int	dobbox;			/* enable BoundingBox stuff if TRUE */
extern double	pageheight;		/* only for BoundingBox calculations! */
extern double	pagewidth;

extern int	reading;		/* input */
extern int	writing;		/* and output encoding */

extern char	*optarg;		/* for getopt() */
extern int	optind;

extern void	interrupt();
extern char	*tempnam(char*,char*);

/*
 * the C library's and common's functions, declared: undeclared they
 * returned int, wrong where they do not (atof, malloc on a 64-bit
 * machine, error's pointers).  For the programs on APE; tr2post, a
 * native one, has libc.h's
 */
#ifdef _POSIX_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int	cover(double, double);
int	writebbox(FILE*, char*, int);
int	resetbbox(int);
int	scale(double, double);
int	translate(double, double);
int	rotate(double);
int	concat(double*);
int	out_list(char*);
int	in_olist(int);
int	setencoding(char*);
int	cat(char*);
int	str_convert(char**, int);
int	error(int, char*, ...);
int	saverequest(char*);
int	writerequest(int, FILE*);
int	dumprequest(char*, char*, FILE*);
#endif
/* 
 * extern char	*malloc();
 * extern char	*calloc();
 * extern char	*strtok();
 * extern long	ftell();
 * extern double	atof();
 * extern double	sqrt();
 * extern double	atan2();
 */
