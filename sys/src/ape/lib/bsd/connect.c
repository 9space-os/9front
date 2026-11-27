/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

#include "priv.h"
#include "lib.h"
#include "sys9.h"	/* _CLOSE: _sock_srv's descriptor is no APE one */

int
connect(int fd, void *a, int alen)
{
	Rock *r;
	int n, cfd, nfd, sfd;
	char msg[8+256+1], file[8+256+1];
	struct sockaddr *sa;
	struct sockaddr_un *runix;
	static int vers;

	r = _sock_findrock(fd, 0);
	if(r == 0){
		errno = ENOTSOCK;
		return -1;
	}
	sa = (struct sockaddr*)a;
	if(sa->sa_family != r->domain){
		errno = EAFNOSUPPORT;
		return -1;
	}
	if(alen > sizeof(r->raddr)){
		errno = ENAMETOOLONG;
		return -1;
	}
	memmove(&r->raddr, a, alen);

	switch(r->domain){
	case PF_INET:
	case PF_INET6:
		/* set up a tcp or udp connection */
		cfd = open(r->ctl, O_RDWR);
		if(cfd < 0)
			return -1;
		if(_sock_inport(&r->addr) > 0) {
			snprintf(msg, sizeof msg, "connect %s!%d%s %d",
				inet_ntop(sa->sa_family, _sock_inip(sa), file, sizeof(file)),
				_sock_inport(sa),
				r->reserved ? "!r" : "",
				_sock_inport(&r->addr));
		} else {
			snprintf(msg, sizeof msg, "connect %s!%d%s",
				inet_ntop(sa->sa_family, _sock_inip(sa), file, sizeof(file)),
				_sock_inport(sa),
				r->reserved ? "!r" : "");
		}
		n = write(cfd, msg, strlen(msg));
		close(cfd);
		return (n < 0) ? -1 : 0;
	case PF_UNIX:
		/* null terminate the address */
		if(alen == sizeof(r->raddr))
			alen--;
		*(((char*)&r->raddr)+alen) = 0;

		if(r->other < 0){
			errno = EGREG;
			return -1;
		}

		/* put far end of our pipe in /srv */
		snprintf(msg, sizeof msg, "UD.%d.%d", getpid(), vers++);
		sfd = -1;
		if(_sock_srv(msg, r->other, &sfd) < 0){
			r->other = -1;
			return -1;
		}
		r->other = -1;

		/* tell server the /srv file to open */
		runix = (struct sockaddr_un*)&r->raddr;
		_sock_srvname(file, runix->sun_path);
		nfd = open(file, O_RDWR);
		if(nfd < 0){
			_CLOSE(sfd);	/* ORCLOSE: the entry goes */
			return -1;
		}
		if(write(nfd, msg, strlen(msg)) < 0){
			close(nfd);
			_CLOSE(sfd);
			return -1;
		}
		close(nfd);

		/* wait for server to open it and then remove it (ORCLOSE) */
		read(fd, file, sizeof(file));
		_CLOSE(sfd);
		return 0;
	default:
		errno = EAFNOSUPPORT;
		return -1;
	}
}
