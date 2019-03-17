#include "u.h"

void *
xmalloc(size_t size)
{
	void *p = malloc(size);
	if (!p) {
		fputs("out of memory\n", stderr);
		abort();
	}
	return p;
}

void *
xrealloc(void *ptr, size_t size)
{
	void *p = realloc(ptr, size);
	if (!p) {
		fputs("out of memory\n", stderr);
		abort();
	}
	return p;
}

/* region-based memory management */

#define CHUNK_SIZE 0x1000
#define ALIGN(x,a) (((x)+(a)-1)&(-(a)))

typedef struct chunk {
	struct chunk *next;
	void *limit;
	char data[];
} Chunk;

static void
new_chunk(Region *r, int data_size)
{
	int minsize = sizeof(Chunk) + data_size;
	int malloc_size = minsize < CHUNK_SIZE ? CHUNK_SIZE : minsize;
	Chunk *c;
	assert(malloc_size >= 0);
	c = xmalloc(malloc_size);
	c->next = r->head;
	c->limit = (char *) c + malloc_size;
	r->head = c;
	r->cur = c->data;
	r->limit = c->limit;
}

void *
ralloc(Region *r, int size)
{
	void *ret;
	// align
	size = ALIGN(size, sizeof(void *));
	if (r->cur + size > r->limit) {
		new_chunk(r, size);
	}
	ret = r->cur;
	r->cur += size;
	return ret;
}

void
rinit(Region *r)
{
	r->head = 0;
	new_chunk(r, 0);
}

void
rfreeall(Region *r)
{
	Chunk *c = r->head;
	while (c) {
		Chunk *nextc = c->next;
		free(c);
		c = nextc;
	}
	r->cur = 0;
	r->limit = 0;
	r->head = 0;
}

void
rfree(Region *r, void *p)
{
	Chunk *c = r->head;
	while (!(p >= (void *) c && p <= c->limit)) {
		Chunk *nextc = c->next;
		assert(nextc);
		free(c);
		c = nextc;
	}
	r->cur = p;
	r->limit = c->limit;
	r->head = c;
}

/* common definitions for both versions of printf */

#define INIT_BUFSIZE 32

typedef void (*Formatter)();

enum {
	F_SIGNED = 1,
	F_UPPERCASE = 2,
	F_PRINTSIGN = 4,
	F_PLUS_SIGN = 8,
	F_HAVE_PREC = 16,
};

union u {
	int i;
	long l;
	INT64 L;
};

/* printf */

#define T_(x) x
#define _TCHAR char
#include "printf.c"
#undef T_
#undef _TCHAR
#ifdef UNICODE
#define strlen_w wcslen
#define T_(x) x##_w
#define _TCHAR wchar_t
#include "printf.c"
#undef T_
#undef _TCHAR
#endif
