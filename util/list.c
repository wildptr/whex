#include "region.h"
#include "list.h"

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
length(List *l)
{
	int n = 0;
	while (l) {
		l = l->next;
		n++;
	}
	return n;
}
