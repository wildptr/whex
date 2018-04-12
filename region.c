#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void *
ralloc(Region *r, int size)
{
	// align
	size = (size+PTR_SIZE-1)&(-PTR_SIZE);
	if (r->cur + size > r->limit) {
		int malloc_size =
			size > CHUNK_SIZE-PTR_SIZE ? PTR_SIZE+size : CHUNK_SIZE;
		assert(malloc_size >= 0);
		Chunk *c = malloc(malloc_size);
		if (!c) {
			fputs("out of memory\n", stderr);
			exit(1);
		}
		c->next = r->head;
		r->head = c;
		r->cur = c->data;
		r->limit = (char *) c + malloc_size;
	}
	void *ret = r->cur;
	r->cur += size;
	return ret;
}

void
rinit(Region *r)
{
	Chunk *c = malloc(sizeof *c);
	c->next = 0;
	r->cur = c->data;
	r->limit = c->data + sizeof c->data;
	r->head = c;
}

void *
ralloc0(Region *r, int size)
{
	assert(size >= 0);
	void *p = ralloc(r, size);
	memset(p, 0, size);
	return p;
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
	while (!(p >= (void *) c && p <= (void *)((char *) c + CHUNK_SIZE))) {
		Chunk *nextc = c->next;
		assert(nextc);
		free(c);
		c = nextc;
	}
	r->cur = p;
	r->limit = (char *) c + CHUNK_SIZE;
	r->head = c;
}
