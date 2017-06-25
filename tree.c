#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#if 0
static struct tree *last_leaf(struct tree *tree)
{
	if (tree->n_child <= 0) return tree;
	return last_leaf(tree->children[tree->n_child-1]);
}

struct tree *tree_prev_leaf(struct tree *tree)
{
	struct tree *parent = tree->parent;
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

struct link_node {
	struct link_node *next;
	char *s;
	int buf_len;
};

static struct link_node *tree_path_rec(struct tree *tree, struct link_node *next)
{
	struct link_node *node = malloc(sizeof *node);
	node->next = next;
	node->s = tree->name;
	node->buf_len = strlen(tree->name) + 1 + (next ? next->buf_len : 0);
	if (tree->parent) {
		return tree_path_rec(tree->parent, node);
	}
	// tree->parent == NULL
	return node;
}

static void write_path(struct link_node *node, char *p)
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

char *tree_path(struct tree *tree)
{
	struct link_node *link = tree_path_rec(tree, 0);
	char *path = malloc(link->buf_len);
	write_path(link, path);
	// free the linked list
	do {
		struct link_node *next = link->next;
		free(link);
		link = next;
	} while (link);
	return path;
}
