#include "util.h"

List *
cons(Region *r, void *data, List *next)
{
	List *p;
	NEW(p, r);
	p->data = data;
	p->next = next;
	return p;
}

int
len(List *l)
{
	int n = 0;
	while (l) {
		l = l->next;
		n++;
	}
	return n;
}
