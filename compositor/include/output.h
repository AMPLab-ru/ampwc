#ifndef _OUTPUT_H_FLURUJBH
#define _OUTPUT_H_FLURUJBH

#include <stdbool.h>

#include "amcs_drm.h"
#include "vector.h"
//TODO: refactor amcs_output and amcs_win relation
#include "window.h"

struct amcs_output {
	int w, h;
	bool isactive;
	pvector cards;
	pvector screens; //struct amcs_screen *
};

struct amcs_screen {
	int x, y;		//relative screen offset
	int w, h;
	int pitch;
	uint8_t *buf;
};

struct amcs_output *amcs_output_new();
void amcs_output_free(struct amcs_output *out);

//call it when user switches to acquired tty
int amcs_output_reload(struct amcs_output *out);

//call it when user leaves acquired tty
int amcs_output_release(struct amcs_output *out);

//send updated info to wl_output object
void amcs_output_send_info(struct amcs_output *out, struct wl_resource *resource);
int amcs_output_update_region(struct amcs_output *out, struct amcs_win *w);

struct amcs_compositor;
int output_init(struct amcs_compositor *ctx);
void output_finalize(struct amcs_compositor *ctx);

#endif
