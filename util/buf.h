typedef struct buf {
	int (*putc)(struct buf *, TCHAR);
	int (*puts)(struct buf *, const TCHAR *, int);
} Buf;

typedef struct {
	Buf buf;
	TCHAR *start, *cur, *end;
} HeapBuf;

typedef struct {
	Buf buf;
	TCHAR *cur;
} FixedBuf;

int init_heapbuf(HeapBuf *);
void init_fixedbuf(FixedBuf *, TCHAR *);
int bprintf(Buf *, const TCHAR *fmt, ...);
