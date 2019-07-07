#ifndef SEAT_V8UZNNQ
#define SEAT_V8UZNNQ

#include <libinput.h>
#include <libudev.h>

#include "wl-server.h"

struct seat {
	struct libinput *input;
	struct udev *udev;
	int ifd;			// libinput file descriptor
	int capabilities;
	struct amcs_client *focus;
};

int seat_init(struct amcs_compositor *ctx);

/* Focus on client, that owns this resource. */
//TODO
int seat_focus(struct wl_resource *res);

int seat_finalize(struct amcs_compositor *ctx);

#endif
