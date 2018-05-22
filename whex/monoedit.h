typedef struct med_tag {
	struct med_tag *next;
	int start;
	int len;
	uint attr;
} MedTag;

typedef void (*MedGetLineProc)(uint64 ln, Buf *buf, void *arg);

typedef struct {
	uchar mask;
	MedGetLineProc getline;
	void *getline_arg;
	HFONT font;
} MedConfig;

enum {
	MED_CONFIG_GETLINE = 1,
	MED_CONFIG_FONT = 2,
};

enum {
	MED_NOTIFY_POS_CHANGED
};

ATOM med_register_class(void);
void med_set_current_line(HWND, uint64 ln);
uint64 med_get_current_line(HWND);
void med_set_total_lines(HWND, uint64 ln);
uint64 med_get_total_lines(HWND);
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
void med_scroll_up_line(HWND);
void med_scroll_down_line(HWND);
void med_scroll_up_page(HWND);
void med_scroll_down_page(HWND);
int med_get_nrow(HWND);
void med_get_cursor_pos(HWND, int pos[2]);
void med_move_left(HWND);
void med_move_right(HWND);
void med_move_up(HWND);
void med_move_down(HWND);
void med_reset_position(HWND);
