typedef struct {
    uchar flags;
    COLORREF text_color;
    COLORREF bg_color;
} MedTextAttr;

typedef struct med_tag_list MedTagList;

typedef void (*MedGetLineProc)(uint64 ln, T(Buf) *buf, void *arg, MedTagList *);

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

enum {
    MED_ATTR_TEXT_COLOR	= 1,
    MED_ATTR_BG_COLOR	= 2,
    MED_ATTR_BOLD_FONT	= 4,
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
void med_update_buffer_row(HWND, int);
void med_scroll(HWND, int delta);
void med_add_overlay(HWND, int ln, int start, int len, MedTextAttr *attr);
void med_clear_overlay(HWND);
void med_set_cursor_pos(HWND, int y, int x);
void med_set_size(HWND, int nrow, int ncol);
void med_set_char(HWND, int y, int x, TCHAR c);
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
void med_update_canvas(HWND);
void med_update_canvas_row(HWND, int);
void med_invalidate_char(HWND, int, int);
void med_add_tag(MedTagList *, int start, int len, MedTextAttr *attr);
