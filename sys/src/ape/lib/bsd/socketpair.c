/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>

#include "priv.h"

int
socketpair(int domain, int type, int protocol, int *sv)
{
	Rock *r;
	int i;

	switch(domain){
	case PF_UNIX:
		if(pipe(sv) < 0)
			return -1;
		/*
		 * 9space: rocks for both ends, as socket's: recvmsg keeps the
		 * rest of a message read in part there (r->rest); without one
		 * the rest was taken for the next message's header
		 */
		for(i = 0; i < 2; i++)
			if((r = _sock_newrock(sv[i])) != NULL){
				r->domain = domain;
				r->stype = type;
				r->protocol = protocol;
			}
		return 0;
	default:
		errno = EOPNOTSUPP;
		return -1;
	}
}
