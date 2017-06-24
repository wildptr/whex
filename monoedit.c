#include "monoedit.h"

#include <stdlib.h>
#include <string.h>

struct monoedit {
	/* in characters */
	int ncols;
	int nrows;
	const char *buffer;
	HFONT font;
	int cursorx;
	int cursory;
	int charwidth;
	int charheight;
	struct tag *tags;
	int tag_len;
	int tag_cap;
};

static void monoedit_paint(struct monoedit *w, HWND hwnd)
{
	PAINTSTRUCT paint;
	HDC hdc = BeginPaint(hwnd, &paint);

	if (w->buffer) {
		HGDIOBJ old_font;
		COLORREF old_bkcolor;
		if (w->font) {
			old_font = SelectObject(hdc, w->font);
		}
		struct tag *segments = malloc((w->nrows + 2*w->tag_len) * sizeof *segments);
		int n_seg = 0;
		int next_tag_i = 0;
		int line = 0;
		int col = 0;
		while (line < w->nrows) {
			segments[n_seg].line = line;
			segments[n_seg].start = col;
			segments[n_seg].attr = 0;
			if (next_tag_i < w->tag_len && w->tags[next_tag_i].line == line) {
				struct tag *next_tag = &w->tags[next_tag_i];
				// untagged segment
				segments[n_seg++].len = next_tag->start - col;
				// tagged segment
				segments[n_seg++] = *next_tag;
				next_tag_i++;
				col = next_tag->start + next_tag->len;
			} else {
				segments[n_seg++].len = w->ncols - col;
				col = w->ncols;
			}
			if (col == w->ncols) {
				line++;
				col = 0;
			}
		}
		const char *pbuf = w->buffer;
		for (int i=0; i<n_seg; i++) {
			struct tag *seg = &segments[i];
#if 0
			printf("seg[%d].line = %d\n", i, seg->line);
			printf("seg[%d].start = %d\n", i, seg->start);
			printf("seg[%d].len = %d\n", i, seg->len);
			printf("seg[%d].attr = %u\n", i, seg->attr);
#endif
			if (seg->attr) {
				old_bkcolor = SetBkColor(hdc, RGB(204, 204, 204));
			}
			TextOut(hdc, seg->start * w->charwidth, seg->line * w->charheight,
				pbuf, seg->len);
			pbuf += seg->len;
			if (seg->attr) {
				SetBkColor(hdc, old_bkcolor);
			}
		}
		free(segments);
		if (w->font) {
			SelectObject(hdc, old_font);
		}
	}

	EndPaint(hwnd, &paint);
}

void monoedit_clear_tags(struct monoedit *w)
{
	w->tag_len = 0;
}

void monoedit_add_tag(struct monoedit *w, struct tag *tag)
{
#if 0
	printf("monoedit_add_tag() line=%d start=%d len=%d attr=%u\n",
	       tag->line, tag->start, tag->len, tag->attr);
#endif
	if (w->tag_len == w->tag_cap) {
		if (w->tags) {
			w->tag_cap *= 2;
			w->tags = realloc(w->tags, w->tag_cap * sizeof *w->tags);
		} else {
			w->tag_cap = 4;
			w->tags = malloc(w->tag_cap * sizeof *w->tags);
		}
	}
	w->tags[w->tag_len++] = *tag;
}

static LRESULT CALLBACK
monoedit_wndproc(HWND hwnd,
		 UINT message,
		 WPARAM wparam,
		 LPARAM lparam)
{
	struct monoedit *w = (void *) GetWindowLong(hwnd, 0);
	switch (message) {
	case WM_NCCREATE:
		w = calloc(1, sizeof *w);
		if (!w) {
			return FALSE;
		}
		SetWindowLong(hwnd, 0, (LONG) w);
		return TRUE;
	case WM_NCDESTROY:
		if (w) {
			/* This check is necessary, since we reach here even
			 * when WM_NCCREATE returns FALSE (which means we
			 * failed to allocate w). */
			free(w);
		}
		return 0;
	case WM_PAINT:
		monoedit_paint(w, hwnd);
		return 0;
	case MONOEDIT_WM_SCROLL:
		{
			int delta = (int) lparam;
			RECT scroll_rect = {
			       	0,
				0,
				w->charwidth * w->ncols,
				w->charheight * w->nrows
			};
			ScrollWindow(hwnd, 0, -delta*w->charheight, &scroll_rect, &scroll_rect);
		}
		return 0;
	case MONOEDIT_WM_SET_BUFFER:
		w->buffer = (void *) lparam;
		return 0;
	case MONOEDIT_WM_SET_CSIZE:
		{
			int ncols  = (int) wparam;
			int nrows = (int) lparam;
			if (ncols >= 0) {
				w->ncols = ncols;
			}
			if (nrows >= 0) {
				w->nrows = nrows;
			}
		}
		return 0;
	case MONOEDIT_WM_CLEAR_TAGS:
		monoedit_clear_tags(w);
		return 0;
	case MONOEDIT_WM_ADD_TAG:
		monoedit_add_tag(w, (struct tag *) lparam);
		return 0;
	case WM_SETFONT:
		{
			w->font = (HFONT) wparam;
			HDC dc = GetDC(hwnd);
			SelectObject(dc, w->font);
			TEXTMETRIC tm;
			GetTextMetrics(dc, &tm);
			ReleaseDC(hwnd, dc);
			w->charwidth = tm.tmAveCharWidth;
			w->charheight = tm.tmHeight;
		}
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
	case MONOEDIT_WM_SET_CURSOR_POS:
		{
			int cursorx = (int) wparam;
			int cursory = (int) lparam;
			w->cursorx = cursorx;
			w->cursory = cursory;
			SetCaretPos(cursorx*w->charwidth, cursory*w->charheight);
		}
		return 0;
	case WM_CHAR:
		{
			/* forward WM_CHAR to parent */
			HWND parent = GetParent(hwnd);
			SendMessage(parent, WM_CHAR, wparam, lparam);
			return 0;
		}
	}
	return DefWindowProc(hwnd, message, wparam, lparam);
}

ATOM monoedit_register_class(void)
{
	WNDCLASS wndclass = {0};
	wndclass.style = CS_GLOBALCLASS;
	wndclass.lpfnWndProc = monoedit_wndproc;
	wndclass.cbWndExtra = sizeof(long);
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	wndclass.lpszClassName = "MonoEdit";
	return RegisterClass(&wndclass);
}
