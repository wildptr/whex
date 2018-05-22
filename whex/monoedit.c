#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "types.h"
#include "buf.h"
#include "monoedit.h"
#include "printf.h"

typedef struct {
	TCHAR *text;
	MedTag *tags;
	int textlen;
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
	MedTag *tagbuf;
	int ntag;
	int tag_cap;
	int clientwidth;
	int clientheight;
	HBRUSH bgbrush;
	int sbscale;
	uint id;
} Med;

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
static void free_line(Med *w, Line *l);
#endif
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

static void
paint_row(Med *w, HWND hwnd, HDC dc, int row)
{
	assert(row >= 0 && row < w->nrow);
	Line *line = &w->buffer[row];
	int ntag = 0;
	MedTag *t = line->tags;
	while (t) {
		ntag++;
		t = t->next;
	}
	int maxnseg = ntag*2+1;
	MedTag *segments = malloc(maxnseg * sizeof *segments);
	int nextstart;
	t = line->tags;
	int s = 0;
	int prev_end = 0;
	for (;;) {
		if (t) {
			nextstart = t->start;
		} else {
			nextstart = max(line->textlen, prev_end);
		}
		/* untagged segment */
		int len = nextstart - prev_end;
		if (len) {
			segments[s].start = prev_end;
			segments[s].len = len;
			segments[s].attr = 0;
			s++;
		}
		if (!t) break;
		/* tagged segment */
		segments[s++] = *t;
		prev_end = t->start + t->len;
		t = t->next;
	}
	int y = row * w->charheight;
	for (int i=0; i<s; i++) {
		MedTag *seg = &segments[i];
		COLORREF old_bkcolor;
		if (seg->attr) {
			old_bkcolor = SetBkColor
				(dc, RGB(204, 204, 204));
		}
		int x = seg->start * w->charwidth;
		if (seg->start + seg->len <= line->textlen) {
			TextOut(dc, x, y, line->text + seg->start, seg->len);
		} else {
			TCHAR *text = malloc(seg->len * sizeof *text);
			int src = seg->start;
			int dst = 0;
			while (src < line->textlen) {
				text[dst++] = line->text[src++];
			}
			while (dst < seg->len) {
				text[dst++] = ' ';
			}
			TextOut(dc, x, y, text, seg->len);
			free(text);
		}
		if (seg->attr) {
			SetBkColor(dc, old_bkcolor);
		}
	}
	int linelen = 0;
	if (s) {
		MedTag *lastseg = &segments[s-1];
		linelen = lastseg->start + lastseg->len;
	}
	free(segments);
	/* fill right margin */
	RECT r;
	r.left = w->charwidth * linelen;
	r.right = w->clientwidth;
	if (r.left < r.right) {
		r.top = y;
		r.bottom = y + w->charheight;
		FillRect(dc, &r, w->bgbrush);
	}
}

static void
paint(Med *w, HWND hwnd)
{
	PAINTSTRUCT paint;
	HDC dc = BeginPaint(hwnd, &paint);
	RECT r = { 0, 0, w->clientwidth, w->clientheight };

	SelectObject(dc, w->bgbrush);
	if (w->buffer) {
		SelectObject(dc, w->font);
		for (int i=0; i<w->nrow; i++) {
			paint_row(w, hwnd, dc, i);
		}
		r.top = w->nrow * w->charheight;
	}
	if (r.top < r.bottom) {
		FillRect(dc, &r, w->bgbrush);
	}

	EndPaint(hwnd, &paint);
}

static void
clear_tags(Med *w)
{
	for (int i=0; i<w->nrow; i++) {
		Line *line = &w->buffer[i];
		line->tags = 0;
	}
	w->ntag = 0;
#if 0
	_printf("clear_tags\n");
#endif
}

static void
add_tag(Med *w, int row, MedTag *tag)
{
	assert(row >= 0 && row < w->nrow);
	Line *line = &w->buffer[row];
	assert(line);

	if (w->ntag == w->tag_cap) {
		if (w->tagbuf) {
			w->tag_cap *= 2;
			w->tagbuf = realloc(w->tagbuf, w->tag_cap *
					    sizeof *w->tagbuf);
		} else {
			w->tag_cap = 16;
			w->tagbuf = malloc(w->tag_cap * sizeof *w->tagbuf);
		}
	}
	MedTag *newtag = w->tagbuf + w->ntag++;
#if 0
	_printf("add_tag row=%d start=%d len=%d\n", row, tag->start, tag->len);
#endif
	newtag->start = tag->start;
	newtag->len = tag->len;
	newtag->attr = tag->attr;

	MedTag *before = 0;
	MedTag *next = line->tags;
	while (next && next->start < newtag->start) {
		before = next;
		next = before->next;
	}
	newtag->next = next;
	if (before) {
		before->next = newtag;
	} else {
		line->tags = newtag;
	}
}

void
default_getline(uint64 _ln, Buf *_b, void *_arg)
{
}

static void
set_font(Med *w, HWND hwnd, HFONT font)
{
	HDC dc = GetDC(hwnd);
	SelectObject(dc, font);
	TEXTMETRIC tm;
	GetTextMetrics(dc, &tm);
	ReleaseDC(hwnd, dc);
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
		w = calloc(1, sizeof *w);
		if (!w) return FALSE;
		w->getline = default_getline;
		w->bgbrush = (HBRUSH) GetClassLongPtr(hwnd, GCLP_HBRBACKGROUND);
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
		/* This check is necessary, since we reach here even
		   when WM_NCCREATE returns FALSE (which means we failed to
		   allocate 'w'). */
		if (w) {
			for (int i=0; i<w->nrow; i++) {
				free(w->buffer[i].text);
			}
			free(w->buffer);
			free(w->tagbuf);
			free(w);
		}
		return 0;
	case WM_PAINT:
		paint(w, hwnd);
		return 0;
	case WM_SETFONT:
		set_font(w, hwnd, (HFONT) wparam);
		return 0;
	case WM_SETFOCUS:
		_printf("SETFOCUS\n");
		CreateCaret(hwnd, 0, w->charwidth, w->charheight);
		update_caret_pos(w);
		ShowCaret(hwnd);
		return 0;
	case WM_KILLFOCUS:
		_printf("KILLFOCUS\n");
		HideCaret(hwnd);
		DestroyCaret();
		return 0;
	case WM_CHAR:
		/* forward WM_CHAR to parent */
		SendMessage(GetParent(hwnd), WM_CHAR, wparam, lparam);
		return 0;
	case WM_ERASEBKGND:
		/* pretend that the background has been erased in order to
		   prevent flickering */
		return TRUE;
	case WM_SIZE:
		if (wparam != SIZE_MINIMIZED) {
			int wid = LOWORD(lparam);
			int hei = HIWORD(lparam);
			w->clientwidth = wid;
			w->clientheight = hei;
			int ch = w->charheight;
			int cw = w->charwidth;
			int new_nrow = hei/ch;
			int new_ncol = wid/cw;
			set_size(w, new_nrow, new_ncol);
			InvalidateRect(hwnd, 0, 0);
		}
		return 0;
	case WM_MOUSEWHEEL:
		{
			short delta = HIWORD(wparam);
			if (delta > 0) {
				int n = delta / WHEEL_DELTA;
				while (n--) {
					scroll_up_line(w, hwnd);
				}
			} else {
				int n = (-delta) / WHEEL_DELTA;
				while (n--) {
					scroll_down_line(w, hwnd);
				}
			}
		}
notify:
		notify_parent(w, hwnd);
		return 0;
	case WM_LBUTTONDOWN:
		{
			int x = LOWORD(lparam);
			int y = HIWORD(lparam);
			w->cursorx = x / w->charwidth;
			w->cursory = y / w->charheight;
			if (SetFocus(hwnd) == hwnd) {
				update_caret_pos(w);
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
				scroll_to(w, hwnd, si.nTrackPos);
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
		case VK_HOME:
			w->cursorx = 0;
			update_caret_pos(w);
			goto notify;
		case VK_END:
			w->cursorx = max(w->ncol-1, 0);
			update_caret_pos(w);
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
	w->current_line = ln;
	update_buffer(w);
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
free_line(Med *w, Line *l)
{
	free_text(w, l->text, l->textlen);
}

static void
init_textbuf(Med *w, int len)
{
	w->textbuf = malloc(len);
	FreeList *f = (FreeList *) w->textbuf;
	f->len = len;
	f->next = len+8;
}

#endif

static void
getline(Med *w, int row)
{
	HeapBuf hb;
	Line *l = &w->buffer[row];
	if (init_heapbuf(&hb)) {
		l->text = 0;
		l->textlen = 0;
		return;
	}
	w->getline(w->current_line + row, &hb.buf, w->getline_arg);
	l->text = hb.start;
	l->textlen = hb.cur - hb.start;
}

static void
update_buffer(Med *w)
{
	for (int i=0; i<w->nrow; i++) {
		Line *l = &w->buffer[i];
		free(l->text);
		getline(w, i);
	}
}

void
med_update_buffer(HWND hwnd)
{
	Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
	update_buffer(w);
}

static void
scroll(Med *w, HWND hwnd, int delta)
{
	RECT scroll_rect = {
		0,
		0,
		w->charwidth * w->ncol,
		w->charheight * w->nrow
	};
#if 0
	ScrollWindow(hwnd, 0, delta*w->charheight,
		     &scroll_rect, &scroll_rect);
#else
	InvalidateRect(hwnd, 0, 0);
#endif
	w->current_line -= delta;
	if (delta > 0) {
		if (delta > w->nrow) delta = w->nrow;
		int d = w->nrow-1;
		int s = d - delta;
		for (int i=w->nrow-delta; i<w->nrow; i++) {
			free(w->buffer[i].text);
		}
		while (s >= 0) {
			w->buffer[d--] = w->buffer[s--];
		}
		for (int i=0; i<delta; i++) {
			getline(w, i);
		}
	} else if (delta < 0) {
		delta = -delta;
		if (delta > w->nrow) delta = w->nrow;
		int d = 0;
		int s = delta;
		for (int i=0; i<delta; i++) {
			free(w->buffer[i].text);
		}
		while (s < w->nrow) {
			w->buffer[d++] = w->buffer[s++];
		}
		for (int i=w->nrow-delta; i<w->nrow; i++) {
			getline(w, i);
		}
	}
	update_scrollbar_pos(w, hwnd);
}

void
med_scroll(HWND hwnd, int delta)
{
	Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
	scroll(w, hwnd, delta);
}

void
med_add_tag(HWND hwnd, int ln, MedTag *tag)
{
	Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
	add_tag(w, ln, tag);
}

void
med_clear_tags(HWND hwnd)
{
	Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
	clear_tags(w);
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
	assert(nrow >= 0);
	assert(ncol >= 0);
	w->ncol = ncol;
	if (nrow == w->nrow) return;
	if (nrow < w->nrow) {
		for (int i=nrow; i<w->nrow; i++) {
			free(w->buffer[i].text);
		}
	}
	w->buffer = realloc(w->buffer, nrow * sizeof *w->buffer);
	if (nrow > w->nrow) {
		memset(&w->buffer[w->nrow], 0,
		       (nrow - w->nrow) * sizeof w->buffer[0]);
		for (int i=w->nrow; i<nrow; i++) {
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

void
med_paint_row(HWND hwnd, int row)
{
	Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
	HDC dc = GetDC(hwnd);
	SelectObject(dc, w->bgbrush);
	SelectObject(dc, w->font);
	paint_row(w, hwnd, dc, row);
	ReleaseDC(hwnd, dc);
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
	while (bottom >> s >= 0x80000000) s++;
	w->sbscale = s;
	int maxscroll = (int)(bottom >> s);
	int newpos = compute_scrollbar_pos(w);
	SCROLLINFO si;
	si.cbSize = sizeof si;
	si.fMask = SIF_RANGE | SIF_POS;
	si.nMin = 0;
	si.nMax = maxscroll;
	si.nPos = newpos;
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
