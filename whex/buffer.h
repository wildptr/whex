typedef struct buffer Buffer;

extern const int sizeof_Buffer;

int buf_init(Buffer *, HANDLE);
void buf_finalize(Buffer *);
void buf_read(Buffer *, uchar *, uint64, size_t);
uchar buf_getbyte(Buffer *, uint64);
int buf_save(Buffer *, HANDLE);
int buf_save_in_place(Buffer *);
void buf_replace(Buffer *, uint64, const uchar *, size_t);
void buf_insert(Buffer *, uint64, const uchar *, size_t);
uint64 buf_size(Buffer *);
int buf_kmp_search(Buffer *b, const uchar *pat, int len, uint64 start,
		   uint64 *pos);
int buf_kmp_search_backward(Buffer *b, const uchar *pat, int len, uint64 start,
			    uint64 *pos);
