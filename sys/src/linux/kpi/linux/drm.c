// SPDX-License-Identifier: MIT
/*
 * LinuxKPI for 9front: the module_init of DRM's core and helpers, as
 * Linux runs them at boot, for the GPU drivers (aux/amdgpu, aux/i915);
 * apart from main.c, so that a driver without DRM (aux/rtw89) links
 * without it.
 */
#include <linux/kernel.h>
#include <kpi/linux.h>

int	kpi_init_drm_core_init(void);
int	kpi_init_drm_display_helper_module_init(void);
int	kpi_init_gpu_buddy_module_init(void);
int	kpi_init_mipi_dsi_bus_init(void);

int
kpi_drm_start(void)
{
	int r;

	if ((r = kpi_init_drm_core_init()) < 0)
		return r;
	if ((r = kpi_init_gpu_buddy_module_init()) < 0)
		return r;
	if ((r = kpi_init_mipi_dsi_bus_init()) < 0)
		return r;
	return kpi_init_drm_display_helper_module_init();
}
