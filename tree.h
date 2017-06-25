#ifndef _TREE_H_
#define _TREE_H_

enum field_type {
	F_RAW,
	F_UINT,
	F_INT,
	F_ASCII,
};

struct tree {
	long long start;
	long long len;
	char *name;
	int n_child;
	struct tree **children;
	enum field_type type;
	struct tree *parent;
};

void tree_free(struct tree *);
void tree_print(struct tree *);
struct tree *tree_lookup(struct tree *tree, long long addr);
char *tree_path(struct tree *tree);

#endif // _TREE_H_
