#ifdef DEBUG
#define DEBUG_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__);
#else
#define DEBUG_PRINTF(...) ((void)0)
#endif

#define CHUNK_SIZE 0x1000
#define PTR_SIZE sizeof(void*)

typedef struct chunk {
	struct chunk *next;
	char data[CHUNK_SIZE-PTR_SIZE];
} Chunk;

typedef struct {
	char *cur, *limit;
	Chunk *head;
} Region;

void rinit(Region *);
void *ralloc(Region *, int);
void *ralloc0(Region *, int);
void rfreeall(Region *);
void rfree(Region *, void *);

#define NEW(p, r) p = ralloc(r, sizeof *p)
#define NEW0(p, r) p = ralloc0(r, sizeof *p)

typedef struct list {
	void *data;
	struct list *next;
} List;

#define APPEND(LAST, DATA, RGN)\
	do {\
		List *_node = cons(RGN, DATA, 0);\
		LAST->next = _node;\
		LAST = _node;\
	} while (0)

#define FOREACH(LIST, VAR)\
	for (List *_node = LIST; _node && (VAR = _node->data, 1); _node = _node->next)

List *cons(Region *, void *data, List *next);
int len(List *);
