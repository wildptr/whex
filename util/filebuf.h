typedef struct {
	Buf buf;
	FILE *fp;
} FileBuf;

void init_filebuf(FileBuf *, FILE *);
