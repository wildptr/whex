#define N_CACHE_BLOCK 16
#define LOG2_CACHE_BLOCK_SIZE 12
#define CACHE_BLOCK_SIZE (1 << LOG2_CACHE_BLOCK_SIZE)

struct cache_entry {
	long long tag;
	uint8_t *data;
};

struct tree;

typedef struct {
	HANDLE file;
	long long file_size;
	struct cache_entry *cache;
	uint8_t *cache_data;
	struct tree *tree;
	Region tree_rgn;
} Buffer;

int buf_init(Buffer *, HANDLE);
void buf_finalize(Buffer *);
int buf_find_cache(Buffer *, long long);
uint8_t buf_getbyte(Buffer *, long long);
