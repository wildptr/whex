/* THIS FILE IS MEANT TO BE INCLUDED FROM u.h; DO NOT USE DIRECTLY */

/* buffer */

typedef struct T_(buf) {
	void (*putc)(struct T_(buf) *, _TCHAR);
	void (*puts)(struct T_(buf) *, const _TCHAR *, int);
} T_(Buf);

typedef struct {
	T_(Buf) buf;
	_TCHAR *start, *cur, *end;
} T_(HeapBuf);

/* WARNING: FixedBuf does not check for buffer overflow! */
typedef struct {
	T_(Buf) buf;
	_TCHAR *cur;
} T_(FixedBuf);

typedef struct {
	T_(Buf) buf;
	FILE *fp;
} T_(FileBuf);

typedef struct {
	T_(Buf) buf;
	Region *r;
	_TCHAR *start;
} T_(RegionBuf);

void T_(init_heapbuf)(T_(HeapBuf) *);
void T_(init_heapbuf_size)(T_(HeapBuf) *, int);
_TCHAR *T_(finish_heapbuf)(T_(HeapBuf) *);
void T_(init_fixedbuf)(T_(FixedBuf) *, _TCHAR *);
void T_(init_filebuf)(T_(FileBuf) *, FILE *);
void T_(init_regionbuf)(T_(RegionBuf) *, Region *);
_TCHAR *T_(finish_regionbuf)(T_(RegionBuf) *);

/* printf */

void T_(vbprintf)(T_(Buf) *b, const _TCHAR *fmt, va_list va);
void T_(bprintf)(T_(Buf) *b, const _TCHAR *fmt, ...);
void T_(_vsprintf)(_TCHAR *s, const _TCHAR *fmt, va_list va);
void T_(_sprintf)(_TCHAR *buf, const _TCHAR *fmt, ...);
_TCHAR *T_(asprintf)(const _TCHAR *fmt, ...);
_TCHAR *T_(rsprintf)(Region *r, const _TCHAR *fmt, ...);
void T_(_vfprintf)(FILE *fp, const _TCHAR *fmt, va_list va);
void T_(_fprintf)(FILE *fp, const _TCHAR *fmt, ...);
void T_(_printf)(const _TCHAR *fmt, ...);
void T_(eprintf)(const _TCHAR *fmt, ...);
