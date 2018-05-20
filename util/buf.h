typedef struct bufA {
	int (*putc)(struct bufA *, char);
	int (*puts)(struct bufA *, const char *, int);
} BufA;

typedef struct bufW {
	int (*putc)(struct bufW *, wchar_t);
	int (*puts)(struct bufW *, const wchar_t *, int);
} BufW;

typedef struct {
	BufA buf;
	char *start, *cur, *end;
} HeapBufA;

typedef struct {
	BufW buf;
	wchar_t *start, *cur, *end;
} HeapBufW;

typedef struct {
	BufA buf;
	char *cur;
} FixedBufA;

typedef struct {
	BufW buf;
	wchar_t *cur;
} FixedBufW;

int init_heapbufA(HeapBufA *);
int init_heapbufW(HeapBufW *);
void init_fixedbufA(FixedBufA *, char *);
void init_fixedbufW(FixedBufW *, wchar_t *);

#ifdef UNICODE
typedef BufW Buf;
typedef HeapBufW HeapBuf;
typedef FixedBufW FixedBuf;
#define init_heapbuf init_heapbufW
#define init_fixedbuf init_fixedbufW
#else
typedef BufA Buf;
typedef HeapBufA HeapBuf;
typedef FixedBufA FixedBuf;
#define init_heapbuf init_heapbufA
#define init_fixedbuf init_fixedbufA
#endif
