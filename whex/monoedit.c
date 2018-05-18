#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "monoedit.h"

typedef struct {
	/* in characters */
	int ncol;
	int nrow;
	TCHAR *buffer;
	HFONT font;
	int cursorx;
	int cursory;
	int charwidth;
	int charheight;
	MedTag *tags;
	int tag_len;
	int tag_cap;
} Med;

static void
med_paint(Med *w, HWND hwnd)
{
	PAINTSTRUCT paint;
	HDC hdc = BeginPaint(hwnd, &paint);
	HBRUSH bgbrush = (HBRUSH) GetClassLongPtr(hwnd, GCLP_HBRBACKGROUND);
	RECT r;

	GetClientRect(hwnd, &r);
	if (w->buffer) {
		HGDIOBJ old_font = 0; // intial value not used, just to placate compiler
		COLORREF old_bkcolor;
		if (w->font) {
			old_font = SelectObject(hdc, w->font);
		}
		MedTag *segments =
			malloc((w->nrow + 2*w->tag_len) * sizeof *segments);
		int n_seg = 0;
		int next_tag_i = 0;
		int line = 0;
		int col = 0;
		while (line < w->nrow) {
			segments[n_seg].line = line;
			segments[n_seg].start = col;
			segments[n_seg].attr = 0;
			if (next_tag_i < w->tag_len &&
			    w->tags[next_tag_i].line == line) {
				MedTag *next_tag = &w->tags[next_tag_i];
				// untagged segment
				segments[n_seg++].len = next_tag->start - col;
				// tagged segment
				segments[n_seg++] = *next_tag;
				next_tag_i++;
				col = next_tag->start + next_tag->len;
			} else {
				segments[n_seg++].len = w->ncol - col;
				col = w->ncol;
			}
			if (col == w->ncol) {
				line++;
				col = 0;
			}
		}
		const TCHAR *pbuf = w->buffer;
		for (int i=0; i<n_seg; i++) {
			MedTag *seg = &segments[i];
#if 0
			printf("seg[%d].line = %d\n", i, seg->line);
			printf("seg[%d].start = %d\n", i, seg->start);
			printf("seg[%d].len = %d\n", i, seg->len);
			printf("seg[%d].attr = %u\n", i, seg->attr);
#endif
			if (seg->attr) {
				old_bkcolor = SetBkColor
					(hdc, RGB(204, 204, 204));
			}
			TextOut(hdc,
				seg->start * w->charwidth,
				seg->line * w->charheight,
				pbuf,
				seg->len);
			pbuf += seg->len;
			if (seg->attr) {
				SetBkColor(hdc, old_bkcolor);
			}
		}
		free(segments);
		if (w->font) {
			SelectObject(hdc, old_font);
		}
		int textw = w->ncol * w->charwidth;
		int texth = w->nrow * w->charheight;
		r.left += textw;
		FillRect(hdc, &r, bgbrush);
		r.left = 0;
		r.right = textw;
		r.top = texth;
		FillRect(hdc, &r, bgbrush);
	} else {
		FillRect(hdc, &r, bgbrush);
	}

	EndPaint(hwnd, &paint);
}

void
med_clear_tags(Med *w)
{
	w->tag_len = 0;
}

void
med_add_tag(Med *w, MedTag *tag)
{
#if 0
	printf("med_add_tag() line=%d start=%d len=%d attr=%u\n",
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
med_wndproc(HWND hwnd,
		 UINT message,
		 WPARAM wparam,
		 LPARAM lparam)
{
	Med *w = (void *) GetWindowLongPtr(hwnd, 0);
	switch (message) {
	case WM_NCCREATE:
		w = calloc(1, sizeof *w);
		if (!w) {
			return FALSE;
		}
		SetWindowLongPtr(hwnd, 0, (LONG_PTR) w);
		break;
	case WM_NCDESTROY:
		if (w) {
			/* This check is necessary, since we reach here even
			   when WM_NCCREATE returns FALSE (which means we
			   failed to allocate w). */
			free(w);
		}
		return 0;
	case WM_PAINT:
		med_paint(w, hwnd);
		return 0;
	case MED_WM_SCROLL:
		{
			int delta = (int) lparam;
			RECT scroll_rect = {
			       	0,
				0,
				w->charwidth * w->ncol,
				w->charheight * w->nrow
			};
			ScrollWindow(hwnd, 0, -delta*w->charheight,
				     &scroll_rect, &scroll_rect);
		}
		return 0;
	case MED_WM_SET_BUFFER:
		w->buffer = (void *) lparam;
		return 0;
	case MED_WM_SET_CSIZE:
		{
			int ncol  = (int) wparam;
			int nrow = (int) lparam;
			if (ncol >= 0) {
				w->ncol = ncol;
			}
			if (nrow >= 0) {
				w->nrow = nrow;
			}
		}
		return 0;
	case MED_WM_CLEAR_TAGS:
		med_clear_tags(w);
		return 0;
	case MED_WM_ADD_TAG:
		med_add_tag(w, (MedTag *) lparam);
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
	case MED_WM_SET_CURSOR_POS:
		{
			int cx = (int) wparam;
			int cy = (int) lparam;
			w->cursorx = cx;
			w->cursory = cy;
			SetCaretPos(cx*w->charwidth, cy*w->charheight);
		}
		return 0;
	case WM_CHAR:
		{
			/* forward WM_CHAR to parent */
			HWND parent = GetParent(hwnd);
			SendMessage(parent, WM_CHAR, wparam, lparam);
			return 0;
		}
	case WM_ERASEBKGND:
		/* pretend that the background has been erased in order to
		   prevent flickering */
		return TRUE;
	}
	return DefWindowProc(hwnd, message, wparam, lparam);
}

ATOM
med_register_class(void)
{
	WNDCLASS wndclass = {0};
	wndclass.style = CS_GLOBALCLASS;
	wndclass.lpfnWndProc = med_wndproc;
	wndclass.cbWndExtra = sizeof(LONG_PTR);
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	//CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	wndclass.lpszClassName = TEXT("MonoEdit");
	return RegisterClass(&wndclass);
}
