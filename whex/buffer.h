#define N_CACHE_BLOCK 16
#define LOG2_CACHE_BLOCK_SIZE 12
#define CACHE_BLOCK_SIZE (1 << LOG2_CACHE_BLOCK_SIZE)

struct cache_entry {
	uint64 addr;
	uchar *data;
	uchar flags;
};

typedef struct tree Tree;
typedef struct segment Segment;

typedef struct {
	HANDLE file;
	uint64 file_size;
	uint64 buffer_size;
	Segment *firstseg;
	struct cache_entry *cache;
	uchar *cache_data;
	Tree *tree;
	Region tree_rgn;
	int next_cache;
} Buffer;

int buf_init(Buffer *, HANDLE);
void buf_finalize(Buffer *);
void buf_read(Buffer *, uchar *, uint64, size_t);
uchar buf_getbyte(Buffer *, uint64);
int buf_save(Buffer *, HANDLE);
void buf_replace(Buffer *, uint64, const uchar *, size_t);
void buf_insert(Buffer *, uint64, const uchar *, size_t);
