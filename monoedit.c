#include "u.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "monoedit.h"

/* text attributes */
typedef struct tag {
    struct tag *next;
    int start;
    int len;
    MedTextAttr attr;
} Tag;

typedef struct {
    TCHAR *text;
    Tag *segments; /* counted array */
    Tag *overlay;
    int textlen;
    int nseg;
} Line;

typedef struct {
    /* in characters */
    int ncol;
    int nrow;
    Line *buffer;
    HFONT font;
    int cursorx;
    int cursory;
    int charwidth;
    int charheight;
    MedGetLineProc getline;
    void *getline_arg;
    uint64 current_line;
    uint64 total_lines;
    int clientwidth;
    int clientheight;
    HBRUSH bgbrush;
    int sbscale;
    uint id;
    COLORREF default_text_color;
    COLORREF default_bg_color;
    HBITMAP canvas_bitmap;
    HDC canvas_dc;
} Med;

struct med_tag_list {
    Tag *first;
    Tag *last;
};

#if 0
typedef struct {
    int len, next;
} FreeList;
#endif

static void set_source(Med *w, MedGetLineProc proc, void *arg);
#if 0
static TCHAR *alloc_text(Med *w, int nch);
static void free_text(Med *w, TCHAR *text, int nch);
static void init_textbuf(Med *w, int len);
#endif
static void free_line(Line *l);
static void update_buffer(Med *w);
static void scroll(Med *w, HWND hwnd, int delta);
static void set_size(Med *w, int nrow, int ncol);
static void scroll_up_line(Med *w, HWND hwnd);
static void scroll_down_line(Med *w, HWND hwnd);
static void scroll_up_page(Med *w, HWND hwnd);
static void scroll_down_page(Med *w, HWND hwnd);
static void scroll_to(Med *w, HWND hwnd, uint64 ln);
static void update_scrollbar_pos(Med *w, HWND hwnd);
static void update_scrollbar_range(Med *w, HWND hwnd);
static void notify_parent(Med *, HWND);
static void move_up(Med *, HWND);
static void move_down(Med *, HWND);
static void move_left(Med *w);
static void move_right(Med *w);
static void set_cursor_pos(Med *, HWND, int y, int x);
static void update_canvas(Med *);

/* `l` is the line containing `seg` */
static void
paint_segment(Med *w, HDC dc, Line *l, Tag *seg, int y)
{
    COLORREF textcolor = w->default_text_color;
    COLORREF bgcolor = w->default_bg_color;
    int x;
    MedTextAttr *attr = &seg->attr;
    if (attr->flags & MED_ATTR_TEXT_COLOR)
        textcolor = attr->text_color;
    if (attr->flags & MED_ATTR_BG_COLOR)
        bgcolor = attr->bg_color;
    SetTextColor(dc, textcolor);
    SetBkColor(dc, bgcolor);
    x = seg->start * w->charwidth;
    /* both TextOut and DrawText flicker on Windows XP */
    if (seg->start + seg->len <= l->textlen) {
        TextOut(dc, x, y, l->text + seg->start, seg->len);
    } else {
        /* segment ends beyond text */
        /* TODO: make sure seg->len is small enough */
        TCHAR *text = alloca(seg->len * sizeof *text);
        int src = seg->start;
        int dst = 0;
        while (src < l->textlen) {
            text[dst++] = l->text[src++];
        }
        while (dst < seg->len) {
            text[dst++] = ' ';
        }
        TextOut(dc, x, y, text, seg->len);
    }
}

static void
paint_row(Med *w, HDC dc, int row)
{
    Line *l;
    int y;
    int n;
    int i;
    int linelen;
    RECT r;
    Tag *t;

    assert(row >= 0 && row < w->nrow);
    l = &w->buffer[row];
    y = w->charheight * row;
    n = l->nseg;
    for (i=0; i<n; i++) {
        paint_segment(w, dc, l, l->segments+i, y);
    }
    for (t=l->overlay; t; t=t->next) {
        paint_segment(w, dc, l, t, y);
    }
    linelen = 0;
    if (n) {
        Tag *lastseg = &l->segments[n-1];
        linelen = lastseg->start + lastseg->len;
    }
    /* fill right margin */
    r.left = w->charwidth * linelen;
    r.right = w->clientwidth;
    if (r.left < r.right) {
        r.top = y;
        r.bottom = y + w->charheight;
        FillRect(dc, &r, w->bgbrush);
    }
}

static void
update_canvas(Med *w)
{
    HDC dc = w->canvas_dc;
    RECT r = { 0, 0, w->clientwidth, w->clientheight };

    if (w->buffer) {
        int i;
        for (i=0; i<w->nrow; i++) {
            paint_row(w, dc, i);
        }
        r.top = w->nrow * w->charheight;
    }
    if (r.top < r.bottom) {
        FillRect(dc, &r, w->bgbrush);
    }
}

static void
add_overlay(Med *w, int row, int start, int len, MedTextAttr *attr)
{
    Line *l;
    Tag *newtag;
    Tag *last;

    assert(row >= 0 && row < w->nrow);
    l = &w->buffer[row];
    assert(l);

    newtag = xmalloc(sizeof *newtag);
    newtag->next = 0;
    newtag->start = start;
    newtag->len = len;
    newtag->attr = *attr;

#if 0
    before = 0;
    next = l->overlay;
    while (next && next->start < newtag->start) {
        before = next;
        next = before->next;
    }
    newtag->next = next;
    if (before) {
        before->next = newtag;
    } else {
        l->overlay = newtag;
    }
#endif

    last = l->overlay;
    if (last) {
        while (last->next) last = last->next;
        last->next = newtag;
    } else {
        l->overlay = newtag;
    }
}

static void
default_getline(uint64 _ln, T(Buf) *_b, void *_arg, MedTagList *_taglist)
{
}

static void
set_font(Med *w, HWND hwnd, HFONT font)
{
    HDC dc = w->canvas_dc;
    TEXTMETRIC tm;
    SelectObject(dc, font);
    GetTextMetrics(dc, &tm);
    w->charwidth = tm.tmAveCharWidth;
    w->charheight = tm.tmHeight;
    w->font = font;
}

static void
update_caret_pos(Med *w)
{
    SetCaretPos(w->cursorx*w->charwidth, w->cursory*w->charheight);
}

static LRESULT CALLBACK
wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);

    switch (message) {
    case WM_NCCREATE:
        w = xmalloc(sizeof *w);
        memset(w, 0, sizeof *w);
        w->getline = default_getline;
        w->bgbrush = (HBRUSH) GetClassLongPtr(hwnd, GCLP_HBRBACKGROUND);
        {
            HDC dc = GetDC(hwnd);
            w->default_text_color = GetTextColor(dc);
            w->default_bg_color = GetBkColor(dc);
            w->canvas_dc = CreateCompatibleDC(dc);
            ReleaseDC(hwnd, dc);
        }
        {
            CREATESTRUCT *cs = (CREATESTRUCT *) lparam;
            uchar has_font = 0;
            MedConfig *conf = cs->lpCreateParams;
            if (conf) {
                if (conf->mask & MED_CONFIG_GETLINE) {
                    set_source(w, conf->getline,
                               conf->getline_arg);
                }
                if (conf->mask & MED_CONFIG_FONT) {
                    has_font = 1;
                    set_font(w, hwnd, (HFONT) conf->font);
                }
            }
            if (!has_font) {
                set_font(w, hwnd, (HFONT)
                         GetStockObject(ANSI_FIXED_FONT));
            }
            w->id = (uint) cs->hMenu;
        }
        SetWindowLongPtr(hwnd, 0, (LONG_PTR) w);
        break;
    case WM_NCDESTROY:
        for (int i=0; i<w->nrow; i++) {
            free_line(&w->buffer[i]);
        }
        free(w->buffer);
        free(w);
        DeleteDC(w->canvas_dc);
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            int x = ps.rcPaint.left;
            int y = ps.rcPaint.top;
            int wid = ps.rcPaint.right - x;
            int hei = ps.rcPaint.bottom - y;
            BitBlt(dc, x, y, wid, hei,
                   w->canvas_dc, x, y, SRCCOPY);
            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_SETFONT:
        set_font(w, hwnd, (HFONT) wparam);
        return 0;
    case WM_SETFOCUS:
        CreateCaret(hwnd, 0, w->charwidth, w->charheight);
        update_caret_pos(w);
        ShowCaret(hwnd);
        return 0;
    case WM_KILLFOCUS:
        HideCaret(hwnd);
        DestroyCaret();
        return 0;
    case WM_CHAR:
        /* forward WM_CHAR to parent */
        SendMessage(GetParent(hwnd), WM_CHAR, wparam, lparam);
        return 0;
    case WM_ERASEBKGND:
        return 0;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            int wid = LOWORD(lparam);
            int hei = HIWORD(lparam);
            int ch = w->charheight;
            int cw = w->charwidth;
            int new_nrow = hei/ch;
            int new_ncol = wid/cw;
            w->clientwidth = wid;
            w->clientheight = hei;
            set_size(w, new_nrow, new_ncol);
            InvalidateRect(hwnd, 0, 0);
            {
                HDC window_dc = GetDC(hwnd);
                HDC back_dc = w->canvas_dc;
                HBITMAP old_bitmap;
                w->canvas_bitmap = CreateCompatibleBitmap
                    (window_dc, wid, hei);
                ReleaseDC(hwnd, window_dc);
                old_bitmap = SelectObject(back_dc,
                                          w->canvas_bitmap);
                if (old_bitmap)
                    DeleteObject(old_bitmap);
            }
            update_canvas(w);
        }
        return 0;
    case WM_MOUSEWHEEL:
        {
            short delta = HIWORD(wparam);
            if (delta > 0) {
                int n = delta / WHEEL_DELTA;
                if (n > w->current_line) n = (int)w->current_line;
                scroll(w, hwnd, n);
            } else {
                int n = (-delta) / WHEEL_DELTA;
                if (w->current_line + n > w->total_lines) {
                    n = (int)(w->total_lines - w->current_line);
                }
                scroll(w, hwnd, -n);
            }
        }
notify:
        notify_parent(w, hwnd);
        return 0;
    case WM_LBUTTONDOWN:
        {
            int x = LOWORD(lparam);
            int y = HIWORD(lparam);
            int cx = x / w->charwidth;
            int cy = y / w->charheight;
            if (cx < w->ncol && cy < w->nrow) {
                w->cursorx = cx;
                w->cursory = cy;
                if (SetFocus(hwnd) == hwnd) {
                    update_caret_pos(w);
                }
            }
        }
        goto notify;
    case WM_VSCROLL:
        switch (LOWORD(wparam)) {
        case SB_LINEDOWN:
            scroll_down_line(w, hwnd);
            goto notify;
        case SB_LINEUP:
            scroll_up_line(w, hwnd);
            goto notify;
        case SB_PAGEDOWN:
            scroll_down_page(w, hwnd);
            goto notify;
        case SB_PAGEUP:
            scroll_up_page(w, hwnd);
            goto notify;
        case SB_TOP:
            scroll_to(w, hwnd, 0);
            goto notify;
        case SB_BOTTOM:
            scroll_to(w, hwnd, w->total_lines);
            goto notify;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            {
                SCROLLINFO si;
                si.cbSize = sizeof si;
                si.fMask = SIF_TRACKPOS;
                GetScrollInfo(hwnd, SB_VERT, &si);
                scroll_to(w, hwnd, (uint64)si.nTrackPos << w->sbscale);
            }
            goto notify;
        }
        return 0;
    case WM_KEYDOWN:
        switch (wparam) {
        case VK_UP:
            move_up(w, hwnd);
            goto notify;
        case VK_DOWN:
            move_down(w, hwnd);
            goto notify;
        case VK_LEFT:
            move_left(w);
            goto notify;
        case VK_RIGHT:
            move_right(w);
            goto notify;
        case VK_PRIOR:
            scroll_up_page(w, hwnd);
            goto notify;
        case VK_NEXT:
            scroll_down_page(w, hwnd);
            goto notify;
        }
        return 0;
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

ATOM
med_register_class(void)
{
    WNDCLASS wndclass = {0};
    wndclass.style = CS_GLOBALCLASS;
    wndclass.lpfnWndProc = wndproc;
    wndclass.cbWndExtra = sizeof(LONG_PTR);
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
    wndclass.lpszClassName = TEXT("MonoEdit");
    return RegisterClass(&wndclass);
}

static void
scroll_to(Med *w, HWND hwnd, uint64 ln)
{
    if (ln >= 0x8000000000000000ULL) {
        eprintf("ln=%llu\n", ln);
    }
    w->current_line = ln;
    update_buffer(w);
    update_canvas(w);
    InvalidateRect(hwnd, 0, 0);
    update_scrollbar_pos(w, hwnd);
}

void
med_set_current_line(HWND hwnd, uint64 ln)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    scroll_to(w, hwnd, ln);
}

uint64
med_get_current_line(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    return w->current_line;
}

void
med_set_total_lines(HWND hwnd, uint64 n)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    w->total_lines = n;
    update_scrollbar_range(w, hwnd);
}

uint64
med_get_total_lines(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    return w->total_lines;
}

static void
set_source(Med *w, MedGetLineProc proc, void *arg)
{
    w->getline = proc;
    w->getline_arg = arg;
}

void
med_set_source(HWND hwnd, MedGetLineProc proc, void *arg)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    set_source(w, proc, arg);
}

#if 0

static TCHAR *
alloc_text(Med *w, int nch)
{
    int len = nch * sizeof(TCHAR);
    len = (len+7)&-8;
    FreeList *f = (FreeList *) w->textbuf;
    FreeList *prev = 0;
    for (;;) {
        if (f->len > len) {
            int newlen = f->len - len;
            f->len = newlen;
            return (TCHAR *)((char *) f + newlen);
        }
        if (f->len == len && prev) {
            prev->next = f->next;
            return (TCHAR *) f;
        }
        if (f->next == 0) return 0;
        prev = f;
        f = (FreeList *)((char *) w->textbuf + f->next);
    }
}

static void
free_text(Med *w, TCHAR *text, int nch)
{
    if (!text) return;
    int len = nch * sizeof(TCHAR);
    len = (len+7)&-8;
    char *start = (char *) w->textbuf;
    char *cur = start;
    char *next;
    FreeList *before, *after;
loop:
    before = (FreeList *) cur;
    next = start + before->next;
    if (next <= (char *) text) {
        cur = next;
        goto loop;
    }

    char *textend = (char *) text + len;
    after = (FreeList *) next;
    if ((char *) before + before->len == (char *) text) {
        if (textend == next) {
            before->len += len + after->len;
            before->next = after->next;
        } else {
            before->len += len;
        }
    } else {
        FreeList *newf = (FreeList *) text;
        if (textend == next) {
            newf->len = len + after->len;
            newf->next = after->next;
        } else {
            newf->len = len;
            newf->next = before->next;
        }
        before->next = (char *) newf - start;
    }
}

TCHAR *
med_alloc_text(HWND hwnd, int nch)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    return alloc_text(w, nch);
}

static void
init_textbuf(Med *w, int len)
{
    w->textbuf = xmalloc(len);
    FreeList *f = (FreeList *) w->textbuf;
    f->len = len;
    f->next = len+8;
}

#endif

static void
free_line_overlay(Line *l)
{
    Tag *t = l->overlay;
    while (t) {
        Tag *next = t->next;
        free(t);
        t = next;
    }
    l->overlay = 0;
}

static void
free_line(Line *l)
{
    free(l->text);
    l->text = 0;
    free(l->segments);
    l->segments = 0;
    free_line_overlay(l);
}

static void
getline(Med *w, int row)
{
    T(HeapBuf) hb;
    Line *l = &w->buffer[row];
    MedTagList taglist;
    int n;
    int maxnseg;
    int nextstart;
    int prev_end;
    Tag *segments, *t;

    T(init_heapbuf)(&hb);

    taglist.first = 0;
    taglist.last = (Tag *) &taglist.first;
    /*{
        if (w->current_line + row > 0x8000000000000000ULL) {
            eprintf("w->current_line=%llu row=%d\n", w->current_line, row);
        }
    }*/

    w->getline(w->current_line + row, &hb.buf, w->getline_arg, &taglist);
    l->text = hb.start;
    l->textlen = hb.cur - hb.start;

    n = 0;
    t = taglist.first;
    while (t) {
        n++;
        t = t->next;
    }
    maxnseg = n*2+1;
    segments = xmalloc(maxnseg * sizeof *segments);
    t = taglist.first;
    n = 0;
    prev_end = 0;
    for (;;) {
        int len;
        if (t) {
            nextstart = t->start;
        } else {
            nextstart = max(l->textlen, prev_end);
        }
        /* untagged segment */
        len = nextstart - prev_end;
        if (len) {
            segments[n].start = prev_end;
            segments[n].len = len;
            segments[n].attr.flags = 0;
            n++;
        }
        if (!t) break;
        /* tagged segment */
        segments[n++] = *t;
        prev_end = t->start + t->len;
        t = t->next;
    }
    l->segments = segments;
    l->nseg = n;
    t = taglist.first;
    while (t) {
        Tag *next = t->next;
        free(t);
        t = next;
    }
}

static void
update_buffer_row(Med *w, int i)
{
    free_line(&w->buffer[i]);
    getline(w, i);
}

static void
update_buffer(Med *w)
{
    int i;
    for (i=0; i<w->nrow; i++) {
        free_line(&w->buffer[i]);
        getline(w, i);
    }
}

void
med_update_buffer(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    update_buffer(w);
}

void
med_update_buffer_row(HWND hwnd, int i)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    update_buffer_row(w, i);
}

static void
scroll(Med *w, HWND hwnd, int delta)
{
#if 0
    RECT scroll_rect = {
        0,
        0,
        w->charwidth * w->ncol,
        w->charheight * w->nrow
    };
#endif
    w->current_line -= delta;
    if (delta > 0) {
        /* move lines down, first 'delta' lines exposed */
        int d, s;
        int i;
        if (delta > w->nrow) delta = w->nrow;
        d = w->nrow-1;
        s = d - delta;
        for (i=w->nrow-delta; i<w->nrow; i++) {
            free_line(&w->buffer[i]);
        }
        while (s >= 0) {
            w->buffer[d--] = w->buffer[s--];
        }
        i = 0;
        memset(w->buffer+i, 0, delta * sizeof *w->buffer);
        while (i < delta)
            getline(w, i++);
    } else if (delta < 0) {
        /* move lines up, last 'delta' lines exposed */
        int d, s;
        int i;
        delta = -delta;
        if (delta > w->nrow) delta = w->nrow;
        d = 0;
        s = delta;
        for (i=0; i<delta; i++) {
            free_line(&w->buffer[i]);
        }
        while (s < w->nrow) {
            w->buffer[d++] = w->buffer[s++];
        }
        i = w->nrow - delta;
        memset(w->buffer+i, 0, delta * sizeof *w->buffer);
        while (i < w->nrow)
            getline(w, i++);
    }
    update_scrollbar_pos(w, hwnd);
    update_canvas(w);
    InvalidateRect(hwnd, 0, 0);
}

void
med_scroll(HWND hwnd, int delta)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    scroll(w, hwnd, delta);
}

void
med_add_overlay(HWND hwnd, int ln, int start, int len, MedTextAttr *attr)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    add_overlay(w, ln, start, len, attr);
}

static void
clear_overlay(Med *w)
{
    int i;
    for (i=0; i<w->nrow; i++) {
        Line *l = &w->buffer[i];
        free_line_overlay(l);
    }
}

void
med_clear_overlay(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    clear_overlay(w);
}

static void
set_cursor_pos(Med *w, HWND hwnd, int y, int x)
{
    w->cursory = y;
    w->cursorx = x;
    SetCaretPos(x*w->charwidth, y*w->charheight);
}

void
med_set_cursor_pos(HWND hwnd, int y, int x)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    set_cursor_pos(w, hwnd, y, x);
}

static void
set_size(Med *w, int nrow, int ncol)
{
    int i;

    assert(nrow >= 0);
    assert(ncol >= 0);
    w->ncol = ncol;
    if (nrow == w->nrow) return;
    if (nrow < w->nrow) {
        for (i=nrow; i<w->nrow; i++) {
            free_line(&w->buffer[i]);
        }
    }
    w->buffer = xrealloc(w->buffer, nrow * sizeof *w->buffer);
    if (nrow > w->nrow) {
        memset(&w->buffer[w->nrow], 0,
               (nrow - w->nrow) * sizeof w->buffer[0]);
        for (i=w->nrow; i<nrow; i++) {
            getline(w, i);
        }
    }
    w->nrow = nrow;
}

void
med_set_size(HWND hwnd, int nrow, int ncol)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    set_size(w, nrow, ncol);
}

void
med_set_char(HWND hwnd, int y, int x, TCHAR c)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    Line *l;
    if (y < 0 || y >= w->nrow) return;
    l = &w->buffer[y];
    if (x < 0 || x >= l->textlen) return;
    l->text[x] = c;
}

static void
scroll_up_line(Med *w, HWND hwnd)
{
    if (w->current_line) {
        scroll(w, hwnd, 1);
    }
}

static void
scroll_down_line(Med *w, HWND hwnd)
{
    if (w->current_line < w->total_lines) {
        scroll(w, hwnd, -1);
    }
}

void
med_scroll_up_line(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    scroll_up_line(w, hwnd);
}

void
med_scroll_down_line(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    scroll_down_line(w, hwnd);
}

static void
scroll_up_page(Med *w, HWND hwnd)
{
    uint64 curline = w->current_line;
    int delta;
    if (curline >= w->nrow) {
        delta = w->nrow;
    } else {
        delta = (int) curline;
    }
    scroll(w, hwnd, delta);
}

static void
scroll_down_page(Med *w, HWND hwnd)
{
    uint64 curline = w->current_line;
    int delta;
    if (curline + w->nrow <= w->total_lines) {
        delta = w->nrow;
    } else {
        delta = (int)(w->total_lines - curline);
    }
    scroll(w, hwnd, -delta);
}

void
med_scroll_up_page(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    scroll_up_page(w, hwnd);
}

void
med_scroll_down_page(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    scroll_down_page(w, hwnd);
}

int
med_get_nrow(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    return w->nrow;
}

static int
compute_scrollbar_pos(Med *w)
{
    return (int)(w->current_line >> w->sbscale);
}

static void
update_scrollbar_pos(Med *w, HWND hwnd)
{
    int newpos = compute_scrollbar_pos(w);
    SCROLLINFO si;
    si.cbSize = sizeof si;
    si.fMask = SIF_POS;
    si.nPos = newpos;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

static void
update_scrollbar_range(Med *w, HWND hwnd)
{
    uint64 bottom = w->total_lines;
    int s = 0;
    SCROLLINFO si;
    int maxscroll;
    int newpos;

    while (bottom >> s >= 0x80000000) s++;
    w->sbscale = s;
    maxscroll = (int)(bottom >> s);
    newpos = compute_scrollbar_pos(w);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_POS;
    si.nMin = 0;
    si.nMax = maxscroll;
    si.nPos = newpos;
    //eprintf("nMax=%d nPos=%d\n", si.nMax, si.nPos);
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

static void
notify_parent(Med *w, HWND hwnd)
{
    SendMessage(GetParent(hwnd), WM_COMMAND,
                MED_NOTIFY_POS_CHANGED << 16 | w->id, (LPARAM) hwnd);
}

void
med_get_cursor_pos(HWND hwnd, int pos[2])
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    pos[0] = w->cursory;
    pos[1] = w->cursorx;
}

static void
move_left(Med *w)
{
    if (w->cursorx > 0) {
        w->cursorx--;
        update_caret_pos(w);
    }
}

void
med_move_left(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    move_left(w);
}

static void
move_right(Med *w)
{
    if (w->cursorx+1 < w->ncol) {
        w->cursorx++;
        update_caret_pos(w);
    }
}

void
med_move_right(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    move_right(w);
}

static void
move_up(Med *w, HWND hwnd)
{
    if (w->cursory > 0) {
        w->cursory--;
        update_caret_pos(w);
    } else {
        scroll_up_line(w, hwnd);
        notify_parent(w, hwnd);
    }
}

void
med_move_up(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    move_up(w, hwnd);
}

static void
move_down(Med *w, HWND hwnd)
{
    if (w->cursory+1 < w->nrow) {
        w->cursory++;
        update_caret_pos(w);
    } else {
        scroll_down_line(w, hwnd);
        notify_parent(w, hwnd);
    }
}

void
med_move_down(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    move_down(w, hwnd);
}

void
med_reset_position(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    w->current_line = 0;
    set_cursor_pos(w, hwnd, 0, 0);
}

void
med_invalidate_char(HWND hwnd, int row, int col)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    RECT r;
    r.left = col*w->charwidth;
    r.top = row*w->charheight;
    r.right = r.left + w->charwidth;
    r.bottom = r.top + w->charheight;
    InvalidateRect(hwnd, &r, 0);
}

void
med_update_canvas(HWND hwnd)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    update_canvas(w);
}

void
med_update_canvas_row(HWND hwnd, int row)
{
    Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
    HDC dc = w->canvas_dc;
    paint_row(w, dc, row);
}

void
med_add_tag(MedTagList *taglist, int start, int len, MedTextAttr *attr)
{
    Tag *newtag = xmalloc(sizeof *newtag);
    newtag->next = 0;
    newtag->start = start;
    newtag->len = len;
    newtag->attr = *attr;
    taglist->last->next = newtag;
    taglist->last = newtag;
}
