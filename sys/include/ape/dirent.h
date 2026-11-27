#ifndef	__DIRENT_H
#define	__DIRENT_H
#pragma lib "/$M/lib/ape/libap.a"

#ifndef __STAT_H
#include <sys/stat.h>
#endif

/*
 * this must be a power of 2 and a multiple of all the ones in the system
 */
#define MAXNAMLEN 255

struct	dirent {
	char	d_name[MAXNAMLEN + 1];
	struct stat d_stat;
};

typedef struct _dirdesc {
	int	dd_fd;		/* file descriptor */
	long	dd_loc;		/* buf offset of entry from last readdir() */
	long	dd_size;	/* amount of valid data in buffer */
	char	*dd_buf;	/* directory data buffer */
	void *dirs;
	int	dirsize;
	int	dirloc;
} DIR;


#ifdef __cplusplus
extern "C" {
#endif

/*
 * functions defined on directories
 */
DIR		*opendir(const char *);
struct dirent	*readdir(DIR *);
void		rewinddir(DIR *);
int		closedir(DIR *);
#if defined(__GNUC__) || defined(_APE_GNU)
/* GCC port: POSIX.1-2008's dirfd, the DIR's descriptor (ap/gnu/at.c) */
int		dirfd(DIR *);
#endif

#ifdef __cplusplus
}
#endif

#endif
