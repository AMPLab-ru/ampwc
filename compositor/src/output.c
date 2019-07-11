#include <assert.h>
#include <limits.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include "wl-server.h"
#include "macro.h"
#include "output.h"
#include "udev.h"
#include "common.h"

#define DEFAULT_SURFSZ 1024

#define DRIPATH "/dev/dri/"

static void
update_screen_state(const char *name, int status)
{
	if (status)
		debug("EVENT: %s: connected", name);
	else
		debug("EVENT: %s: disconnected", name);
}

static int
amcs_output_screens_add(struct amcs_output *out, const char *path)
{
	amcs_drm_card *card;
	amcs_drm_dev_list *dev_list;
	struct amcs_screen *screen;

	assert(out && path);

	if ((card = amcs_drm_init(path)) == NULL) {
		return 1;
	}
	pvector_push(&out->cards, card);
	dev_list = card->list;

	while (dev_list) {
		screen = xmalloc(sizeof (*screen));
		screen->w = dev_list->w;
		screen->h = dev_list->h;
		screen->pitch = dev_list->pitch;
		screen->buf = dev_list->buf;
		pvector_push(&out->screens, screen);
		dev_list = dev_list->next;
	}
	return 0;
}

static void
amcs_output_screens_free(struct amcs_output *out)
{
	amcs_drm_card *card;
	struct amcs_screen *screen;
	int i;

	pvector_for_each(i, card, &out->cards) {
		amcs_drm_free(card);
	}
	pvector_clear(&out->cards);
	pvector_for_each(i, screen, &out->screens) {
		free(screen);
	}
	pvector_clear(&out->screens);
}

struct amcs_output *
amcs_output_new()
{
	struct amcs_output *res;

	res = xmalloc(sizeof(*res));
	memset(res, 0, sizeof(*res));
	pvector_init(&res->cards, xrealloc);
	pvector_init(&res->screens, xrealloc);
	res->w = DEFAULT_SURFSZ;
	res->h = DEFAULT_SURFSZ;
	return res;
}

int
amcs_output_reload(struct amcs_output *out)
{
	int i;
	char path[PATH_MAX];
	const char **cards;
	struct amcs_screen *screen;

	amcs_output_screens_free(out);

	cards = amcs_udev_get_cardnames();
	for (i = 0; cards[i] != NULL; ++i) {
		snprintf(path, sizeof(path), "%s%s", DRIPATH, cards[i]);
		amcs_output_screens_add(out, path);
	}
	amcs_udev_free_cardnames(cards);
	out->isactive = true;

	if (pvector_len(&out->screens) < 1) {
		/* Don't change output geometry */
		return 1;
	}

	screen = pvector_get(&out->screens, 0);
	//TODO: use additional screens
	out->w = screen->w;
	out->h = screen->h;
	return 0;
}

int
amcs_output_release(struct amcs_output *out)
{
	amcs_output_screens_free(out);
	out->isactive = false;
	return 0;
}

void
amcs_output_send_info(struct amcs_output *out, struct wl_resource *resource)
{
	wl_output_send_geometry(resource, 0, 0, out->w, out->h, 0,
			"unknown", "unknown", WL_OUTPUT_TRANSFORM_NORMAL);
	wl_output_send_mode(resource, 0, out->w, out->h, 60);
	wl_output_send_scale(resource, 1);
	wl_output_send_done(resource);
}

int
amcs_output_update_region(struct amcs_output *out, struct amcs_win *win)
{
	struct amcs_screen *screen;
	int i, j, h, w;
	size_t offset;

	debug("nscreens %lu", pvector_len(&out->screens));
	// no actual surface
	if (out->isactive == false)
		return 0;
	if (pvector_len(&out->screens) < 1)
		return 0;

	screen = pvector_get(&out->screens, 0);
	//TODO: use additional screens
	h = MIN(win->h, win->buf.h);
	w = MIN(win->w, win->buf.w);
	for (i = 0; i < h; ++i) {
		for (j = 0; j < w; ++j) {
			offset = screen->pitch * (i + win->y) + 4*(j + win->x);
			*(uint32_t*)&screen->buf[offset] = win->buf.dt[win->buf.w * i + j];
		}
	}
	return 0;
}

void
amcs_output_free(struct amcs_output *out)
{
	amcs_output_screens_free(out);
	pvector_free(&out->screens);
	pvector_free(&out->cards);
	free(out);
}

static void
output_release(struct wl_client *client,
	struct wl_resource *resource)
{
	warning("");
}

static const struct wl_output_interface output_interface = {
	.release = output_release,
};

static void
bind_output(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;
	struct amcs_client *c;
	struct amcs_output *out = data;

	debug("");
	RESOURCE_CREATE(resource, client, &wl_output_interface, version, id);
	wl_resource_set_implementation(resource, &output_interface, data, NULL);
	amcs_output_send_info(out, resource);
	c = amcs_get_client(resource);
	assert(c && "can't locate client");
	c->output = resource;
}

int
output_init(struct amcs_compositor *ctx)
{
	ctx->output = amcs_output_new();
	ctx->g.output = wl_global_create(ctx->display, &wl_output_interface,
			3, ctx->output, &bind_output);
	if (!ctx->g.output) {
		warning("can't create output interface");
		return 1;
	}
	//amcs_output_reload(ctx->output);
	amcs_udev_monitor_tracking(update_screen_state);
	return 0;
}

void
output_finalize(struct amcs_compositor *ctx)
{
	if (ctx->output)
		amcs_output_free(ctx->output);
	ctx->output = NULL;
	if (ctx->g.output)
		wl_global_destroy(ctx->g.shell);
	ctx->g.output = NULL;
}

