#ifndef _AWC_WINDOWS_H
#define _AWC_WINDOWS_H

#include <stdint.h>

#include "vector.h"
/*
 * amcs_workspace -- single compositor workspace with it's own output position
 *   and window hierarchy
 * amcs_container -- container for child windows / splits.
 * root container has no parent
 */
struct amcs_workspace;
struct amcs_win;
struct amcs_container;

enum win_objtype {
	WT_TREE = 0,
	WT_WIN = 1,
	WT_WORKSPACE = 2,
};

struct amcs_workspace {
	enum win_objtype type;
	struct amcs_container *parent;	//unused
	// offsets for multi monitor systems
	int w, h;
	int x, y;

	struct amcs_container *root;
	struct amcs_win *current;
	struct amcs_output *out;
	char *name;
};

enum container_type {
	CONTAINER_HSPLIT = 0,
	CONTAINER_VSPLIT,
};

#define AMCS_CONTAINER(v) ((struct amcs_container *)v)
struct amcs_container {
	enum win_objtype type;
	struct amcs_container *parent;
	int w, h;
	int x, y;

	enum container_type wt;
	pvector subwins; // struct amcs_win *
	struct amcs_workspace *ws; // NOTE: Valid only for root wtree
};

struct amcs_buf {
	uint32_t *dt;
	int format;
	int h, w;
	int sz;
};

/* Notify callback for window resize */
typedef int (*win_update_cb)(struct amcs_win *w, void *opaq);
#define AMCS_WIN(v) ((struct amcs_win *)v)
struct amcs_win {
	enum win_objtype type;
	struct amcs_container *parent;

	int w, h;
	int x, y;

	struct amcs_buf buf;

	void *opaq;	//TODO: getter/setter ???
	win_update_cb upd_cb;
};

/* Workspace */
struct amcs_workspace *amcs_workspace_new(const char *name);
void amcs_workspace_free(struct amcs_workspace *w);
/* Handle resizing, repositioning and output structure changing. */
void amcs_workspace_set_output(struct amcs_workspace *ws, struct amcs_output *out);
// ????
void amcs_workspace_focus(struct amcs_workspace *ws);
void amcs_workspace_redraw(struct amcs_workspace *ws);
void amcs_workspace_debug(struct amcs_workspace *ws);

struct amcs_win *amcs_workspace_new_win(struct amcs_workspace *ws, void *opaq,
		win_update_cb upd);
int amcs_workspace_split(struct amcs_workspace * ws);


struct amcs_win *amcs_win_new(struct amcs_container *par, void *opaq, win_update_cb upd);
void amcs_win_free(struct amcs_win *w);
static inline void *amcs_win_get_opaq(struct amcs_win *w)
{
	assert(w);
	return w->opaq;
}
int amcs_win_commit(struct amcs_win *w);
//TODO: change current window, free empty containers (except root)
int amcs_win_orphain(struct amcs_win *w);
//int amcs_win_resize(struct amcs_win *w,);
#endif // _AWC_WINDOWS_H
