#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "monoedit.h"

typedef struct {
	/* in characters */
	int ncol;
	int nrow;
	MedLine *buffer;
	HFONT font;
	int cursorx;
	int cursory;
	int charwidth;
	int charheight;
	MedGetLineProc getline;
	void *getline_arg;
	long long lineno;
	MedTag *tagbuf;
	int ntag;
	int tag_cap;
	int clientwidth;
	int clientheight;
	HBRUSH bgbrush;
} Med;

typedef struct {
	int len, next;
} FreeList;

static void set_source(Med *w, MedGetLineProc proc, void *arg);
#if 0
static TCHAR *alloc_text(Med *w, int nch);
static void free_text(Med *w, TCHAR *text, int nch);
static void init_textbuf(Med *w, int len);
static void free_line(Med *w, MedLine *l);
#endif
static void update_buffer(Med *w);
static void scroll(Med *w, HWND hwnd, int delta);
static void set_size(Med *w, int nrow, int ncol);

static void
paint_row(Med *w, HWND hwnd, HDC dc, int row)
{
	assert(row >= 0 && row < w->nrow);
	MedLine *line = &w->buffer[row];
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
			nextstart = line->textlen;
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
		assert(seg->start + seg->len <= line->textlen);
		TextOut(dc,
			seg->start * w->charwidth,
			y,
			line->text + seg->start,
			seg->len);
		if (seg->attr) {
			SetBkColor(dc, old_bkcolor);
		}
	}
	free(segments);
	/* fill right margin */
	RECT r;
	r.left = w->charwidth * line->textlen;
	r.right = w->clientwidth;
	r.top = y;
	r.bottom = y + w->charheight;
	FillRect(dc, &r, w->bgbrush);
}

static void
paint(Med *w, HWND hwnd)
{
	PAINTSTRUCT paint;
	HDC dc = BeginPaint(hwnd, &paint);
	RECT r = { 0, 0, w->clientwidth, w->clientheight };

	if (w->buffer) {
		SelectObject(dc, w->font);
		for (int i=0; i<w->nrow; i++) {
			paint_row(w, hwnd, dc, i);
		}
		r.top = w->nrow * w->charheight;
	}
	FillRect(dc, &r, w->bgbrush);

	EndPaint(hwnd, &paint);
}

static void
clear_tags(Med *w)
{
	for (int i=0; i<w->nrow; i++) {
		MedLine *line = &w->buffer[i];
		line->tags = 0;
	}
	w->ntag = 0;
}

static void
add_tag(Med *w, int lineno, MedTag *tag)
{
	if (!(lineno >= 0 && lineno < w->nrow)) {
		abort();
	}
	MedLine *line = &w->buffer[lineno];
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
default_getline(long long ln, MedLine *line, void *_arg)
{
	memset(line, 0, sizeof *line);
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
			bool has_font = 0;
			MedConfig *conf = cs->lpCreateParams;
			if (conf) {
				if (conf->mask & MED_CONFIG_GETLINE) {
					set_source(w, conf->getline, conf->getline_arg);
				}
				if (conf->mask & MED_CONFIG_FONT) {
					has_font = 1;
					set_font(w, hwnd, (HFONT) conf->font);
				}
			}
			if (!has_font) {
				set_font(w, hwnd,
					 (HFONT) GetStockObject(ANSI_FIXED_FONT));
			}
		}
		SetWindowLongPtr(hwnd, 0, (LONG_PTR) w);
		return TRUE;
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
		CreateCaret(hwnd, 0, w->charwidth, w->charheight);
		SetCaretPos(w->cursorx*w->charwidth, w->cursory*w->charheight);
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
		/* pretend that the background has been erased in order to
		   prevent flickering */
		return TRUE;
	case WM_SIZE:
		if (wparam != SIZE_MINIMIZED) {
			int wid = LOWORD(lparam);
			int hei = HIWORD(lparam);
			w->clientwidth = wid;
			w->clientheight = hei;
			set_size(w, hei/w->charheight, wid/w->charwidth);
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
	//CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	wndclass.lpszClassName = TEXT("MonoEdit");
	return RegisterClass(&wndclass);
}

void
med_set_current_line(HWND hwnd, long long ln)
{
	Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
	w->lineno = ln;
	update_buffer(w);
}

long long
med_get_current_line(HWND hwnd)
{
	Med *w = (Med *) GetWindowLongPtr(hwnd, 0);
	return w->lineno;
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
free_line(Med *w, MedLine *l)
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
update_buffer(Med *w)
{
	for (int i=0; i<w->nrow; i++) {
		MedLine *l = &w->buffer[i];
		free(l->text);
		w->getline(w->lineno + i, l, w->getline_arg);
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
	ScrollWindow(hwnd, 0, delta*w->charheight,
		     &scroll_rect, &scroll_rect);
	w->lineno -= delta;
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
			w->getline(w->lineno + i, &w->buffer[i],
				   w->getline_arg);
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
			w->getline(w->lineno + i, &w->buffer[i],
				   w->getline_arg);
		}
	}
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
	w->buffer = realloc(w->buffer, nrow * sizeof *w->buffer);
	if (nrow > w->nrow) {
		memset(&w->buffer[w->nrow], 0,
		       (nrow - w->nrow) * sizeof w->buffer[0]);
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
	MedLine *l;
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
	SelectObject(dc, w->font);
	paint_row(w, hwnd, dc, row);
	ReleaseDC(hwnd, dc);
}
