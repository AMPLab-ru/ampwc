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
#define XKB_STATE(st) (st == LIBINPUT_KEY_STATE_RELEASED ? XKB_KEY_UP : XKB_KEY_DOWN)
#define XKB_KEY(k) (k + 8)


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

static struct amcs_seat *
seat_new()
{
	struct amcs_seat *res;

	res = xmalloc(sizeof(*res));
	memset(res, 0, sizeof(*res));
	if ((res->udev = udev_new()) == NULL)
		error(1, "Can not create udev object.");
	res->input = libinput_udev_create_context(&input_iface, res, res->udev);
	assert(res->input && "can't initialize libinput context");

	libinput_udev_assign_seat(res->input, SEAT_NAME);
	res->ifd = libinput_get_fd(res->input);

	res->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	assert(res->xkb && "can't initialize keyboard context");
	res->keymap = xkb_keymap_new_from_names(res->xkb, NULL, 0);
	assert(res->keymap && "can't create keymap");
	res->kbstate = xkb_state_new(res->keymap);

	return res;
}

static void
seat_free(struct amcs_seat *ps)
{
	if (ps == NULL)
		return;
	if (ps->udev)
		udev_unref(ps->udev);
	if (ps->input)
		libinput_unref(ps->input);
	if (ps->kbstate)
		xkb_state_unref(ps->kbstate);
	if (ps->keymap)
		xkb_keymap_unref(ps->keymap);
	if (ps->xkb)
		xkb_context_unref(ps->xkb);
	free(ps);
}

#define keymod_set(xkb_key, kb_mode) do {				\
	index = xkb_keymap_mod_get_index(seat->keymap,		\
		XKB_MOD_NAME_ ## xkb_key);			\
	if (xkb_state_mod_index_is_active(seat->kbstate, index,	\
		XKB_STATE_MODS_DEPRESSED))			\
		ki->modifiers |= kb_mode;			\
	} while (0)

static void
ki_state_init(struct amcs_key_info *ki, struct amcs_seat *seat, uint32_t keysym,
		uint32_t state)
{
	struct xkb_state *s = seat->kbstate;
	int index;

	ki->mods.depressed = xkb_state_serialize_mods(s, XKB_STATE_MODS_DEPRESSED);
	ki->mods.latched = xkb_state_serialize_mods(s, XKB_STATE_MODS_LATCHED);
	ki->mods.locked = xkb_state_serialize_mods(s, XKB_STATE_MODS_LOCKED);
	ki->keysym = keysym;
	ki->state = state;
	ki->modifiers = 0;

	keymod_set(CTRL, KB_CTRL);
	keymod_set(ALT, KB_ALT);
	keymod_set(SHIFT, KB_SHIFT);
	keymod_set(LOGO, KB_WIN);
}
#undef keymod_set

static int
process_keyboard_event(struct amcs_compositor *ctx, struct libinput_event *ev)
{
	struct libinput_event_keyboard *k;
	struct amcs_client *client;
	struct amcs_workspace *ws;
	struct amcs_surface *surf;
	struct amcs_key_info ki = {0};
	struct wl_array arr;
	uint32_t serial, time, key;
	uint32_t state;
	xkb_keysym_t keysym;

	/* for debugging purposes only
	if (ctx->isactive == false)
		return 0;
	*/

	k = libinput_event_get_keyboard_event(ev);
	time = libinput_event_keyboard_get_time(k);
	key = libinput_event_keyboard_get_key(k);
	state = libinput_event_keyboard_get_key_state(k);
	xkb_state_update_key(ctx->seat->kbstate, XKB_KEY(key), XKB_STATE(state));
	keysym = xkb_state_key_get_one_sym(ctx->seat->kbstate, XKB_KEY(key));

	client = amcs_current_client();
	debug("focused client = %p", client);

	ki_state_init(&ki, ctx->seat, keysym, state);
	if (amcs_compositor_handle_key(ctx, &ki))
		return 0;

	if (client == NULL || client->keyboard == NULL) {
		warning("can't send keyboard event, no client keyboard connection");
		return 1;
	}

	ws = pvector_get(&ctx->workspaces,
			ctx->cur_workspace);
	if (ws == NULL || ws->current == NULL)
		return 1;

	serial = wl_display_next_serial(ctx->display);
	if (ws->current != NULL && ws->current->opaq != NULL) {
		debug("send enter");
		surf = amcs_win_get_opaq(ws->current);
		assert(surf && "can't get amcs_surface from amcs_win");

		wl_array_init(&arr);
		wl_keyboard_send_enter(client->keyboard,
			serial, surf->res, &arr);
		wl_array_release(&arr);

		wl_keyboard_send_modifiers(client->keyboard,
			wl_display_next_serial(ctx->display),
			ki.mods.depressed, ki.mods.latched, ki.mods.locked, 0);
		wl_keyboard_send_key(client->keyboard,
			wl_display_next_serial(ctx->display),
			time, key, state);

		if (surf->redraw_cb)
			wl_callback_send_done(surf->redraw_cb, get_time());
	}

	debug("send (time, key, state) (%d, %d, %d)", time, key, state);
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
	struct amcs_seat *seat = compositor_ctx.seat;
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
	wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, 0, 0);
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
	debug("");
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
seat_finalize(struct amcs_compositor *ctx)
{
	seat_free(compositor_ctx.seat);
	if (ctx->seat)
		wl_global_destroy(ctx->g.seat);
	return 0;

}
