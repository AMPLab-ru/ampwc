#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <wayland-server.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "xdg-shell-server.h"

#include "common.h"
#include "macro.h"
#include "seat.h"
#include "wl-server.h"

#define SEAT_NAME "seat0"

static void
pointer_set_cursor(struct wl_client *client, struct wl_resource *resource,
	uint32_t serial, struct wl_resource *surface,
	int32_t hotspot_x, int32_t hotspot_y)
{
	error(1, "writeme");
}

static void
pointer_release(struct wl_client *client, struct wl_resource *resource)
{
	debug("unimplemented");
}

const struct wl_pointer_interface pointer_interface = {
	.set_cursor = pointer_set_cursor,
	.release = pointer_release,
};

static void
keyboard_release(struct wl_client *client, struct wl_resource *resource)
{
	debug("unimplemented");
}

const struct wl_keyboard_interface keyboard_interface = {
	.release = keyboard_release,
};

static int open_restricted(const char *path, int flags, void *user_data)
{
	return open(path, flags);
}

static void close_restricted(int fd, void *user_data)
{
	close(fd);
}

//actually not so restricted :-)
struct libinput_interface input_iface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

static struct seat *
seat_new()
{
	struct seat *res;

	res = xmalloc(sizeof(*res));
	memset(res, 0, sizeof(*res));
	if ((res->udev = udev_new()) == NULL)
		error(1, "Can not create udev object.");
	res->input = libinput_udev_create_context(&input_iface, res, res->udev);
	assert(res->input);

	libinput_udev_assign_seat(res->input, SEAT_NAME);
	res->ifd = libinput_get_fd(res->input);
	return res;
}

static void
seat_free(struct seat *ps)
{
	if (ps == NULL)
		return;
	if (ps->udev)
		udev_unref(ps->udev);
	if (ps->input)
		libinput_unref(ps->input);
	free(ps);
}

static int
process_keyboard_event(struct amcs_compositor *ctx, struct libinput_event *ev)
{
	struct libinput_event_keyboard *k;
	uint32_t serial, time, key;
	enum libinput_key_state state;
	enum wl_keyboard_key_state wlstate;

	debug("focus = %p", compositor_ctx.seat->focus);

	if (compositor_ctx.seat->focus == NULL || compositor_ctx.seat->focus->keyboard == NULL) {
		warning("can't send keyboard event, no client keyboard connection");
		return 1;
	}

	k = libinput_event_get_keyboard_event(ev);

	time = libinput_event_keyboard_get_time(k);
	key = libinput_event_keyboard_get_key(k);
	state = libinput_event_keyboard_get_key_state(k);
	if (state == LIBINPUT_KEY_STATE_RELEASED) {
		wlstate = WL_KEYBOARD_KEY_STATE_RELEASED;
	} else if (state == LIBINPUT_KEY_STATE_PRESSED) {
		wlstate = WL_KEYBOARD_KEY_STATE_PRESSED;
	} else {
		warning("unknown key state %d", state);
		return 1;
	}
	struct amcs_win *w;
	struct amcs_surface *surf;
	struct wl_array arr;

	if (pvector_len(&ctx->cur_wins) == 0)
		return 1;
	w = pvector_get(&ctx->cur_wins, 0);
	serial = wl_display_next_serial(ctx->display);
	if (w != NULL && w->opaq != NULL) {
		debug("send enter");
		surf = w->opaq;	//TODO, rewrite with getter?
		assert(surf && "can't get amcs_surface from amcs_win");

		wl_array_init(&arr);
		wl_keyboard_send_enter(compositor_ctx.seat->focus->keyboard,
			serial, surf->res, &arr);
		wl_array_release(&arr);

		wl_keyboard_send_key(compositor_ctx.seat->focus->keyboard,
			wl_display_next_serial(ctx->display),
			time, key, wlstate);

		if (surf->redraw_cb)
			wl_callback_send_done(surf->redraw_cb, get_time());
	}

	debug("send (time, key, wlstate) (%d, %d, %d)", time, key, wlstate);
	/*
	wl_keyboard_send_key(wl_resource_from_link(&compositor_ctx.seat->resources)
	    WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP,;
	*/
	return 0;
}

static int
get_dev_caps(struct libinput_device *dev)
{
	int res = 0;
	if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD))
		res |= WL_SEAT_CAPABILITY_KEYBOARD;
	if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER))
		res |= WL_SEAT_CAPABILITY_POINTER;;
	return res;
}

static void
update_capabilities(struct libinput_device *dev, int isAdd)
{
	struct amcs_client *c;
	int caps;

	assert(isAdd == 1 && "unimplemented yet");

	caps = get_dev_caps(dev);
	if ((compositor_ctx.seat->capabilities & caps) !=  caps) {
		compositor_ctx.seat->capabilities |= caps;
		wl_list_for_each(c, &compositor_ctx.clients, link) {
			wl_seat_send_capabilities(c->seat, compositor_ctx.seat->capabilities);
		}
	}
}

int
notify_seat(int fd, uint32_t mask, void *data)
{
	struct amcs_compositor *ctx;
	struct seat *seat = compositor_ctx.seat;
	struct libinput_event *ev = NULL;

	ctx = (struct amcs_compositor *) data;
	debug("notify_seat triggered");
	libinput_dispatch(seat->input);
	while ((ev = libinput_get_event(seat->input)) != NULL) {
		struct libinput_device *dev;
		int evtype;
		evtype = libinput_event_get_type(ev);
		switch (evtype) {
		case LIBINPUT_EVENT_DEVICE_ADDED:
			dev = libinput_event_get_device(ev);
			debug("device added sysname = %s, name %s",
					libinput_device_get_sysname(dev),
					libinput_device_get_name(dev));
			update_capabilities(dev, 1);
			break;
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			debug("device removed");
			update_capabilities(dev, 0);
			break;
		case LIBINPUT_EVENT_KEYBOARD_KEY:
			process_keyboard_event(ctx, ev);
			break;
		case LIBINPUT_EVENT_POINTER_MOTION:
			debug("mouse moved");
			break;
		case LIBINPUT_EVENT_POINTER_BUTTON:
			debug("mouse key pressed");
			break;
		default:
			debug("next input event %p, type = %d", ev, evtype);
			break;
		}

		libinput_event_destroy(ev);
	}
	return 0;
}

static void
seat_get_pointer(struct wl_client *client, struct wl_resource *resource,
	uint32_t id)
{
	struct wl_resource *res;
	struct amcs_client *c;

	c = wl_resource_get_user_data(resource);

	RESOURCE_CREATE(res, client, &wl_pointer_interface,
			wl_resource_get_version(resource), id);
	wl_resource_set_implementation(res, &pointer_interface, resource, NULL);
	assert(c->pointer == NULL && "pointer not null");
	c->pointer = res;
}

static void
seat_get_keyboard(struct wl_client *client, struct wl_resource *resource,
	uint32_t id)
{
	struct wl_resource *res;
	struct amcs_client *c;

	c = wl_resource_get_user_data(resource);

	RESOURCE_CREATE(res, client, &wl_keyboard_interface,
			wl_resource_get_version(resource), id);
	wl_resource_set_implementation(res, &keyboard_interface, resource, NULL);
	assert(c->keyboard == NULL && "keyboard not null");
	c->keyboard = res;
	wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, 0, 0);
	wl_keyboard_send_repeat_info(res, 200, 200);
	wl_keyboard_send_modifiers(res,
			wl_display_next_serial(compositor_ctx.display), 0, 0, 0, 0);
}

static void
seat_get_touch(struct wl_client *client, struct wl_resource *resource,
	uint32_t id)
{
	error(2, "not implemented");

}

static void
seat_release(struct wl_client *client, struct wl_resource *resource)
{
	warning("");
}

static const struct wl_seat_interface seat_interface = {
	.get_pointer = seat_get_pointer,
	.get_keyboard = seat_get_keyboard,
	.get_touch = seat_get_touch,
	.release = seat_release
};

static void
unbind_seat(struct wl_resource *resource)
{
	struct amcs_client *c;

	debug("");
	c = wl_resource_get_user_data(resource);
	assert(c);

	if (compositor_ctx.seat->focus != c)
		return;
	if (wl_list_empty(&compositor_ctx.clients)) {
		compositor_ctx.seat->focus = NULL;
	} else {
		struct wl_list *pos;
		pos = compositor_ctx.clients.next;
		compositor_ctx.seat->focus = wl_container_of(pos, c, link);
	}
}

static void
bind_seat(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct amcs_client *c;
	struct wl_resource *resource;

	debug("client %p caps = 0x%x", client, compositor_ctx.seat->capabilities);

	RESOURCE_CREATE(resource, client, &wl_seat_interface, version, id);
	c = amcs_get_client(resource);
	assert(c && "can't get client");
	c->seat = resource;

	wl_resource_set_implementation(resource, &seat_interface,
				       c, unbind_seat);
	if (compositor_ctx.seat->capabilities)
		wl_seat_send_capabilities(resource, compositor_ctx.seat->capabilities);
}

int
seat_init(struct amcs_compositor *ctx)
{
	ctx->g.seat = wl_global_create(ctx->display, &wl_seat_interface, 6,
			ctx, &bind_seat);
	if (!ctx->g.seat) {
		warning("can't create seat inteface");
		return 1;
	}
	ctx->seat = seat_new();

	wl_event_loop_add_fd(ctx->evloop, ctx->seat->ifd, WL_EVENT_READABLE,
			notify_seat, ctx);
	return 0;
}

int
seat_focus(struct wl_resource *res)
{
	struct amcs_client *c;

	debug("");
	c = amcs_get_client(res);
	assert(c != NULL);
	compositor_ctx.seat->focus = c;
	return 1;
}

int
seat_finalize(struct amcs_compositor *ctx)
{
	seat_free(compositor_ctx.seat);
	if (ctx->seat)
		wl_global_destroy(ctx->g.seat);
	return 0;

}
