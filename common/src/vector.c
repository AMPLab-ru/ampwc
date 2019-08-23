#include <assert.h>

#include "vector.h"

int
vector_init(vector *v, size_t size, void *(*real)(void *, size_t))
{
	void *data;

	assert(v && size);

	if (!real)
		real = realloc;

	data = real(NULL, size * VECTOR_START_MAXNMEMB);

	if (!data)
		return VECTOR_MEM_ERR;

	v->nmemb = 0;
	v->maxnmemb = VECTOR_START_MAXNMEMB;
	v->size = size;
	v->data = data;
	v->realloc = real;

	return VECTOR_OK;
}

void
vector_free(vector *v)
{
	assert(v);

	if (v->data && v->realloc)
		v->realloc(v->data, 0);
}

void
vector_initdata(vector *v, void *arr, size_t nmemb, size_t size,
		void *(*real)(void *, size_t))
{
	assert(v && arr && nmemb && size);

	if (!(real))
		real = realloc;

	v->data = arr;
	v->nmemb = nmemb;
	v->maxnmemb = nmemb;
	v->size = size;
	v->realloc = real;
}

size_t
vector_len(const vector *v)
{
	assert(v);

	return v->nmemb;
}

void *
vector_data(const vector *v)
{
	assert(v && v->data);

	return v->data;
}

int
vector_reserve(vector *v, int nmemb)
{
	void *data;
	assert(v);
	if (nmemb <= v->maxnmemb) {
		int sz;
		sz = (v->maxnmemb - v->nmemb) * v->size;
		memset(v->data + v->nmemb * v->size, 0, sz);
		v->nmemb = nmemb;
		return VECTOR_OK;
	}

	data = v->realloc(v->data, nmemb * v->size);
	if (!data)
		return VECTOR_MEM_ERR;

	memset(data + v->maxnmemb * v->size, 0, (nmemb - v->maxnmemb) * v->size);
	v->data = data;
	v->nmemb = nmemb;
	v->maxnmemb = nmemb;
	return VECTOR_OK;
}

void
vector_set(vector *v, size_t idx, const void *val)
{
	void *p;

	assert(v && v->data && val);

	assert(idx < v->nmemb);

	p = (void *)((char *)v->data + idx * v->size);

	memcpy(p, val, v->size);
}

void *
vector_get(const vector *v, size_t idx)
{
	assert(v && v->data);

	assert(idx < v->nmemb);

	return (void *)((const char *)v->data + idx * v->size);
}

/* Add element to vector so: v[idx] = val */
int
vector_add(vector *v, size_t idx, const void *val)
{
	void *data;
	size_t nmemb;

	assert(v && v->data && val);
	assert(idx <= v->nmemb);

	if (v->nmemb == v->maxnmemb) {
		nmemb = (size_t)(v->maxnmemb * VECTOR_MAXNMEMB_GROWTH);
		data = v->realloc(v->data, nmemb * v->size);
		if (!data)
			return VECTOR_MEM_ERR;

		v->data = data;
		v->maxnmemb = nmemb;
	}

	v->nmemb++;
	if (v->nmemb - 1 > idx) {
		memmove(v->data + (idx + 1) * v->size,
			v->data + idx * v->size,
			(v->nmemb - idx - 1) * v->size);
	}
	#if 0
	// same as above
	int i;
	for (i = idx; i < v->nmemb - 1; i++)
		/* v[i] = v[i + 1] */
		vector_set(v, i + 1, vector_get(v, i));
	#endif

	vector_set(v, idx, val);

	return VECTOR_OK;
}

void
vector_del(vector *v, size_t idx)
{
	size_t i;

	assert(v && v->nmemb);
	assert(idx < v->nmemb);

	for (i = idx + 1; i < v->nmemb; i++)
		/* v[i - 1] = v[i] */
		vector_set(v, i - 1, vector_get(v, i));

	v->nmemb--;
}

void
vector_clear(vector *v)
{
	v->nmemb = 0;
}

int
vector_push(vector *v, const void *val)
{
	assert(v);

	return vector_add(v, v->nmemb, val);
}

void
vector_pop(vector *v)
{
	assert(v && v->nmemb);

	vector_del(v, v->nmemb - 1);
}

