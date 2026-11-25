/*
 * SPDX-License-Identifier: MIT
 *
 * the first GPU in shr(3)'s share gpu whose status says "device kind ..."
 * (gpu(4): an entry is named for its #G/n, so a passed-through card may
 * be /shr/gpu/1 behind QEMU's VGA); /shr/gpu/0 if none does
 */
static char*
gpufind(char *kind)
{
	char path[64], buf[128];
	int n, fd, m;

	for(n = 0; n < 8; n++){
		snprint(path, sizeof path, "/shr/gpu/%d/status", n);
		if((fd = open(path, OREAD)) < 0)
			continue;
		m = read(fd, buf, sizeof buf - 1);
		close(fd);
		if(m <= 0)
			continue;
		buf[m] = 0;
		if(strncmp(buf, "device ", 7) == 0 && strncmp(buf+7, kind, strlen(kind)) == 0)
			return smprint("/shr/gpu/%d", n);
	}
	return "/shr/gpu/0";
}
