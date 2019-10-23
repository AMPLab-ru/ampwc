#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/mman.h>

#include <string.h>
#include <unistd.h>

#include <wayland-client.h>

//
#include <time.h>

#include "xdg-shell-client.h"
#include "macro.h"
#include "utils.h"

#define POOL_SZ 8 * 1024 * 1024

struct client_ctx {
	struct wl_display *disp;
	struct wl_compositor *comp;
	struct wl_shm *shm;
	struct wl_shm_pool *pool;

	struct xdg_wm_base *shell;
	struct wl_seat *seat;

	struct wl_surface *surf;
	struct xdg_surface *xdgsurf;
	struct xdg_toplevel *toplevel;

	//surface size
	int h;
	int w;

	struct wl_buffer *buf;
	void *data;
	int datasz;

	struct wl_event_source *timer_ev;
} g_ctx;

static void
unimplemented(void *data,
	struct wl_surface *wl_surface,
	struct wl_output *output)
{
}

struct wl_surface_listener wl_surf_listener = {
	.enter = unimplemented,
	.leave = unimplemented
};

static void
toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
	int32_t width, int32_t height, struct wl_array *states)
{
	struct client_ctx *ctx;
	int bufsz, stride;

	debug("width = %d, height = %d", width, height);

	ctx = data;
	ctx->h = height;
	ctx->w = width;
	stride =  ctx->w * 4;
	bufsz = stride * ctx->h;
	debug("stride %d", stride);

	if (bufsz > POOL_SZ)
		error(1, "pool is to small");

	ctx->buf = wl_shm_pool_create_buffer(ctx->pool, 0, ctx->w, ctx->h, stride, WL_SHM_FORMAT_ARGB8888);
	debug("wl_buffer created = %p", ctx->buf);

	//xdg_surface_set_window_geometry(ctx->xdgsurf, 0, 0, 600, 800);
	wl_surface_attach(ctx->surf, ctx->buf, 0, 0);
	wl_surface_commit(ctx->surf);
}

static void
toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	debug("close request!!");
	exit(1);
}

static void
xdgsurf_configure(void *data, struct xdg_surface *xdg_surface,
	uint32_t serial)
{
	struct client_ctx *ctx;

	debug("serial = %d", serial);
	//TODO: resize surface
	ctx = data;
	xdg_surface_ack_configure(ctx->xdgsurf, serial);
}

struct xdg_surface_listener xdgsurf_listener = {
	.configure = xdgsurf_configure
};

struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

void
global_add(void *our_data, struct wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version)
{
	debug("global_add iface = %s nm %d, version %d", interface, name, version);
	if (STREQ(interface, "wl_compositor")) {
		g_ctx.comp = wl_registry_bind(registry, name, &wl_compositor_interface, 3);
	} else if (STREQ(interface, "wl_shm")) {
		g_ctx.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (STREQ(interface, "wl_seat")) {
		g_ctx.seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
	} else if(STREQ(interface, "xdg_wm_base")) {
		debug("@WRITEME xdg_shell initialize");
		g_ctx.shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
	}
}

void
global_remove(void *our_data, struct wl_registry *registry, uint32_t name)
{
	debug("global_remove nm %d", name);
}

struct wl_registry_listener registry_listener = {
	.global = global_add,
	.global_remove = global_remove
};

static void
shm_pool_init(struct client_ctx *ctx)
{
	int fd;

	fd = alloc_tempfile(POOL_SZ);
	if (fd < 0)
		error(1, "can't get temp file");

	ctx->data = mmap(NULL, POOL_SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (ctx->data == NULL)
		error(1, "mmap error :-(");
	ctx->datasz = POOL_SZ;

	memset(ctx->data, random() & 0xff, POOL_SZ);

	ctx->pool = wl_shm_create_pool(ctx->shm, fd, POOL_SZ);
	if (ctx->pool == NULL)
		error(1, "can't create shm pool :-(");
	close(fd);
}

void
paint_surface(struct wl_surface *surf)
{
	memset(g_ctx.data, random() & 0xff, g_ctx.datasz);
	wl_surface_commit(surf);
}

int
main(int argc, const char *argv[])
{
	struct wl_display *display = wl_display_connect(NULL);
	int rc, niter;

	srandom(time(NULL));
	if (display == NULL)
		error(1, "can't connect to display");

	g_ctx.disp = display;

	struct wl_registry *registry = wl_display_get_registry(display);

	if (registry == NULL)
		error(2, "can't get registry");

	void *our_data = NULL;	/* arbitrary state you want to keep around */;
	rc = wl_registry_add_listener(registry, &registry_listener, our_data);
	if (rc != 0) {
		warning("wl_registry_add_listener rc = %d", rc);
		goto finalize;
	}
	debug("roundtrip");
	wl_display_roundtrip(display);

	if (!g_ctx.comp) {
		warning("can't get compositor interface");
		goto finalize;
	}

	if (g_ctx.comp == NULL)
		error(1, "can't get compositor interface");
	if (g_ctx.shell == NULL)
		error(2, "can't get xdg_shell interface");
	if (g_ctx.shm == NULL)
		error(3, "can't get shm");
	if (g_ctx.seat == NULL)
		error(4, "can't get seat");

	shm_pool_init(&g_ctx);

	g_ctx.surf = wl_compositor_create_surface(g_ctx.comp);
	if (!g_ctx.surf) {
		warning("can't get surface");
		goto finalize;
	}
	//TODO: wl_seat test

	g_ctx.xdgsurf = xdg_wm_base_get_xdg_surface(g_ctx.shell, g_ctx.surf);
	g_ctx.toplevel = xdg_surface_get_toplevel(g_ctx.xdgsurf);

	wl_surface_add_listener(g_ctx.surf, &wl_surf_listener, &g_ctx);
	xdg_surface_add_listener(g_ctx.xdgsurf, &xdgsurf_listener, &g_ctx);
	xdg_toplevel_add_listener(g_ctx.toplevel, &toplevel_listener, &g_ctx);
	wl_display_roundtrip(display);

	debug("start dispatching stuff");
	niter = 0;
	while (1) {
		niter++;
		debug("wainting for events");
		rc = wl_display_dispatch(display);
		if (rc < 0) {
			warning("dispatch error %d", rc);
			break;
		} else {
			debug("dispatched %d events", rc);
		}
		paint_surface(g_ctx.surf);
		//wl_surface_destroy(surf);
	}

	debug("wayland client finalize!");

finalize:
	debug("finalize");
	if (display) {
		wl_display_disconnect(display);
	}

	return 0;
}
