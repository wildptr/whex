struct chunk;

typedef struct {
	char *cur, *limit;
	struct chunk *head;
} Region;

void rinit(Region *);
void *ralloc(Region *, int);
void *ralloc0(Region *, int);
void rfreeall(Region *);
void rfree(Region *, void *);

#define NEW(p, r) p = ralloc(r, sizeof *p)
#define NEW0(p, r) p = ralloc0(r, sizeof *p)
