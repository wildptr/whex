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
int length(List *);
