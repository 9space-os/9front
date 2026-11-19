/*
 * SPDX-License-Identifier: MIT
 *
 * sharetest: gpu(4)'s users in shr(3) (aux/gpu/usersrv.c and
 * gpushare.c, the code aux/i915 and aux/amdgpu run), on a small tree of
 * its own, so that QEMU's virtio-gpu (one user) is not needed:
 *	a share gpu.none someone made first (a squatter's entry in it) is
 *	taken over by the grant ("user none"): none's share is none's,
 *	with only the driver's entry; the grant gpu.grant.none the
 *	hostowner's;
 *	none reads /shr/gpu.none/0/status, not the squatter's, not /shr/gpu;
 *	a driver started again (the entry gone) posts it again for the
 *	grant; a gpu.grant.fake that is not the hostowner's grants nothing;
 *	"user none off" takes all of it away: none's open fails.
 * Run as the hostowner, in a name space with #σ on /shr (the namespace
 * file's).  Lines "ok ..." or "FAIL ..."; "sharetest: done".
 */
#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "../../sys/src/cmd/aux/gpu/gpushare.c"
#include "../../sys/src/cmd/aux/gpu/usersrv.c"

static Srv fs;
static Srv squat;

static void
fsread(Req *r)
{
	readstr(r, (char*)r->fid->file->aux);
	respond(r, nil);
}

static int nfail;

static void
check(int ok, char *what)
{
	print("%s %s\n", ok ? "ok" : "FAIL", what);
	if(!ok)
		nfail++;
}

/* the owner of #σc/name, "" if there is none */
static char*
owner(char *name)
{
	char *o;

	o = shareowner(name);
	return o != nil ? o : "";
}

/* does a process of user none find path? (a child that becomes none) */
static int
noneseen(char *path)
{
	int pid, fd;
	Waitmsg *w;
	int seen;

	switch(pid = fork()){
	case -1:
		return -1;
	case 0:
		if((fd = open("#c/user", OWRITE)) < 0 || write(fd, "none", 4) != 4)
			exits("not none");
		close(fd);
		exits(access(path, AREAD) == 0 ? nil : "unseen");
	}
	seen = 0;
	while((w = wait()) != nil){
		if(w->pid == pid){
			seen = w->msg[0] == 0;
			free(w);
			break;
		}
		free(w);
	}
	return seen;
}

/* an entry name in share share, put there with a service of tree t */
static int
entry(char *share, char *name, Srv *s)
{
	char path[80];
	int fd, sfd;

	if((sfd = threadpostsrv(s, nil)) < 0)
		return -1;
	snprint(path, sizeof path, "#σc/%s/%s", share, name);
	if((fd = create(path, OWRITE|ORCLOSE, 0600)) < 0){
		close(sfd);
		return -1;
	}
	fprint(fd, "%d\n", sfd);
	close(sfd);
	return fd;	/* held: the entry stays while it is open */
}

void
threadmain(int argc, char **argv)
{
	char *e;
	int fd;

	ARGBEGIN{
	}ARGEND
	USED(argc);
	USED(argv);

	fs.tree = alloctree(getuser(), getuser(), DMDIR|0555, nil);
	createfile(createfile(fs.tree->root, "0", getuser(), DMDIR|0555, nil),
		"status", getuser(), 0444, "device sharetest\n");
	fs.read = fsread;
	squat.tree = alloctree(getuser(), getuser(), DMDIR|0555, nil);
	createfile(squat.tree->root, "squatter", getuser(), 0444, "theirs\n");
	squat.read = fsread;

	/* what an earlier run left */
	revokeuser("none");
	shareremove("gpu.grant.fake");
	shareremove("gpu.fake");

	/* a squatter's share gpu.none (here the hostowner's, made so; then none's) */
	if(sharemake("gpu.none") < 0 || entry("gpu.none", "x", &squat) < 0 ||
	   chownfile("#σc/gpu.none", "none", 0700) < 0)
		sysfatal("the squatter's share: %r");
	check(access("/shr/gpu.none/squatter", AEXIST) == 0, "the squatter's share is there");

	e = grantuser(&fs, "none");
	check(e == nil, e != nil ? smprint("user none: %s", e) : "user none granted");
	check(strcmp(owner("gpu.none"), "none") == 0, smprint("gpu.none is none's (%s)", owner("gpu.none")));
	check(iseve9p(owner("gpu.grant.none")), smprint("gpu.grant.none is the hostowner's (%s)", owner("gpu.grant.none")));
	check(noneseen("/shr/gpu.none/0/status") == 1, "none reads /shr/gpu.none/0/status");
	check(noneseen("/shr/gpu.none/squatter") == 0, "none does not see the squatter's files");

	/*
	 * the driver ended: its entry goes (ORCLOSE; here removed, the share
	 * taken back to do so) and a new one starts with nothing posted
	 */
	chownfile("#σc/gpu.none", getuser(), 0700);
	remove("#σc/gpu.none/0");
	chownfile("#σc/gpu.none", "none", 0700);
	posted("none", -1);
	check(noneseen("/shr/gpu.none/0/status") == 0, "the driver gone: none's files gone");
	/* a grant someone else made (here: given to none) grants nothing */
	if((fd = create("#σc/gpu.grant.fake", OREAD, DMDIR|0700)) >= 0)
		close(fd);
	chownfile("#σc/gpu.grant.fake", "none", 0700);
	repostusers(&fs);
	check(noneseen("/shr/gpu.none/0/status") == 1, "started again: none's files back for the grant");
	check(access("#σc/gpu.fake", AEXIST) < 0, "a gpu.grant.fake not the hostowner's posts nothing");

	e = revokeuser("none");
	check(e == nil, "user none off");
	check(access("#σc/gpu.none", AEXIST) < 0 && access("#σc/gpu.grant.none", AEXIST) < 0,
		"gpu.none and gpu.grant.none gone");
	check(noneseen("/shr/gpu.none/0/status") == 0, "none's open fails");
	repostusers(&fs);
	check(access("#σc/gpu.none", AEXIST) < 0, "started again after off: nothing posted");

	shareremove("gpu.grant.fake");
	print("sharetest: done, %d failed\n", nfail);
	threadexitsall(nfail ? "failed" : nil);
}
