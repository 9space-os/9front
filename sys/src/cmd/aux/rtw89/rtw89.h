/*
 * SPDX-License-Identifier: MIT
 *
 * aux/rtw89: what the Plan 9 side calls on the Linux side
 * (linux/rtw89.c); plain C types only, both sides include it.
 */
/* before rtw89_start: printk's level */
void	rtw89_options(int loglevel);
/* rtw89's PCI driver registered: it probes the device of #G (efuse, firmware); 0 or -errno */
int	rtw89_start(void);
