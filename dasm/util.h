typedef unsigned char	uchar;
typedef unsigned short	ushort;
typedef unsigned int	uint;
typedef signed int	int32;
typedef unsigned int	uint32;

struct chunk;

typedef struct {
	char *cur, *limit;
	struct chunk *head;
} Region;

void rinit(Region *);
void *ralloc(Region *, int);
void *ralloc0(Region *, int);
void rfreeall(Region *);
void rfree(Region *, void *);

#define NEW(p, r) p = ralloc(r, sizeof *p)
#define NEW0(p, r) p = ralloc0(r, sizeof *p)

#define TODO()\
	do {\
		fprintf(stderr, "%s:%d: TODO\n", __FILE__, __LINE__);\
		abort();\
	} while (0)

#define NELEM(x) (sizeof(x)/sizeof(*(x)))

typedef char TCHAR;

typedef struct buf {
	int (*putc)(struct buf *, TCHAR);
	int (*puts)(struct buf *, const TCHAR *, int);
} Buf;

typedef struct {
	Buf buf;
	TCHAR *start, *cur, *end;
} HeapBuf;

int init_heapbuf(HeapBuf *);
int bprintf(Buf *, const TCHAR *fmt, ...);

#ifdef USE_FILEBUF
typedef struct {
	Buf buf;
	FILE *fp;
} FileBuf;
int init_filebuf(FileBuf *, FILE *);
#endif
