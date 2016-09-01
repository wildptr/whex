#include "monoedit.h"

#include <stdlib.h>
#include <string.h>

struct monoedit_state {
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

static void monoedit_paint(struct monoedit_state *s, HWND hwnd)
{
	PAINTSTRUCT paint;
	HDC hdc = BeginPaint(hwnd, &paint);

	if (s->buffer) {
		HGDIOBJ old_font;
		if (s->font) {
			old_font = SelectObject(hdc, s->font);
		}
		const char *bufp = s->buffer;
		for (int i=0; i<s->nrows; i++) {
			TextOut(hdc, 0, s->charheight*i, bufp, s->ncols);
			bufp += s->ncols;
		}
		if (s->font) {
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
	struct monoedit_state *state = (void *) GetWindowLong(hwnd, 0);
	switch (message) {
		int printf(const char *fmt, ...);
	case WM_NCCREATE:
		state = calloc(1, sizeof *state);
		if (!state) {
			return FALSE;
		}
		SetWindowLong(hwnd, 0, (LONG) state);
		return TRUE;
	case WM_NCDESTROY:
		if (state) {
			/* This check is necessary, since we reach here even
			 * when WM_NCCREATE returns FALSE (which means we
			 * failed to allocate state). */
			free(state);
		}
		return 0;
	case WM_PAINT:
		monoedit_paint(state, hwnd);
		return 0;
	case MONOEDIT_WM_SCROLL:
		{
			int delta = (int) lparam;
			RECT scroll_rect = {
			       	0,
				0,
				state->charwidth * state->ncols,
				state->charheight * state->nrows
			};
			ScrollWindow(hwnd, 0, -delta*state->charheight, &scroll_rect, &scroll_rect);
		}
		return 0;
	case MONOEDIT_WM_SET_BUFFER:
		state->buffer = (void *) lparam;
		return 0;
	case MONOEDIT_WM_SET_CSIZE:
		{
			int ncols  = (int) wparam;
			int nrows = (int) lparam;
			if (ncols >= 0) {
				state->ncols = ncols;
			}
			if (nrows >= 0) {
				state->nrows = nrows;
			}
		}
		return 0;
	case WM_SETFONT:
		{
			state->font = (HFONT) wparam;
			HDC dc = GetDC(hwnd);
			SelectObject(dc, state->font);
			TEXTMETRIC tm;
			GetTextMetrics(dc, &tm);
			ReleaseDC(hwnd, dc);
			state->charwidth = tm.tmAveCharWidth;
			state->charheight = tm.tmHeight;
		}
		return 0;
	case WM_SETFOCUS:
		CreateCaret(hwnd, 0, state->charwidth, state->charheight);
		SetCaretPos(state->cursorx*state->charwidth, state->cursory*state->charheight);
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
			state->cursorx = cursorx;
			state->cursory = cursory;
			SetCaretPos(cursorx*state->charwidth, cursory*state->charheight);
		}
		return 0;
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
