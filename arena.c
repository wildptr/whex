#include <stdlib.h>
#include "arena.h"

#define CHUNK_SIZE 4096

void *arena_alloc(struct arena *arena, int size)
{
	void *p;
	size = (size+3)&-4;
	if (arena->limit - arena->cur <= size) {
		int new_chunk_size = sizeof(char*) + size;
		if (new_chunk_size < CHUNK_SIZE) new_chunk_size = CHUNK_SIZE;
		char *new_chunk = malloc(new_chunk_size);
		*(char**)new_chunk = arena->head;
		arena->cur = new_chunk + sizeof(char*);
		arena->limit = new_chunk + CHUNK_SIZE;
		arena->head = new_chunk;
	}
	p = arena->cur;
	arena->cur += size;
	return p;
}

void arena_init(struct arena *arena)
{
	arena->cur = 0;
	arena->limit = 0;
	arena->head = 0;
}

void arena_reset(struct arena *arena)
{
	char *chunk = arena->head;
	while (chunk) {
		char *next_chunk = *(char**)chunk;
		free(chunk);
		chunk = next_chunk;
	}
	arena_init(arena);
}
