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

struct tree *tree_lookup(struct tree *tree, long long addr, char *path, int path_len)
{
	if (addr < tree->start || addr >= tree->start + tree->len) {
		// out of bounds
		return 0;
	}
	if (tree->n_child <= 0) {
		snprintf(path, path_len, "%s", tree->name);
		// leaf node reached
		return tree;
	}
	for (int i=0; i<tree->n_child; i++) {
		int nc;
		snprintf(path, path_len, "%s.%n", tree->name, &nc);
		struct tree *result = tree_lookup(tree->children[i], addr, path+nc, path_len-nc);
		if (result) return result;
	}
	// `addr` falls within gap
	return 0;
}
