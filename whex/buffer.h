#define N_CACHE_BLOCK 16
#define LOG2_CACHE_BLOCK_SIZE 12
#define CACHE_BLOCK_SIZE (1 << LOG2_CACHE_BLOCK_SIZE)

typedef long long offset;

struct cache_entry {
	offset addr;
	uint8_t *data;
	uint8_t flags;
};

typedef struct tree Tree;
typedef struct segment Segment;

typedef struct {
	HANDLE file;
	offset file_size;
	offset buffer_size;
	Segment *firstseg;
	struct cache_entry *cache;
	uint8_t *cache_data;
	Tree *tree;
	Region tree_rgn;
	int next_cache;
} Buffer;

int buf_init(Buffer *, HANDLE);
void buf_finalize(Buffer *);
//uint8_t *buf_get_data(Buffer *, offset);
void buf_read(Buffer *, uint8_t *, offset, size_t);
uint8_t buf_getbyte(Buffer *, offset);
//void buf_setbyte(Buffer *, offset, uint8_t);
int buf_save(Buffer *);
void buf_replace(Buffer *, offset, const uint8_t *, size_t);
void buf_insert(Buffer *, offset, const uint8_t *, size_t);
