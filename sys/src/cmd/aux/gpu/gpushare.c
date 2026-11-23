/*
 * SPDX-License-Identifier: MIT
 *
 * gpu(4)'s files in shr(3), as nusb's are in share usb: a driver puts
 * its file server into the share gpu as entry N (the N of its #G/N),
 * so its files are /shr/gpu/N in every name space at once, also in
 * those made before it started (a driver killed and started again is
 * there for everyone).  The share is the hostowner's (0700, made by
 * the first driver, kept when that driver goes: only the entry goes
 * with it, ORCLOSE).  Included by gpufs.c and intel/intelgpu.c.
 */

static char *gpuname = "0";	/* N of #G/N: the entry's and the tree's top directory's name */

/* N from the #G directory the driver drives */
static void
gpusetname(char *gdir)
{
	char *p;

	if(gdir != nil && (p = strrchr(gdir, '/')) != nil && p[1] != 0)
		gpuname = estrdup9p(p+1);
}

/* another driver serves GPU N: its entry in the share */
static int
gpuserved(void)
{
	char path[64];

	snprint(path, sizeof path, "#σc/gpu/%s", gpuname);
	return access(path, AEXIST) == 0;
}

/*
 * the file server into the share gpu as entry N, by post: postsharesrv,
 * or threadpostsharesrv for a threaded driver.  The caller names it: a
 * reference to the other here would link libthread (its main) into a
 * driver without threads under kencc, which keeps unused branches
 */
static void
gpupost(Srv *s, void (*post)(Srv*, char*, char*, char*))
{
	post(s, nil, "gpu", gpuname);
}
