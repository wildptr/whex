struct arena {
	char *cur;
	char *limit;
	char *head;
};

void *arena_alloc(struct arena *arena, int size);
void arena_init(struct arena *arena);
void arena_reset(struct arena *arena);
