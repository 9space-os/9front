/*
 * SPDX-License-Identifier: MIT
 *
 * aux/amdgpu, aux/i915: gpu(4) for several users (each driver's fs.c
 * includes this file).  A user's requests are the user's because of the
 * share they came through, not because of the name in their attach
 * (anyone who reaches a service can send any name; there is no
 * factotum here to prove one): the share gpu is the hostowner's
 * (0700, gpushare.c), and "user name" on the driver's ctl grants the
 * user a share of their own, gpu.name (0700, theirs), with the driver's
 * files as entry N.  Every fid of an attach through it is that user's
 * (srvattach), a context is its creator's (its files 0600), and so are
 * the segments the driver makes for it (segown).
 *
 * The grant is the empty share gpu.grant.name, the hostowner's: shares
 * outlive the driver, and a driver started again posts every granted
 * user's entry again (repostusers): a grant lasts until "user name
 * off" or the reboot.  gpu.name itself proves nothing (any user but
 * none may make a share, as theirs): one found is taken over, its
 * entries and itself removed and made anew.
 */
/* is uid the hostowner? (as the kernel's iseve) */
static int
iseve9p(char *uid)
{
	static char eve[64];
	int fd, n;

	if(eve[0] == 0 && (fd = open("/dev/hostowner", OREAD)) >= 0){
		n = read(fd, eve, sizeof eve - 1);
		close(fd);
		eve[n > 0 ? n : 0] = 0;
	}
	return uid != nil && eve[0] != 0 && strcmp(uid, eve) == 0;
}

/* every fid of the attach is the srv's user (Srv.aux) */
static void
srvattach(Req *r)
{
	char *u;

	if((u = r->srv->aux) != nil){
		free(r->fid->uid);
		r->fid->uid = estrdup9p(u);
	}
	respond(r, nil);
}

/* the file (/srv's, #g's) the user's, mode */
static int
chownfile(char *path, char *user, ulong mode)
{
	Dir d;

	nulldir(&d);
	d.uid = user;
	d.mode = mode;
	return dirwstat(path, &d);
}

/* a share #σc/name: its owner (nil if there is none); free it */
static char*
shareowner(char *name)
{
	char path[64];
	Dir *d;
	char *u;

	snprint(path, sizeof path, "#σc/%s", name);
	if((d = dirstat(path)) == nil)
		return nil;
	u = estrdup9p(d->uid);
	free(d);
	return u;
}

/* share name gone: its entries first (shr removes empty shares only); eve's may */
static void
shareremove(char *name)
{
	char path[80];
	Dir *d;
	int fd, n, i;

	snprint(path, sizeof path, "#σc/%s", name);
	/*
	 * someone else's (a user's 0700): ours first, to read its entries
	 * (shr(3) checks owner and mode on open, the hostowner too; it may
	 * wstat any share)
	 */
	chownfile(path, getuser(), 0700);
	if((fd = open(path, OREAD)) >= 0){
		while((n = dirread(fd, &d)) > 0){
			for(i = 0; i < n; i++){
				snprint(path, sizeof path, "#σc/%s/%s", name, d[i].name);
				remove(path);
			}
			free(d);
		}
		close(fd);
	}
	snprint(path, sizeof path, "#σc/%s", name);
	remove(path);
}

/* the share name made anew, ours (we may then put entries in it) */
static int
sharemake(char *name)
{
	char path[64];
	int fd;

	shareremove(name);
	snprint(path, sizeof path, "#σc/%s", name);
	if((fd = create(path, OREAD, DMDIR|0700)) < 0)
		return -1;
	close(fd);
	return 0;
}

/* has this driver posted user's share (add: it has now; -1: it has not any more) */
static int
posted(char *user, int add)
{
	static char *users[64];
	int i, slot;

	slot = -1;
	for(i = 0; i < nelem(users); i++){
		if(users[i] == nil){
			if(slot < 0)
				slot = i;
			continue;
		}
		if(strcmp(users[i], user) == 0){
			if(add < 0){
				free(users[i]);
				users[i] = nil;
			}
			return 1;
		}
	}
	if(add > 0 && slot >= 0)
		users[slot] = estrdup9p(user);
	return 0;
}

/*
 * user's share gpu.user with a srv like proto for user as entry N, then
 * the share theirs (0700): the hostowner may put an entry only into a
 * share it owns (shr(3)), so it is made, filled and then given away;
 * nil or the error
 */
static char*
postuser(Srv *proto, char *user)
{
	static char err[ERRMAX];
	char share[64], path[80];
	Srv *s;
	int fd, sfd;

	if(user == nil || *user == 0 || strchr(user, '/') != nil || strchr(user, '.') != nil)
		return "bad user name";
	snprint(share, sizeof share, "gpu.%s", user);
	snprint(path, sizeof path, "#σc/%s/%s", share, gpuname);
	/* posted by this driver already, the share still the user's (the hostowner cannot look into it: 0700) */
	if(posted(user, 0)){
		char *o = shareowner(share);
		int ours = o != nil && strcmp(o, user) == 0;

		free(o);
		if(ours)
			return nil;
	}
	if(sharemake(share) < 0)
		goto Err;
	s = emalloc9p(sizeof *s);
	memset(s, 0, sizeof *s);
	s->tree = proto->tree;
	s->attach = srvattach;
	s->open = proto->open;
	s->read = proto->read;
	s->write = proto->write;
	s->destroyfid = proto->destroyfid;
	s->aux = estrdup9p(user);
	if((sfd = threadpostsrv(s, nil)) < 0)
		goto Err;
	if((fd = create(path, OWRITE|ORCLOSE|OCEXEC, 0600)) < 0){
		close(sfd);
		goto Err;
	}
	if(fprint(fd, "%d\n", sfd) < 0){
		close(fd);
		close(sfd);
		goto Err;
	}
	close(sfd);
	/* fd stays: the entry goes with the driver (ORCLOSE) */
	snprint(path, sizeof path, "#σc/%s", share);
	if(chownfile(path, user, 0700) < 0)
		goto Err;
	posted(user, 1);
	return nil;
Err:
	rerrstr(err, sizeof err);
	shareremove(share);
	return err;
}

/* "user name": the grant (gpu.grant.name, the hostowner's) and the user's share */
static char*
grantuser(Srv *proto, char *user)
{
	static char err[ERRMAX];
	char grant[64], path[80];
	char *o, *e;
	int fd, ok;

	if(user == nil || *user == 0 || strchr(user, '/') != nil || strchr(user, '.') != nil)
		return "bad user name";
	snprint(grant, sizeof grant, "gpu.grant.%s", user);
	o = shareowner(grant);
	ok = o != nil && iseve9p(o);
	free(o);
	if(!ok){
		/* none, or someone else's of that name: ours anew */
		shareremove(grant);
		snprint(path, sizeof path, "#σc/%s", grant);
		if((fd = create(path, OREAD, DMDIR|0700)) < 0){
			rerrstr(err, sizeof err);
			return err;
		}
		close(fd);
	}
	if((e = postuser(proto, user)) != nil){
		shareremove(grant);
		return e;
	}
	return nil;
}

/* "user name off": the user's entry, share and grant gone */
static char*
revokeuser(char *user)
{
	char name[64];

	if(user == nil || *user == 0 || strchr(user, '/') != nil || strchr(user, '.') != nil)
		return "bad user name";
	posted(user, -1);
	snprint(name, sizeof name, "gpu.%s", user);
	shareremove(name);
	snprint(name, sizeof name, "gpu.grant.%s", user);
	shareremove(name);
	return nil;
}

/*
 * at the start: every user granted before (gpu.grant.name, the
 * hostowner's: a share of that name someone else made grants nothing)
 * gets their entry again
 */
static void
repostusers(Srv *proto)
{
	Dir *d;
	char *e;
	int fd, n, i;

	if((fd = open("#σc", OREAD)) < 0)
		return;
	while((n = dirread(fd, &d)) > 0){
		for(i = 0; i < n; i++){
			if(strncmp(d[i].name, "gpu.grant.", 10) != 0 || !iseve9p(d[i].uid))
				continue;
			if((e = postuser(proto, d[i].name + 10)) != nil)
				fprint(2, "%s: user %s: %s\n", argv0, d[i].name + 10, e);
		}
		free(d);
	}
	close(fd);
}

/* a segment the driver made for a client of user: the user's alone */
static void
segown(char *seg, char *user)
{
	char path[64];

	if(seg == nil || *seg == 0 || user == nil || strcmp(user, getuser()) == 0)
		return;
	/* the segment's owner and mode are its files' (its directory takes no wstat) */
	snprint(path, sizeof path, "#g/%s/ctl", seg);
	if(chownfile(path, user, 0600) < 0)
		fprint(2, "%s: segment %s to %s: %r\n", argv0, seg, user);
}
