typedef struct {
	BufA buf;
	FILE *fp;
} FileBufA;

typedef struct {
	BufW buf;
	FILE *fp;
} FileBufW;

void init_filebufA(FileBufA *, FILE *);
void init_filebufW(FileBufW *, FILE *);

#ifdef UNICODE
typedef FileBufW FileBuf;
#define init_filebuf init_filebufW
#else
typedef FileBufA FileBuf;
#define init_filebuf init_filebufA
#endif
