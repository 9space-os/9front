/*
 * SPDX-License-Identifier: MIT
 *
 * xdg_wm_dialog_v1 (wayland-protocols' staging xdg-dialog): a toplevel of
 * another one (set_parent) is a dialog; set_modal makes its window wsys'
 * modal one (wsys(4): a press in the parent's contents raises the dialog
 * instead), unset_modal or the dialog object's end undoes it.
 */
#include "dat.h"
#include <xdg-dialog-v1-server-protocol.h>

static void
modal(struct wl_resource *r, int on)
{
	Surface *s;

	if((s = wl_resource_get_user_data(r)) == NULL)
		return;
	s->modal = on;
	winrelate(s);
}

static void
dialogdestroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	modal(r, 0);
	wl_resource_destroy(r);
}

static void
setmodal(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	modal(r, 1);
}

static void
unsetmodal(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	modal(r, 0);
}

static void
dialogdestroyres(struct wl_resource *r)
{
	Surface *s;

	if((s = wl_resource_get_user_data(r)) != NULL)
		s->dialog = NULL;
}

static const struct xdg_dialog_v1_interface dialogimpl = {
	dialogdestroy,
	setmodal,
	unsetmodal,
};

static void
resdestroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
getdialog(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *toplevel)
{
	struct wl_resource *d;
	Surface *s;

	if((s = wl_resource_get_user_data(toplevel)) != NULL && s->dialog != NULL){
		wl_resource_post_error(r, XDG_WM_DIALOG_V1_ERROR_ALREADY_USED, "a dialog object already");
		return;
	}
	if((d = wl_resource_create(c, &xdg_dialog_v1_interface, wl_resource_get_version(r), id)) == NULL){
		wl_client_post_no_memory(c);
		return;
	}
	/* the toplevel's surface (xdg.c's user data); if it goes first, nil */
	wl_resource_set_implementation(d, &dialogimpl, s, dialogdestroyres);
	if(s != NULL)
		s->dialog = d;
}

static const struct xdg_wm_dialog_v1_interface mgrimpl = {
	resdestroy,
	getdialog,
};

static void
mgrbind(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *r;

	(void)data;
	if((r = wl_resource_create(c, &xdg_wm_dialog_v1_interface, version, id)) == NULL){
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(r, &mgrimpl, NULL, NULL);
}

void
dialoginit(void)
{
	wl_global_create(display, &xdg_wm_dialog_v1_interface, 1, NULL, mgrbind);
}
