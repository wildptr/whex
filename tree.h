enum field_type {
    F_RAW,
    F_UINT,
    F_INT,
    F_ASCII,
    F_CUSTOM,
};

typedef struct tree {
    uint64 start;
    uint64 len;
    char *name;
    long intvalue;
    int n_child;
    struct tree **children;
    enum field_type type;
    struct tree *parent;
    char *custom_type_name;
} Tree;

void tree_print(Tree *);
struct tree *tree_lookup(Tree *, uint64 addr);
char *tree_path(Region *, Tree *);
