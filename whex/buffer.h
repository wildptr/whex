#define N_CACHE_BLOCK 16
#define LOG2_CACHE_BLOCK_SIZE 12
#define CACHE_BLOCK_SIZE (1 << LOG2_CACHE_BLOCK_SIZE)

typedef long long offset;

struct cache_entry {
	offset addr;
	uchar *data;
	uchar flags;
};

typedef struct tree Tree;
typedef struct segment Segment;

typedef struct {
	HANDLE file;
	offset file_size;
	offset buffer_size;
	Segment *firstseg;
	struct cache_entry *cache;
	uchar *cache_data;
	Tree *tree;
	Region tree_rgn;
	int next_cache;
} Buffer;

int buf_init(Buffer *, HANDLE);
void buf_finalize(Buffer *);
//uchar *buf_get_data(Buffer *, offset);
void buf_read(Buffer *, uchar *, offset, size_t);
uchar buf_getbyte(Buffer *, offset);
//void buf_setbyte(Buffer *, offset, uchar);
int buf_save(Buffer *, HANDLE);
void buf_replace(Buffer *, offset, const uchar *, size_t);
void buf_insert(Buffer *, offset, const uchar *, size_t);
