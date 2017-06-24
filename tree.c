#include <stdio.h>
#include <stdlib.h>

#include "tree.h"

void tree_free(struct tree *tree)
{
	free(tree->name);
	for (int i=0; i<tree->n_child; i++) {
		tree_free(tree->children[i]);
	}
	free(tree->children);
	free(tree);
}

static void print_rec(struct tree *tree, int depth)
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

void tree_print(struct tree *tree)
{
	print_rec(tree, 0);
}

struct tree *tree_lookup(struct tree *tree, long long addr)
{
	if (addr < tree->start || addr >= tree->start + tree->len) {
		// out of bounds
		return 0;
	}
	if (tree->n_child <= 0) {
		// leaf node reached
		return tree;
	}
	for (int i=0; i<tree->n_child; i++) {
		struct tree *result = tree_lookup(tree->children[i], addr);
		if (result) return result;
	}
	// `addr` falls within gap
	return 0;
}
