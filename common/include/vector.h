#ifndef _VECTOR_
#define _VECTOR_

#include <stdlib.h>
#include <string.h>

#define VECTOR_START_MAXNMEMB	8
#define VECTOR_MAXNMEMB_GROWTH	1.4

enum {
	VECTOR_OK = 0,
	VECTOR_MEM_ERR = 1
};

typedef struct {
	size_t nmemb;
	size_t size;
	size_t maxnmemb;
	void *data;
	void *(*realloc)(void *, size_t);
} vector;

/*
 * Pointer storage type
 */
typedef struct {
	vector v;
} pvector;

void vector_initdata(vector *v, void *arr, size_t nmemb, size_t size,
		void *(*real)(void *, size_t));
int vector_init(vector *v, size_t size, void *(*real)(void *, size_t));
void vector_free(vector *v);

size_t vector_len(const vector *v);
void *vector_data(const vector *v);

//reserve and null space
int vector_reserve(vector *v, int nmemb);
void vector_set(vector *v, size_t idx, const void *val);
void *vector_get(const vector *v, size_t idx);

int vector_push(vector *v, const void *val);
void vector_pop(vector *v);

int vector_add(vector *v, size_t idx, const void *val);
void vector_del(vector *v, size_t idx);
void vector_clear(vector *v);

// Pointer vector
static inline void pvector_init(pvector *v, void *(*real)(void *, size_t))
{
	vector_init(&v->v, sizeof(void *), real);
}

static inline void pvector_free(pvector *v)
{
	vector_free(&v->v);
}

static inline void pvector_initdata(pvector *v, void *arr, size_t nmemb, void *(*real)(void *, size_t))
{
	vector_initdata(&v->v, arr, nmemb, sizeof(void *), real);
}

static inline size_t pvector_len(const pvector *v)
{
	return v->v.nmemb;
}

static inline void *pvector_data(const pvector *v)
{
	return v->v.data;
}

static inline int pvector_reserve(pvector *v, int nmemb)
{
	return vector_reserve(&v->v, nmemb);
}

static inline void pvector_set(pvector *v, size_t idx, const void *val)
{
	((void **)v->v.data)[idx] = (void *)val;
}
static inline void *pvector_get(const pvector *v, size_t idx)
{
	return ((void **)v->v.data)[idx];
}

static inline int pvector_push(pvector *v, const void *val)
{
	return vector_push(&v->v, &val);
}

static inline void pvector_pop(pvector *v)
{
	vector_pop(&v->v);
}

static inline int pvector_add(pvector *v, size_t idx, const void *val)
{
	return vector_add(&v->v, idx, &val);
}

static inline void pvector_del(pvector *v, size_t idx)
{
	vector_del(&v->v, idx);
}

static inline void pvector_clear(pvector *v)
{
	v->v.nmemb = 0;
}

static inline void pvector_zero(pvector *v)
{
	memset(v->v.data, 0, sizeof(void*) * v->v.maxnmemb);
}

#define pvector_for_each(pos, iterator, vector)			\
	if (pvector_len(vector) > 0)				\
		for (pos = 0, iterator = pvector_get(vector, 0);\
		     pos < pvector_len(vector);			\
		     pos++, iterator = pvector_get(vector, pos))

#endif

