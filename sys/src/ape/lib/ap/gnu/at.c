/*
 * POSIX.1-2008's *at functions on 9front: a path relative to a directory's
 * descriptor is that directory's name (fd2path(2)) and the path; AT_FDCWD
 * and an absolute path as they are. A descriptor that is not a directory's
 * is ENOTDIR. 9front has no symbolic links: AT_SYMLINK_NOFOLLOW changes
 * nothing. dirfd: a DIR's descriptor (dirent.h's dd_fd).
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* libap (plan9/sys9.h): fd2path(2) */
extern int _FD2PATH(int, char*, int);

/*
 * the path fd and path name: path itself for AT_FDCWD or an absolute one,
 * else fd's directory's name, '/', path in buf (n bytes); nil and errno
 */
static const char*
atpath(int fd, const char *path, char *buf, int n)
{
	char dir[PATH_MAX];
	struct stat st;

	if(path == NULL){
		errno = EFAULT;
		return NULL;
	}
	if(fd == AT_FDCWD || path[0] == '/')
		return path;
	if(path[0] == '\0'){
		errno = ENOENT;
		return NULL;
	}
	if(fstat(fd, &st) < 0)
		return NULL;	/* EBADF */
	if(!S_ISDIR(st.st_mode)){
		errno = ENOTDIR;
		return NULL;
	}
	if(_FD2PATH(fd, dir, sizeof dir) < 0){
		errno = EBADF;
		return NULL;
	}
	if(snprintf(buf, n, "%s/%s", strcmp(dir, "/") == 0 ? "" : dir, path) >= n){
		errno = ENAMETOOLONG;
		return NULL;
	}
	return buf;
}

int
openat(int fd, const char *path, int flags, ...)
{
	char buf[PATH_MAX];
	va_list arg;
	mode_t mode;

	if((path = atpath(fd, path, buf, sizeof buf)) == NULL)
		return -1;
	mode = 0;
	if(flags & O_CREAT){
		va_start(arg, flags);
		mode = va_arg(arg, int);
		va_end(arg);
	}
	return open(path, flags, mode);
}

int
fstatat(int fd, const char *path, struct stat *st, int flags)
{
	char buf[PATH_MAX];

	(void)flags;	/* AT_SYMLINK_NOFOLLOW: no symbolic links */
	if((path = atpath(fd, path, buf, sizeof buf)) == NULL)
		return -1;
	return stat(path, st);
}

/* AT_REMOVEDIR: rmdir, which removes a file as well, as APE's does */
int
unlinkat(int fd, const char *path, int flags)
{
	char buf[PATH_MAX];

	if((path = atpath(fd, path, buf, sizeof buf)) == NULL)
		return -1;
	return flags & AT_REMOVEDIR ? rmdir(path) : unlink(path);
}

int
mkfifoat(int fd, const char *path, mode_t mode)
{
	char buf[PATH_MAX];

	if((path = atpath(fd, path, buf, sizeof buf)) == NULL)
		return -1;
	return mkfifo(path, mode);
}

int
dirfd(DIR *d)
{
	if(d == NULL){
		errno = EINVAL;
		return -1;
	}
	return d->dd_fd;
}
