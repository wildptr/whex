#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "tree.h"

static void
print_rec(Tree *tree, int depth)
{
	printf("%s: %I64d bytes", tree->name, tree->len);
	if (tree->n_child >= 0) {
		printf(" {\n");
		for (int i=0; i<tree->n_child; i++) {
			for (int j = depth; j; j--) {
				printf("  ");
			}
			printf("  %d => ", i);
			print_rec(tree->children[i], depth+1);
		}
		for (int j = depth; j; j--) {
			printf("  ");
		}
		printf("}\n");
	} else {
		putchar('\n');
	}
}

void
tree_print(Tree *tree)
{
	print_rec(tree, 0);
}

Tree *
tree_lookup(Tree *tree, long long addr)
{
	if (addr < tree->start || addr >= tree->start + tree->len) {
		// out of bounds
		return 0;
	}
	if (tree->n_child <= 0) {
		// leaf node reached
		return tree;
	}
	assert(tree->children);
	for (int i=0; i<tree->n_child; i++) {
		Tree *result = tree_lookup(tree->children[i], addr);
		if (result) return result;
	}
	// `addr` falls within gap
	return 0;
}

#if 0
static Tree *last_leaf(Tree *tree)
{
	if (tree->n_child <= 0) return tree;
	return last_leaf(tree->children[tree->n_child-1]);
}

Tree *tree_prev_leaf(Tree *tree)
{
	Tree *parent = tree->parent;
	if (!parent) return 0;
	for (int i=0; i<parent->n_child; i++) {
		if (tree == parent->children[i]) {
			if (i > 0) return last_leaf(parent->children[i-1]);
			return tree_prev_leaf(parent);
		}
	}
	// unreachable
	return 0;
}
#endif

typedef struct node {
	struct node *next;
	char *s;
	int len;
} Node;

static Node *
tree_path_rec(Region *r, Tree *tree, Node *next)
{
	Node *node = ralloc(r, sizeof *node);
	node->next = next;
	node->s = tree->name;
	node->len = strlen(tree->name) + 1 + (next ? next->len : 0);
	if (tree->parent) {
		return tree_path_rec(r, tree->parent, node);
	}
	// tree->parent == NULL
	return node;
}

static void
write_path(Node *node, char *p)
{
	char *q = node->s;
	while (*q) *p++ = *q++;
	if (node->next) {
		*p++ = '.';
		write_path(node->next, p);
	} else {
		*p = 0;
	}
}

char *
tree_path(Region *r, Tree *tree)
{
	Node *link = tree_path_rec(r, tree, 0);
	char *path = ralloc(r, link->len);
	write_path(link, path);
	return path;
}
