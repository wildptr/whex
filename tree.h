#ifndef _TREE_H_
#define _TREE_H_

struct tree {
	long long start;
	long long len;
	char *name;
	int n_child;
	struct tree **children;
};

void tree_free(struct tree *);
void tree_print(struct tree *);
struct tree *tree_lookup(struct tree *tree, long long addr);

#endif // _TREE_H_
