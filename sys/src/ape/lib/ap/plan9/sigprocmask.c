#include "lib.h"
#include <signal.h>
#include <errno.h>

sigset_t _psigblocked;

int
sigprocmask(int how, sigset_t *set, sigset_t *oset)
{
	if(oset)
		*oset = _psigblocked;
	if(set == 0)	/* POSIX: no change, how not looked at */
		return 0;
	if(how==SIG_BLOCK)
		_psigblocked |= *set;
	else if(how==SIG_UNBLOCK)
		_psigblocked &= ~*set;
	else if(how==SIG_SETMASK)
		_psigblocked = *set;
	else{
		errno = EINVAL;
		return -1;
	}
	return 0;
}
