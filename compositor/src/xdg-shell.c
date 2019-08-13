#include <assert.h>
#include <stdio.h>

#include <wayland-server.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "xdg-shell-server.h"

#include "common.h"
#include "macro.h"
#include "seat.h"
#include "wl-server.h"

static void
destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
xsurf_set_parent(struct wl_client *client, struct wl_resource *resource,
	struct wl_resource *parent_resource)
{
	warning("");
}

static void
xsurf_set_title(struct wl_client *client, struct wl_resource *resource,
	const char *title)
{
	struct amcs_surface *mysurf;

	assert(title);
	debug("");
	mysurf = wl_resource_get_user_data(resource);
	mysurf->title = title;
}

static void
xsurf_set_app_id(struct wl_client *client, struct wl_resource *resource,
	const char *app_id)
{
	struct amcs_surface *mysurf;

	assert(app_id);
	debug("");
	mysurf = wl_resource_get_user_data(resource);
	mysurf->app_id = app_id;
}

static void
xsurf_show_window_menu(struct wl_client *client, struct wl_resource *resource,
	struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y)
{
	warning("");
}

static void
xsurf_move(struct wl_client *client, struct wl_resource *resource,
	struct wl_resource *seat, uint32_t serial)
{
	warning("");
}

static void
xsurf_resize(struct wl_client *client, struct wl_resource *resource,
	struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
	warning("");
}

static void
xsurf_set_max_size(struct wl_client *client, struct wl_resource *resource,
	int32_t width, int32_t height)
{
	warning("");
}

static void
xsurf_set_min_size(struct wl_client *client, struct wl_resource *resource,
	int32_t width, int32_t height)
{
	warning("");
}

static void
xsurf_set_maximized(struct wl_client *client, struct wl_resource *resource)
{
	warning("");
}

static void
xsurf_unset_maximized(struct wl_client *client, struct wl_resource *resource)
{
	warning("");
}

static void
xsurf_set_fullscreen(struct wl_client *client, struct wl_resource *resource,
	struct wl_resource *output)
{
	warning("");
}

static void
xsurf_unset_fullscreen(struct wl_client *client, struct wl_resource *resource)
{
	warning("");
}

static void
xsurf_set_minimized(struct wl_client *client, struct wl_resource *resource)
{
	warning("");
}

static const struct xdg_toplevel_interface toplevel_interface = {
	.destroy = destroy,
	.set_parent = xsurf_set_parent,
	.set_title = xsurf_set_title,
	.set_app_id = xsurf_set_app_id,
	.show_window_menu = xsurf_show_window_menu,
	.move = xsurf_move,
	.resize = xsurf_resize,
	.set_max_size = xsurf_set_max_size,
	.set_min_size = xsurf_set_min_size,
	.set_maximized = xsurf_set_maximized,
	.unset_maximized = xsurf_unset_maximized,
	.set_fullscreen = xsurf_set_fullscreen,
	.unset_fullscreen = xsurf_unset_fullscreen,
	.set_minimized = xsurf_set_minimized,
};

static int
window_update_cb(struct amcs_win *win, void *opaq)
{
	struct amcs_surface *surf;
	uint32_t serial;
	uint32_t *p;
	struct wl_array arr;

	surf = opaq;
	if (win->w == win->buf.w &&
	    win->h == win->buf.h)
		return 0;

	surf->w = win->w;
	surf->h = win->h;

	wl_array_init(&arr);
	p = wl_array_add(&arr, sizeof(*p));
	*p = XDG_TOPLEVEL_STATE_ACTIVATED;
	p = wl_array_add(&arr, sizeof(*p));
	*p = XDG_TOPLEVEL_STATE_RESIZING;

	xdg_toplevel_send_configure(surf->xdgtopres, win->w, win->h, &arr);
	serial = wl_display_next_serial(compositor_ctx.display);
	surf->pending.xdg_serial = serial;
	xdg_surface_send_configure(surf->xdgres, serial);
	if (surf->redraw_cb)
		wl_callback_send_done(surf->redraw_cb, get_time());

	wl_array_release(&arr);
	return 0;
}

static void
window_init(struct amcs_surface *mysurf)
{
	struct amcs_client *c;
	struct amcs_workspace *ws;
	uint32_t serial;
	uint32_t *newst;

	assert(mysurf);

	serial = wl_display_next_serial(compositor_ctx.display);
	mysurf->pending.xdg_serial = serial;
	debug("generated serial = %d", serial);
	//TODO: newst??????
	newst = wl_array_add(&mysurf->surf_states, sizeof(*newst));
	*newst = XDG_TOPLEVEL_STATE_ACTIVATED;

	ws = pvector_get(&compositor_ctx.workspaces,
			compositor_ctx.cur_workspace);
	mysurf->aw = amcs_workspace_new_win(ws, mysurf, window_update_cb);
	mysurf->w = mysurf->aw->w;
	mysurf->h = mysurf->aw->h;

	xdg_toplevel_send_configure(mysurf->xdgtopres, mysurf->w, mysurf->h,
			&mysurf->surf_states);

	xdg_surface_send_configure(mysurf->xdgres, serial);
	c = amcs_get_client(mysurf->res);
	assert(c && "can't locate client");

	//TODO: uncomment me after wl_output implementation
	//wl_surface_send_enter(mysurf->res, c->output);
}

static void
surf_get_toplevel(struct wl_client *client,
	struct wl_resource *resource, uint32_t id)
{
	struct amcs_surface *mysurf;
	mysurf = wl_resource_get_user_data(resource);

	debug("mysurf %p", mysurf);
	RESOURCE_CREATE(mysurf->xdgtopres, client,  &xdg_toplevel_interface,
			wl_resource_get_version(resource), id);
	wl_resource_set_implementation(mysurf->xdgtopres, &toplevel_interface,
			mysurf, NULL);

	window_init(mysurf);
}

static void
surf_set_window_geometry(struct wl_client *client, struct wl_resource *resource,
	int32_t x, int32_t y, int32_t width, int32_t height)
{
	warning("x = %d y = %d w = %d h = %d", x, y, width, height);
}

static void
surf_ack_configure(struct wl_client *client,
	struct wl_resource *resource, uint32_t serial)
{
//	struct amcs_surface *mysurf;
//	mysurf = wl_resource_get_user_data(resource);

	debug("serial = %d", serial);
	//assert(serial == mysurf->pending.xdg_serial);
}


static const struct xdg_surface_interface surface_interface = {
	.destroy = destroy,
	.get_toplevel = surf_get_toplevel,
	.get_popup = NULL,
	.set_window_geometry = surf_set_window_geometry,
	.ack_configure = surf_ack_configure
};

static void
wmbase_create_positioner(struct wl_client *client, struct wl_resource *resource,
	uint32_t id)
{
	debug("");
}

static void
wmbase_get_xdg_surface(struct wl_client *client, struct wl_resource *resource,
	uint32_t id, struct wl_resource *surface)
{
	struct amcs_surface *mysurf;

	mysurf = wl_resource_get_user_data(surface);

	mysurf->xdgres = wl_resource_create(client, &xdg_surface_interface,
			wl_resource_get_version(resource), id);
	wl_resource_set_implementation(mysurf->xdgres, &surface_interface,
			mysurf, NULL);
}

static void
wmbase_pong(struct wl_client *client, struct wl_resource *resource,
	uint32_t serial)
{
	debug("serial = %x", serial);
}

static const struct xdg_wm_base_interface xdg_base_interface = {
	.destroy = destroy,
	.create_positioner = wmbase_create_positioner,
	.get_xdg_surface = wmbase_get_xdg_surface,
	.pong = wmbase_pong
};

static void
bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;

	debug("");

	RESOURCE_CREATE(resource, client, &xdg_wm_base_interface, version, id);
	wl_resource_set_implementation(resource, &xdg_base_interface,
				       data, NULL);
}

int
xdg_shell_init(struct amcs_compositor *ctx)
{
	debug("shm iface version %d", xdg_wm_base_interface.version);
	ctx->g.shell = wl_global_create(ctx->display, &xdg_wm_base_interface, 1,
			ctx, &bind_shell);
	if (!ctx->g.shell) {
		warning("can't create shell inteface");
		return 1;
	}

	return 0;
}

int
xdg_shell_finalize(struct amcs_compositor *ctx)
{
	if (ctx->g.shell)
		wl_global_destroy(ctx->g.shell);
	ctx->g.shell = NULL;
	return 0;
}

