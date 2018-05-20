typedef struct med_tag {
	struct med_tag *next;
	int start;
	int len;
	unsigned attr;
} MedTag;

typedef struct {
	TCHAR *text; /* NOT null-terminated! */
	MedTag *tags;
	int textlen;
} MedLine;

typedef void (*MedGetLineProc)
	(long long ln, MedLine *line, void *arg);

ATOM med_register_class(void);
void med_set_current_line(HWND, long long ln);
long long med_get_current_line(HWND);
void med_set_source(HWND, MedGetLineProc proc, void *arg);
#if 0
TCHAR *med_alloc_text(HWND, int nch);
#endif
void med_update_buffer(HWND);
void med_scroll(HWND, int delta);
void med_add_tag(HWND, int ln, MedTag *tag);
void med_clear_tags(HWND);
void med_set_cursor_pos(HWND, int y, int x);
void med_set_size(HWND, int nrow, int ncol);
void med_set_char(HWND, int y, int x, TCHAR c);
void med_paint_row(HWND hwnd, int row);
