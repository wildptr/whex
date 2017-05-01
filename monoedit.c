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
};

static void monoedit_paint(struct monoedit *w, HWND hwnd)
{
	PAINTSTRUCT paint;
	HDC hdc = BeginPaint(hwnd, &paint);

	if (w->buffer) {
		HGDIOBJ old_font;
		if (w->font) {
			old_font = SelectObject(hdc, w->font);
		}
		const char *bufp = w->buffer;
		for (int i=0; i<w->nrows; i++) {
			TextOut(hdc, 0, w->charheight*i, bufp, w->ncols);
			bufp += w->ncols;
		}
		if (w->font) {
			SelectObject(hdc, old_font);
		}
	}

	EndPaint(hwnd, &paint);
}

static LRESULT CALLBACK
monoedit_wndproc(HWND hwnd,
		 UINT message,
		 WPARAM wparam,
		 LPARAM lparam)
{
	struct monoedit *st = (void *) GetWindowLong(hwnd, 0);
	switch (message) {
	case WM_NCCREATE:
		st = calloc(1, sizeof *st);
		if (!st) {
			return FALSE;
		}
		SetWindowLong(hwnd, 0, (LONG) st);
		return TRUE;
	case WM_NCDESTROY:
		if (st) {
			/* This check is necessary, since we reach here even
			 * when WM_NCCREATE returns FALSE (which means we
			 * failed to allocate st). */
			free(st);
		}
		return 0;
	case WM_PAINT:
		monoedit_paint(st, hwnd);
		return 0;
	case MONOEDIT_WM_SCROLL:
		{
			int delta = (int) lparam;
			RECT scroll_rect = {
			       	0,
				0,
				st->charwidth * st->ncols,
				st->charheight * st->nrows
			};
			ScrollWindow(hwnd, 0, -delta*st->charheight, &scroll_rect, &scroll_rect);
		}
		return 0;
	case MONOEDIT_WM_SET_BUFFER:
		st->buffer = (void *) lparam;
		return 0;
	case MONOEDIT_WM_SET_CSIZE:
		{
			int ncols  = (int) wparam;
			int nrows = (int) lparam;
			if (ncols >= 0) {
				st->ncols = ncols;
			}
			if (nrows >= 0) {
				st->nrows = nrows;
			}
		}
		return 0;
	case WM_SETFONT:
		{
			st->font = (HFONT) wparam;
			HDC dc = GetDC(hwnd);
			SelectObject(dc, st->font);
			TEXTMETRIC tm;
			GetTextMetrics(dc, &tm);
			ReleaseDC(hwnd, dc);
			st->charwidth = tm.tmAveCharWidth;
			st->charheight = tm.tmHeight;
		}
		return 0;
	case WM_SETFOCUS:
		CreateCaret(hwnd, 0, st->charwidth, st->charheight);
		SetCaretPos(st->cursorx*st->charwidth, st->cursory*st->charheight);
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
			st->cursorx = cursorx;
			st->cursory = cursory;
			SetCaretPos(cursorx*st->charwidth, cursory*st->charheight);
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

BOOL monoedit_unregister_class(void)
{
	return UnregisterClass("MonoEdit", 0);
}
