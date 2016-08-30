#include "monoedit.h"

#include <stdlib.h>
#include <string.h>

/* character size */
#define CW  8
#define CH 16

struct monoedit_state {
	/* in characters */
	int cwidth;
	int cheight;
	const char *buffer;
	HFONT font;
	int cursorx;
	int cursory;
};

static void monoedit_paint(struct monoedit_state *s, HWND hwnd)
{
	PAINTSTRUCT paint;
	HDC hdc = BeginPaint(hwnd, &paint);

#if 0
	if (s->getline) {
		RECT rect;
		GetClientRect(hwnd, &rect);

		int height = rect.bottom - rect.top;
		int nc = height / CH;

		int start = s->top_line;
		int end = s->total_lines;

		/* determine number of lines to paint */
		int n = end-start;
		if (n > nc) n = nc;

		HGDIOBJ old_font = SelectObject(hdc, GetStockObject(OEM_FIXED_FONT));

		for (int i=0; i<n; i++) {
			const char *line = s->getline(start+i);
			TextOut(hdc, 0, CH*i, line, strlen(line));
		}

		SelectObject(hdc, old_font);
	}
#endif
	if (s->buffer) {
		HGDIOBJ old_font;
		if (s->font) {
			old_font = SelectObject(hdc, s->font);
		}
		const char *bufp = s->buffer;
		for (int i=0; i<s->cheight; i++) {
			TextOut(hdc, 0, CH*i, bufp, s->cwidth);
			bufp += s->cwidth;
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
#if 0
	case MONOEDIT_WM_SET_GETLINE_FUNC:
		state->getline = (void *) lparam;
		return 0;
	case MONOEDIT_WM_SET_TOTAL_LINES:
		state->total_lines = (int) lparam;
		return 0;
#endif
	case MONOEDIT_WM_SCROLL:
		{
			int delta = (int) lparam;
			RECT scroll_rect = {
			       	0,
				0,
				CW * state->cwidth,
				CH * state->cheight
			};
			ScrollWindow(hwnd, 0, -delta*CH, &scroll_rect, &scroll_rect);
		}
		return 0;
#if 0
	case MONOEDIT_WM_GET_NUM_LINES:
		{
			RECT rect;
			GetClientRect(hwnd, &rect);
			return (rect.bottom - rect.top) / CH;
		}
#endif
	case MONOEDIT_WM_SET_BUFFER:
		state->buffer = (void *) lparam;
		return 0;
	case MONOEDIT_WM_SET_CSIZE:
		{
			int cwidth  = (int) wparam;
			int cheight = (int) lparam;
			if (cwidth >= 0) {
				state->cwidth = cwidth;
			}
			if (cheight >= 0) {
				state->cheight = cheight;
			}
		}
		return 0;
	case WM_SETFONT:
		state->font = (HFONT) wparam;
		return 0;
	case WM_SETFOCUS:
		CreateCaret(hwnd, 0, CW, CH);
		SetCaretPos(state->cursorx*CW, state->cursory*CH);
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
			SetCaretPos(cursorx*CW, cursory*CH);
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
