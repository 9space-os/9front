/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

int
ioctl(int fd, unsigned long request, void* arg)
{
	struct stat d;

	if(request == FIONREAD) {
		if(fstat(fd, &d) < 0) {
			errno = EBADF;
			return -1;
		}
		/* this works if the file is buffered somehow */
		/* GCC port: an int, as POSIX's and the ported programs' (a long wrote 8 bytes into their int) */
		*(int*)arg = d.st_size > 0x7fffffff ? 0x7fffffff : d.st_size;
		return 0;
	} else {
		errno = EINVAL;
		return -1;
	}
}
