enum field_type {
	F_RAW,
	F_UINT,
	F_INT,
	F_ASCII,
	F_CUSTOM,
};

typedef struct tree {
	long long start;
	long long len;
	char *name;
	int intvalue;
	int n_child;
	struct tree **children;
	enum field_type type;
	struct tree *parent;
	char *custom_type_name;
} Tree;

void tree_print(Tree *);
struct tree *tree_lookup(Tree *, long long addr);
char *tree_path(Region *, Tree *);
