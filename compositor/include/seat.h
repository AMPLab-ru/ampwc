#ifndef SEAT_V8UZNNQ
#define SEAT_V8UZNNQ

#include <libinput.h>
#include <libudev.h>
#include <xkbcommon/xkbcommon.h>

#include "wl-server.h"

enum KB_MODS {
	KB_NONE = 0,
	KB_SHIFT = 1,
	KB_CTRL = 2,
	KB_ALT = 4,
	KB_WIN = 8,
};

struct keyboard_modifiers_state {
	uint32_t depressed;
	uint32_t latched;
	uint32_t locked;
	uint32_t group;
};

struct amcs_key_info {
	uint32_t keysym;
	uint32_t state;
	uint32_t modifiers;
	struct keyboard_modifiers_state mods;
};

struct amcs_seat {
	struct libinput *input;
	struct udev *udev;

	struct xkb_context *xkb;
	struct xkb_keymap *keymap;
	struct xkb_state *kbstate;

	int ifd;			// libinput file descriptor
	int capabilities;
};

int seat_init(struct amcs_compositor *ctx);
int seat_finalize(struct amcs_compositor *ctx);

#endif
