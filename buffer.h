#define N_CACHE_BLOCK 16
#define LOG2_CACHE_BLOCK_SIZE 12
#define CACHE_BLOCK_SIZE (1 << LOG2_CACHE_BLOCK_SIZE)

struct cache_entry {
	long long addr;
	uint8_t *data;
	uint8_t flags;
};

struct tree;

typedef struct {
	HANDLE file;
	long long file_size;
	struct cache_entry *cache;
	uint8_t *cache_data;
	struct tree *tree;
	Region tree_rgn;
	int next_cache;
} Buffer;

int buf_init(Buffer *, HANDLE);
void buf_finalize(Buffer *);
uint8_t *buf_get_data(Buffer *, long long);
uint8_t buf_getbyte(Buffer *, long long);
int buf_save(Buffer *);
