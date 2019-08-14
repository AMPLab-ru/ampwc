#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <wayland-util.h>
#include <wayland-server.h>

#include "drm.h"
#include "macro.h"
#include "output.h"
#include "window.h"

#define DEFAULT_WINSZ 1024

struct amcs_container *amcs_container_new(struct amcs_container *par, enum container_type t);
void amcs_container_free(struct amcs_container *wt);

typedef int (*container_pass_cb)(struct amcs_win *w, void *opaq);
int amcs_container_pass(struct amcs_container *wt, container_pass_cb cb, void *data);

/*
 * Insert window into specified position
 * Thrid argument may be -1, for appending at the end of *wt*
 */
int amcs_container_insert(struct amcs_container *wt, struct amcs_win *w, int pos);
int amcs_container_remove(struct amcs_container *wt, struct amcs_win *w);
void amcs_container_remove_all(struct amcs_container *wt);
int amcs_container_remove_idx(struct amcs_container *wt, int pos);
int amcs_container_pos(struct amcs_container *wt, struct amcs_win *w);
int amcs_container_resize_subwins(struct amcs_container *wt);
int amcs_container_nmemb(struct amcs_container *wt);

// create new window, associate with window another object (*opaq*)
struct amcs_win *amcs_win_new(struct amcs_container *par, void *opaq,
		win_update_cb upd);
void amcs_win_free(struct amcs_win *w);

static struct amcs_container *
win_get_root(struct amcs_win *w)
{
	struct amcs_container *wt = AMCS_CONTAINER(w);

	assert(w);
	while (wt->parent)
		wt = wt->parent;
	return wt;
}

static inline struct amcs_workspace *
win_get_workspace(struct amcs_win *w)
{
	struct amcs_container *r;
	r = win_get_root(w);
	assert(r && r->ws && "can't get workspace from container");
	return r->ws;
}

/* search nearby amcs_win recursively */
static struct amcs_win *
win_get_neighbour_win(struct amcs_win *w, enum ws_lookup_dir direction)
{
	int pos, n;
	int diff = 0;

	if (w->parent == NULL)
		return NULL;

	if (direction == WS_DOWN || direction == WS_RIGHT)
		diff = 1;
	else if (direction == WS_UP || direction == WS_LEFT)
		diff = -1;

	for (;w != NULL && w->parent != NULL;
	    w = AMCS_WIN(w->parent)) {
		n = amcs_container_nmemb(w->parent);
		if (n == 1)
			continue;
		if ((direction == WS_UP || direction == WS_DOWN) && w->parent->wt == CONTAINER_VSPLIT)
			continue;
		if ((direction == WS_LEFT || direction == WS_RIGHT) && w->parent->wt == CONTAINER_HSPLIT)
			continue;
		debug("neightbour_win, after checks");
		pos = amcs_container_pos(w->parent, w);
		if ((pos == 0 && diff == -1) ||
		    (pos == n - 1 && diff == 1))
			continue;
		if (direction == WS_ANY) {
			// special case we can choose any valid direction
			diff = 1;
			if (pos == n - 1)
				diff = -1;
		}
		w = pvector_get(&w->parent->subwins, pos + diff);
		while (w->type == WT_TREE) {
			struct amcs_container *c;
			c = AMCS_CONTAINER(w);
			assert(pvector_len(&c->subwins) > 0 && "get_neighbour error, should not happen");
			w = pvector_get(&c->subwins, 0);
		}
		return w;
	}
	return NULL;
}

// Public functions

struct amcs_workspace *
amcs_workspace_new(const char *nm)
{
	struct amcs_workspace *res;

	res = xmalloc(sizeof(*res));
	memset(res, 0, sizeof(*res));
	res->type = WT_WORKSPACE;
	res->x = res->y = 0;
	res->w = res->h = DEFAULT_WINSZ;
	res->root = amcs_container_new(NULL, CONTAINER_VSPLIT);
	res->root->ws = res;
	res->name = strdup(nm);
	return res;
}

void
amcs_workspace_free(struct amcs_workspace *res)
{
	//TODO: maybe we need to remove windows?
	if (res->root)
		amcs_container_free(res->root);
	free(res);
}

//TODO: multimonitor support
void
amcs_workspace_set_output(struct amcs_workspace *ws, struct amcs_output *out)
{
	bool needreload = false;
	assert(ws && ws->type == WT_WORKSPACE && out);
	if (ws->out != out) {
		ws->out = out;
		needreload = true;
	}
	if (ws->out == NULL) {
		ws->x = ws->y = 0;
		ws->w = ws->h = DEFAULT_WINSZ;
		return;
	}
	if (ws->w != out->w ||
	    ws->h != out->h) {
		needreload = true;
		ws->x = ws->y = 0;
		ws->w = out->w;
		ws->h = out->h;
	}

	debug("ws (%d, %d) out (%d, %d) needreload %d", ws->w, ws->h,
			out->w, out->h, needreload);

	if (needreload) {
		ws->root->x = ws->x;
		ws->root->y = ws->y;
		ws->root->h = ws->h;
		ws->root->w = ws->w;
	}
}

void
amcs_workspace_focus_next(struct amcs_workspace *ws,
		enum ws_lookup_dir direction)
{
	struct amcs_win *w;

	assert(ws->root);
	debug("");
	if (ws->current == NULL && amcs_container_nmemb(ws->root) > 0)
		error(3, "window.c programmer error");
	if (ws->current == NULL)
		return;
	w = win_get_neighbour_win(ws->current, direction);
	if (w) {
		debug("change current focus to %p", w);
		ws->current = w;
	}
}

void
amcs_workspace_win_move(struct amcs_workspace *ws, enum ws_lookup_dir direction)
{
	struct amcs_win *w;
	struct amcs_container *oldroot;
	int pos;

	assert(ws->root);
	debug("");
	if (ws->current == NULL && amcs_container_nmemb(ws->root) > 0)
		error(3, "window.c programmer error");
	if (ws->current == NULL)
		return;
	w = win_get_neighbour_win(ws->current, direction);
	if (w == NULL)
		return;
	//TODO: update window only after move
	oldroot = ws->current->parent;
	pos = amcs_container_pos(w->parent, w);
	amcs_container_remove(oldroot, ws->current);
	amcs_container_insert(w->parent, ws->current, pos);
	while (amcs_container_nmemb(oldroot) == 0 && oldroot->parent) {
		struct amcs_container *tmp = oldroot;
		oldroot = oldroot->parent;
		amcs_container_remove(oldroot, AMCS_WIN(tmp));
	}
	amcs_container_resize_subwins(w->parent);
	amcs_container_resize_subwins(oldroot);
}

void amcs_workspace_redraw(struct amcs_workspace *ws)
{
	assert(ws && ws->root && ws->out);
	amcs_output_clear(ws->out);
	amcs_container_resize_subwins(ws->root);
}

struct amcs_win *
amcs_workspace_new_win(struct amcs_workspace  *ws, void *opaq,
		win_update_cb upd)
{
	struct amcs_container *r;
	struct amcs_win *res;
	if (ws->current)
		r = ws->current->parent;
	else
		r = ws->root;
	assert(r && "can't get temporary root container");
	res = amcs_win_new(r, opaq, upd);
	amcs_container_resize_subwins(r);
	debug("===== win %p root %p", res, r);
	ws->current = res;
	return res;
}

int amcs_workspace_split(struct amcs_workspace *ws)
{
	struct amcs_container *par, *c;
	int pos, type;

	if (ws->current == NULL) {
		ws->root->wt = ws->root->wt == CONTAINER_VSPLIT ?
			CONTAINER_HSPLIT : CONTAINER_VSPLIT;
		return 0;
	}
	par = ws->current->parent;
	type = par->wt == CONTAINER_VSPLIT ?
		CONTAINER_HSPLIT : CONTAINER_VSPLIT;
	if (amcs_container_nmemb(par) < 2) {
		par->wt = type;
		return 0;
	}
	c = amcs_container_new(NULL, type);
	pos = amcs_container_pos(par, ws->current);
	amcs_container_remove_idx(par, pos);
	amcs_container_insert(c, ws->current, 0);
	amcs_container_insert(par, AMCS_WIN(c), pos);
	amcs_container_resize_subwins(par);
	return 0;
}

struct amcs_container *
amcs_container_new(struct amcs_container *par, enum container_type t)
{
	struct amcs_container *res;

	res = xmalloc(sizeof(*res));
	memset(res, 0, sizeof(*res));
	res->type = WT_TREE;
	res->wt = t;
	res->parent = par;
	pvector_init(&res->subwins, xrealloc);

	return res;
}

void
amcs_container_free(struct amcs_container *wt)
{
	assert(wt && wt->type == WT_TREE);
	amcs_container_remove_all(wt);
	free(wt);
}

int
amcs_container_pass(struct amcs_container *wt, container_pass_cb cb, void *data)
{
	struct amcs_win **arr;
	int i, rc;

	if (wt == NULL)
		return 0;
	debug("pass");

	rc = cb(AMCS_WIN(wt), data);

	if (wt->type == WT_WIN)
		return rc;
	arr = pvector_data(&wt->subwins);
	for (i = 0; i < pvector_len(&wt->subwins); i++) {
		if (arr[i]->type == WT_WIN)
			rc = cb(arr[i], data);
		else if (arr[i]->type == WT_TREE)
			rc = amcs_container_pass(AMCS_CONTAINER(arr[i]), cb, data);
		else
			error(2, "should not reach");

		if (rc != 0)
			return rc;
	}
	return 0;
}

int
amcs_container_insert(struct amcs_container *wt, struct amcs_win *w, int pos)
{
	assert(wt && (wt->type == WT_TREE || w->type == WT_WIN));

	debug("insert %p, into %p nsubwinds %zd", w, wt, pvector_len(&wt->subwins));
	if (pos == -1 || pos > pvector_len(&wt->subwins))
		pos = pvector_len(&wt->subwins);
	pvector_add(&wt->subwins, pos, w);
	w->parent = wt;
	return 0;
}

int
amcs_container_pos(struct amcs_container *wt, struct amcs_win *w)
{
	struct amcs_win **arr;
	int i;

	assert(wt && wt->type == WT_TREE);

	arr = pvector_data(&wt->subwins);
	for (i = 0; i < pvector_len(&wt->subwins); i++) {
		if (arr[i] == w)
			return i;
	}
	return -1;
}

int
amcs_container_remove(struct amcs_container *wt, struct amcs_win *w)
{
	int pos;
	assert(wt && wt->type == WT_TREE);

	pos = amcs_container_pos(wt, w);
	if (pos >= 0) {
		struct amcs_win *w;
		w = pvector_get(&wt->subwins, pos);
		w->parent = NULL;

		pvector_del(&wt->subwins, pos);
		amcs_container_resize_subwins(wt);
		return 0;
	}
	error(2, "should not happen, programmer error");
	return 1;
}

int
amcs_container_remove_idx(struct amcs_container *wt, int pos)
{
	assert(wt && wt->type == WT_TREE);

	pvector_del(&wt->subwins, pos);
	amcs_container_resize_subwins(wt);
	return 0;
}

void
amcs_container_remove_all(struct amcs_container *wt)
{
	int i;
	assert(wt && wt->type == WT_TREE);

	for (i = 0; i < pvector_len(&wt->subwins); i++) {
		pvector_pop(&wt->subwins);
	}
	amcs_container_resize_subwins(wt);
}

struct amcs_win *
amcs_win_new(struct amcs_container *par, void *opaq, win_update_cb upd)
{
	struct amcs_win *res;

	debug("win_new, %p", par);
	res = xmalloc(sizeof(*res));
	memset(res, 0, sizeof(*res));
	res->type = WT_WIN;
	res->opaq = opaq;
	res->upd_cb = upd;
	if (par)
		amcs_container_insert(par, res, -1);
	return res;
}

void
amcs_win_free(struct amcs_win *w)
{
	assert(w && w->type == WT_WIN);
	amcs_win_orphain(w);
	if (w->buf.dt)
		free(w->buf.dt);
	free(w);
}

static int
_commit_cb(struct amcs_win *w, void *opaq)
{
	int rc;
	debug("commit cb");
	if (w && w->type == WT_WIN && w->buf.dt) {
		amcs_win_commit(w);
		if (w->upd_cb) {
			rc = w->upd_cb(w, w->opaq);
			assert(rc == 0 || "TODO: writeme");
		}
	}
	return 0;
}

int
amcs_container_resize_subwins(struct amcs_container *wt)
{
	int i, nwin, step;

	if (wt == NULL || wt->type == WT_WIN)
		return 0;

	nwin = pvector_len(&wt->subwins);
	if (nwin == 0) {
		struct amcs_workspace *ws;
		ws = win_get_workspace(AMCS_WIN(wt));
		// Special case, clear buffer if last workspace window is deleted
		if (wt == ws->root)
			amcs_output_clear(ws->out);
		return 0;
	}

	if (wt->wt == CONTAINER_HSPLIT) {
		step = wt->h / nwin;
	} else {
		step = wt->w / nwin;
	}

	assert(step); // is that really happens?

	for (i = 0; i < nwin; i++) {
		struct amcs_win *tmp;
		tmp = pvector_get(&wt->subwins, i);

		if (wt->wt == CONTAINER_HSPLIT) {
			tmp->x = wt->x;
			tmp->y = wt->y + i * step;
			tmp->w = wt->w;
			tmp->h = step;
		} else {
			tmp->x = wt->x + i * step;
			tmp->y = wt->y;
			tmp->w = step;
			tmp->h = wt->h;
		}
		if (tmp->type == WT_TREE) {
			amcs_container_resize_subwins(AMCS_CONTAINER(tmp));
		}
	}
	amcs_container_pass(wt, _commit_cb, NULL);
	return 0;
}

int
amcs_container_nmemb(struct amcs_container *wt)
{
	assert(wt);
	return pvector_len(&wt->subwins);
}

int
amcs_win_commit(struct amcs_win *win)
{
	struct amcs_workspace *ws;

	ws = win_get_workspace(win);
	debug("get workspace %p", ws);
	if (ws->out == NULL)
		return -1;
	return amcs_output_update_region(ws->out, win);
}

int
amcs_win_orphain(struct amcs_win *w)
{
	struct amcs_container *par;
	struct amcs_workspace *ws;

	assert(w);
	if (w->parent == NULL)
		return 1;
	par = w->parent;
	ws = win_get_workspace(w);
	if (ws->current == w) {
		ws->current = win_get_neighbour_win(w, WS_ANY);
	}
	amcs_container_remove(par, w);
	while (amcs_container_nmemb(par) == 0 && par->parent) {
		struct amcs_container *tmp = par;
		par = par->parent;
		amcs_container_remove(par, AMCS_WIN(tmp));
	}

	return 0;
}

static int
_print_cb(struct amcs_win *w, void *opaq)
{
	debug("tp = %s, x = %d y = %d"
	      "         w = %d h = %d", w->type == WT_WIN ? "wt_win" : "wt_wtree",
			w->x, w->y, w->w, w->h);
	return 0;
}

void
amcs_workspace_debug(struct amcs_workspace *ws)
{
	if (ws->root == NULL)
		return;
	amcs_container_pass(ws->root, _print_cb, NULL);
}

