#ifndef WL_SERVER_H_
#define WL_SERVER_H_

#include "window.h"
#include "vector.h"

#define NWORKSPACES 9

struct amcs_client {
	struct wl_client *client;
	struct wl_resource *output;
	struct wl_resource *seat;

	// seat resources
	struct wl_resource *keyboard;
	struct wl_resource *pointer;

	struct wl_list link;
};

struct amcs_surface {
	struct wl_resource *res;
	struct wl_resource *xdgres;
	struct wl_resource *xdgtopres;

	const char *app_id;
	const char *title;

	int w;
	int h;

	struct amcs_win *aw;
	struct {
		struct wl_shm_buffer *buf;
		int x;
		int y;
		int upd_source;
		int xdg_serial;
	} pending;
	struct wl_array surf_states;
	struct wl_resource *redraw_cb;	//client callback for surface redraw

	struct wl_list link;
};

struct global_resources {
	struct wl_global *comp;
	struct wl_global *shell;
	struct wl_global *seat;
	struct wl_global *devman;
	struct wl_global *output;
};

struct amcs_compositor {
	struct global_resources g;
	bool isactive;

	struct wl_display *display;
	struct wl_event_loop *evloop;

	struct renderer *renderer;
	struct amcs_seat *seat;
	//struct drmdev dev;

	struct wl_list clients;
	struct wl_list surfaces;

	pvector workspaces;		//struct amcs_workspace *
	struct amcs_output *output;
	int cur_workspace;

	struct wl_listener redraw_listener;
	struct wl_signal redraw_sig;
};

extern struct amcs_compositor compositor_ctx;

int   amcs_compositor_init    (struct amcs_compositor *ctx);
void  amcs_compositor_deinit  (struct amcs_compositor *ctx);

struct amcs_key_info;
bool  amcs_compositor_handle_key(struct amcs_compositor *ctx, struct amcs_key_info *ki);

//get amcs_client from any valid child resource
struct amcs_client *amcs_get_client(struct wl_resource *res);

struct amcs_win *amcs_current_window();
struct amcs_client *amcs_current_client();

#endif
