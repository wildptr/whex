#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "mainwindow.h"
#include "monoedit.h"

#define INITIAL_N_ROW 32
#define LOG2_N_COL 5
#define N_COL (1<<LOG2_N_COL)
#define N_COL_CHAR (16+4*N_COL)

static bool iswordchar(char c)
{
	return isalnum(c) || c == '_';
}

bool mainwindow_cache_valid(struct mainwindow *w, int block)
{
	return w->cache[block].tag & 1;
};

int mainwindow_find_cache(struct mainwindow *w, long long address)
{
	static int next_cache = 0;

	for (int i=0; i<N_CACHE_BLOCK; i++) {
		long long tag = w->cache[i].tag;
		if ((tag & 1) && address >> 12 == tag >> 12) return i;
	}

	long long base = address & -0x1000;
	long long tag = base|1;
	SetFilePointer(w->file, base, 0, FILE_BEGIN);
	DWORD nread;
	int ret = next_cache;
	ReadFile(w->file, w->cache[ret].data, 0x1000, &nread, 0);
	w->cache[ret].tag = tag;
	next_cache = (ret+1)&(N_CACHE_BLOCK-1);

	DEBUG_PRINTF("loaded %I64x into cache block %d\n", base, ret);

	return ret;
}

uint8_t mainwindow_getbyte(struct mainwindow *w, long long address)
{
	int block = mainwindow_find_cache(w, address);
	return w->cache[block].data[address & 0xfff];
}

static void kmp_table(int *T, const uint8_t *pat, int len)
{
	int pos = 2;
	int cnd = 0;
	// forall i:nat, 0 < i < len ->
	// T[i] < i /\ ...
	// pat[i-T[i]:i] = pat[0:T[i]] /\ ...
	// forall j:nat, T[i] < j < len -> pat[i-j:i] <> pat[0:j]
	T[1] = 0; // T[0] is undefined
	while (pos < len) {
		if (pat[pos-1] == pat[cnd]) {
			T[pos++] = cnd+1;
			cnd++;
		} else {
			// pat[pos-1] != pat[cnd]
			if (cnd > 0) {
				cnd = T[cnd];
			} else {
				T[pos++] = 0;
			}
		}
	}
}

long long mainwindow_kmp_search(struct mainwindow *w, const uint8_t *pat, int len, long long start)
{
	assert(len);
	int T[len];
	kmp_table(T, pat, len);
	long long m = start; // start of potential match
	int i = 0;
	while (m+i < w->file_size) {
		if (pat[i] == mainwindow_getbyte(w, m+i)) {
			if (i == len-1) return m; // match found
			i++;
		} else {
			// current character does not match
			if (i) {
				m += i-T[i];
				i = T[i];
			} else {
				m++;
			}
		}
	}
	/* no match */
	return w->file_size;
}

/*
 * ...|xx|x..
 * position of first mark = start
 * number of bytes after the second mark = N-(start+len-1)
 */
long long mainwindow_kmp_search_backward(struct mainwindow *w, const uint8_t *pat, int len, long long start)
{
	assert(len);
	int T[len];
	uint8_t revpat[len];
	for (int i=0; i<len; i++) {
		revpat[i] = pat[len-1-i];
	}
	pat = revpat;
	kmp_table(T, pat, len);
	long long m = start;
	int i = 0;
	while (m+i < w->file_size) {
		if (pat[i] == mainwindow_getbyte(w, w->file_size-1-(m+i))) {
			if (i == len-1) return w->file_size-(m+len);
			i++;
		} else {
			if (i) {
				m += i-T[i];
				i = T[i];
			} else {
				m++;
			}
		}
	}
	/* no match */
	return w->file_size;
}

void mainwindow_update_monoedit_buffer(struct mainwindow *w, int buffer_line, int num_lines)
{
	long long abs_line = w->current_line + buffer_line;
	long long abs_line_end = abs_line + num_lines;
	while (abs_line < abs_line_end) {
		char *p = &w->monoedit_buffer[buffer_line*N_COL_CHAR];
		long long address = abs_line << LOG2_N_COL;
		if (abs_line >= w->total_lines) {
			memset(p, ' ', N_COL_CHAR);
		} else {
			int block = mainwindow_find_cache(w, address);
			int base = address & 0xfff;
			sprintf(p, "%08x: ", address);
			p += 10;
			int end = 0;
		       	if (abs_line+1 >= w->total_lines) {
				end = w->file_size & (N_COL-1);
			}
			if (!end) {
				end = N_COL;
			}
			for (int j=0; j<end; j++) {
				sprintf(p, "%02x ", w->cache[block].data[base|j]);
				p += 3;
			}
			for (int j=end; j<N_COL; j++) {
				sprintf(p, "   ");
				p += 3;
			}
			sprintf(p, "  ");
			p += 2;
			for (int j=0; j<end; j++) {
				uint8_t b = w->cache[block].data[base|j];
				p[j] = b < 0x20 || b > 0x7e ? '.' : b;
			}
			for (int j=end; j<N_COL; j++) {
				p[j] = ' ';
			}
		}
		buffer_line++;
		abs_line++;
	}
}

static uint8_t hexval(char c)
{
	return c > '9' ? 10+(c|32)-'a' : c-'0';
}

static uint8_t hextobyte(const uint8_t *p)
{
	return hexval(p[0]) << 4 | hexval(p[1]);
}

void mainwindow_goto_line(struct mainwindow *w, long long line)
{
	assert(line <= w->total_lines);
	w->current_line = line;
	if (w->interactive) {
		mainwindow_update_monoedit_buffer(w, 0, w->nrows);
		mainwindow_update_monoedit_tags(w);
		InvalidateRect(w->monoedit, 0, FALSE);
	}
}

void mainwindow_update_cursor_pos(struct mainwindow *w)
{
	SendMessage(w->monoedit,
		    MONOEDIT_WM_SET_CURSOR_POS,
		    10+w->cursor_x*3,
		    w->cursor_y);
}

void mainwindow_goto_address(struct mainwindow *w, long long address)
{
	long long line = address >> LOG2_N_COL;
	int col = address & (N_COL-1);
	long long line1;
	assert(address <= w->file_size);
	if (line >= w->nrows >> 1) {
		line1 = line - (w->nrows >> 1);
	} else {
		line1 = 0;
	}
	mainwindow_goto_line(w, line1);
	SendMessage(w->monoedit, MONOEDIT_WM_SET_CURSOR_POS, 10+col*3, 0);
	w->cursor_pos = address;
	w->cursor_x = col;
	w->cursor_y = line - line1;
	mainwindow_update_cursor_pos(w);
}

char *mainwindow_find(struct mainwindow *w, char *arg, bool istext)
{
	int patlen;
	uint8_t *pat;
	uint8_t *p = (uint8_t *) arg;
	while (*p == ' ') p++;
	if (istext) {
		if (*p == '"') {
			/* TODO */
			return "TODO";
		} else {
			uint8_t *start = p;
			while (iswordchar(*p)) p++;
			if (*p) {
				/* trailing character(s) found */
				return "invalid argument";
			}
			patlen = p - start;
			pat = start;
		}
	} else {
		int slen = strlen((char*)p);
		if (slen&1 || !slen) {
			return "invalid argument";
		}
		patlen = slen >> 1;
		pat = malloc(patlen);
		for (int i=0; i<patlen; i++) {
			if (!(isxdigit(p[0]) && isxdigit(p[1]))) {
				return "invalid argument";
			}
			pat[i] = hextobyte(p);
			p += 2;
		}
	}
	if (w->last_search_pattern) {
		free(w->last_search_pattern);
	}
	w->last_search_pattern = malloc(patlen);
	memcpy(w->last_search_pattern, pat, patlen);
	w->last_search_pattern_len = patlen;
	long long matchpos = mainwindow_kmp_search(w, pat, patlen, w->cursor_pos);
	if (!istext) {
		free(pat);
	}
	if (matchpos == w->file_size) {
		return "pattern not found";
	}
	mainwindow_goto_address(w, matchpos);
	return 0;
}

char *mainwindow_repeat_search(struct mainwindow *w, bool reverse)
{
	if (!w->last_search_pattern) {
		return "no previous pattern";
	}
	long long (*search_func)(struct mainwindow *w, const uint8_t *pat, int patlen, long long start);
	long long start;
	if (reverse) {
		long long tmp = w->cursor_pos + w->last_search_pattern_len - 1;
		if (w->file_size >= tmp) {
			start = w->file_size - tmp;
		} else {
			return "pattern not found";
		}
		search_func = mainwindow_kmp_search_backward;
	} else {
		if (w->cursor_pos+1 < w->file_size) {
			start = w->cursor_pos+1;
		} else {
			return "pattern not found";
			//start = 0;
		}
		search_func = mainwindow_kmp_search;
	}
	long long matchpos = search_func(w, w->last_search_pattern,
					w->last_search_pattern_len,
					start);
	if (matchpos == w->file_size) {
		return "pattern not found";
	}
	mainwindow_goto_address(w, matchpos);
	return 0;
}

void mainwindow_execute_command(struct mainwindow *w, cmdproc_t cmdproc, char *arg)
{
	const char *errmsg = cmdproc(w, arg);
	if (errmsg) {
		MessageBox(w->hwnd, errmsg, "Error", MB_ICONERROR);
	}
}

void mainwindow_scroll_up_line(struct mainwindow *w)
{
	if (w->current_line) {
		w->current_line--;
		w->cursor_pos -= N_COL;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, -1);
		memmove(w->monoedit_buffer+N_COL_CHAR, w->monoedit_buffer, N_COL_CHAR*(w->nrows-1));
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, 0, 1);
			mainwindow_update_monoedit_tags(w);
		}
	}
}

void mainwindow_scroll_down_line(struct mainwindow *w)
{
	if (w->cursor_pos + N_COL < w->file_size) {
		w->current_line++;
		w->cursor_pos += N_COL;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, 1);
		memmove(w->monoedit_buffer, w->monoedit_buffer+N_COL_CHAR, N_COL_CHAR*(w->nrows-1));
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, w->nrows-1, 1);
			mainwindow_update_monoedit_tags(w);
		}
	}
}

void mainwindow_move_up(struct mainwindow *w)
{
	if (w->cursor_pos >= N_COL) {
		w->cursor_pos -= N_COL;
		if (w->cursor_y) {
			w->cursor_y--;
			mainwindow_update_cursor_pos(w);
		} else {
			mainwindow_scroll_up_line(w);
		}
	}
}

void mainwindow_move_down(struct mainwindow *w)
{
	if (w->file_size >= N_COL && w->cursor_pos < w->file_size - N_COL) {
		w->cursor_pos += N_COL;
		if (w->cursor_y < w->nrows-1) {
			w->cursor_y++;
			mainwindow_update_cursor_pos(w);
		} else {
			mainwindow_scroll_down_line(w);
		}
	}
}

void mainwindow_move_left(struct mainwindow *w)
{
	if (w->cursor_x) {
		w->cursor_x--;
		w->cursor_pos--;
		mainwindow_update_cursor_pos(w);
	}
}

void mainwindow_move_right(struct mainwindow *w)
{
	if (w->cursor_x < (N_COL-1) && w->cursor_pos+1 < w->file_size) {
		w->cursor_x++;
		w->cursor_pos++;
		mainwindow_update_cursor_pos(w);
	}
}

void mainwindow_scroll_up_page(struct mainwindow *w)
{
	if (w->current_line >= w->nrows) {
		w->current_line -= w->nrows;
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, 0, w->nrows);
			mainwindow_update_monoedit_tags(w);
			InvalidateRect(w->monoedit, 0, FALSE);
		}
	} else {
		long long delta = w->current_line;
		w->current_line = 0;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, -delta);
		memmove(w->monoedit_buffer+N_COL_CHAR*delta, w->monoedit_buffer, N_COL_CHAR*(w->nrows-delta));
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, 0, delta);
			mainwindow_update_monoedit_tags(w);
		}
	}
}

void mainwindow_scroll_down_page(struct mainwindow *w)
{
	if (w->current_line + w->nrows <= w->total_lines) {
		w->current_line += w->nrows;
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, 0, w->nrows);
			mainwindow_update_monoedit_tags(w);
			InvalidateRect(w->monoedit, 0, FALSE);
		}
	} else {
		long long delta = w->total_lines - w->current_line;
		w->current_line = w->total_lines;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, delta);
		memmove(w->monoedit_buffer, w->monoedit_buffer+N_COL_CHAR*delta, N_COL_CHAR*(w->nrows-delta));
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, w->nrows-delta, delta);
			mainwindow_update_monoedit_tags(w);
		}
	}
}

void mainwindow_move_up_page(struct mainwindow *w)
{
	if (w->cursor_pos >= N_COL*w->nrows) {
		w->cursor_pos -= N_COL*w->nrows;
		mainwindow_scroll_up_page(w);
	}
}

void mainwindow_move_down_page(struct mainwindow *w)
{
	if (w->file_size >= N_COL*w->nrows && w->cursor_pos < w->file_size - N_COL*w->nrows) {
		w->cursor_pos += N_COL*w->nrows;
		mainwindow_scroll_down_page(w);
	}
}

#if 0
void mainwindow_add_char_to_command(struct mainwindow *w, char c)
{
	HWND cmdedit = w->cmdedit;
	int len = w->cmd_len;
	if (c == 8) {
		// backspace
		if (len > 0) {
			SendMessage(cmdedit, EM_SETSEL, len-1, len);
			SendMessage(cmdedit, EM_REPLACESEL, TRUE, (LPARAM) "");
			w->cmd[--w->cmd_len] = 0;
		}
	} else {
		char str[2] = {c};
		if (len < w->cmd_cap) {
			SendMessage(cmdedit, EM_SETSEL, len, len);
			SendMessage(cmdedit, EM_REPLACESEL, TRUE, (LPARAM) str);
			w->cmd[w->cmd_len++] = c;
		}
	}
}
#endif

int mainwindow_handle_char(struct mainwindow *w, int c)
{
	switch (c) {
		char buf[2];
	case '/':
	case ':':
	case '\\':
	case 'g':
		buf[0] = c;
		buf[1] = 0;
		SetWindowText(w->cmdedit, buf);
		SetFocus(w->cmdedit);
		// place caret at end of text
		SendMessage(w->cmdedit, EM_SETSEL, 1, 1);
		return 1;
	case 'N':
		mainwindow_execute_command(w, mainwindow_cmd_findprev, 0);
		return 1;
	case 'h':
		mainwindow_move_left(w);
		return 1;
	case 'j':
		mainwindow_move_down(w);
		return 1;
	case 'k':
		mainwindow_move_up(w);
		return 1;
	case 'l':
		mainwindow_move_right(w);
		return 1;
	case 'n':
		mainwindow_execute_command(w, mainwindow_cmd_findnext, 0);
		return 1;
	}
	return 1;
}

LRESULT CALLBACK
monoedit_wndproc(HWND hwnd,
		 UINT message,
		 WPARAM wparam,
		 LPARAM lparam)
{
	struct mainwindow *w = GetWindowLong(hwnd, GWL_USERDATA);

	switch (message) {
	case WM_LBUTTONDOWN:
		{
			int x = LOWORD(lparam);
			int y = HIWORD(lparam);
			SetFocus(hwnd);
			int cx = x / w->charwidth;
			int cy = y / w->charheight;
			if (cx >= 10 && cx < 10+N_COL*3) {
				cx = (cx-10)/3;
				long long pos = ((w->current_line + cy) << LOG2_N_COL) + cx;
				if (pos < w->file_size) {
					w->cursor_pos = pos;
					w->cursor_x = cx;
					w->cursor_y = cy;
					mainwindow_update_cursor_pos(w);
				}
			}
		}
		return 0;
	case WM_KEYDOWN:
		switch (wparam) {
		case VK_UP:
			mainwindow_move_up(w);
			break;
		case VK_DOWN:
			mainwindow_move_down(w);
			break;
		case VK_LEFT:
			mainwindow_move_left(w);
			break;
		case VK_RIGHT:
			mainwindow_move_right(w);
			break;
		case VK_PRIOR:
			mainwindow_move_up_page(w);
			break;
		case VK_NEXT:
			mainwindow_move_down_page(w);
			break;
		case VK_HOME:
			mainwindow_goto_address(w, 0);
			break;
		case VK_END:
			if (w->file_size) {
				mainwindow_goto_address(w, w->file_size-1);
			}
			break;
		}
		return 0;
	case WM_MOUSEWHEEL:
		{
			int delta = (short) HIWORD(wparam);
			if (delta > 0) {
				int n = delta / WHEEL_DELTA;
				while (n--) {
					mainwindow_scroll_up_line(w);
				}
			} else {
				int n = (-delta) / WHEEL_DELTA;
				while (n--) {
					mainwindow_scroll_down_line(w);
				}
			}
		}
		return 0;
	}
	return CallWindowProc(w->monoedit_wndproc, hwnd, message, wparam, lparam);
}

LRESULT CALLBACK
cmdedit_wndproc(HWND hwnd,
		 UINT message,
		 WPARAM wparam,
		 LPARAM lparam)
{
	struct mainwindow *w = GetWindowLong(hwnd, GWL_USERDATA);

	switch (message) {
	case WM_CHAR:
		switch (wparam) {
			char *cmd;
			int buf_len;
			const char *errmsg;
		case '\r':
			buf_len = GetWindowTextLength(hwnd)+1;
			cmd = malloc(buf_len);
			GetWindowText(hwnd, cmd, buf_len);
			errmsg = 0;
			switch (cmd[0]) {
			case '/':
				mainwindow_execute_command(w, mainwindow_cmd_find_text, cmd+1);
				break;
			case ':':
				errmsg = mainwindow_parse_and_execute_command(w, cmd+1);
				break;
			case '\\':
				mainwindow_execute_command(w, mainwindow_cmd_find_hex, cmd+1);
				break;
			case 'g':
				mainwindow_execute_command(w, mainwindow_cmd_goto, cmd+1);
				break;
			}
			free(cmd);
			if (errmsg) {
				MessageBox(w->hwnd, errmsg, "Error", MB_ICONERROR);
			}
			// fallthrough
		case 27: // escape
			SetWindowText(hwnd, "");
			SetFocus(w->monoedit);
			return 0;
		}
	}

	return CallWindowProc(w->cmdedit_wndproc, hwnd, message, wparam, lparam);
}

void mainwindow_init_font(struct mainwindow *w)
{
	static LOGFONT logfont = {
		.lfHeight = 16,
		.lfFaceName = "Courier New",
	};

	HDC dc;
	TEXTMETRIC tm;
	HFONT mono_font;

	//mono_font = GetStockObject(OEM_FIXED_FONT);
	mono_font = CreateFontIndirect(&logfont);
	dc = GetDC(0);
	//printf("dc=%x\n", dc);
	SelectObject(dc, mono_font);
	GetTextMetrics(dc, &tm);
	w->mono_font = mono_font;
	w->charwidth = tm.tmAveCharWidth;
	w->charheight = tm.tmHeight;
	ReleaseDC(0, dc);
}

void mainwindow_handle_wm_create(struct mainwindow *w, LPCREATESTRUCT create)
{
	HWND hwnd = w->hwnd;
	HWND monoedit;
	HWND cmdedit;

	HINSTANCE instance = create->hInstance;
	/* create and initialize MonoEdit */
	monoedit = CreateWindow("MonoEdit",
				  "",
				  WS_CHILD | WS_VISIBLE,
				  CW_USEDEFAULT,
				  CW_USEDEFAULT,
				  CW_USEDEFAULT,
				  CW_USEDEFAULT,
				  hwnd,
				  0,
				  instance,
				  0);
	w->monoedit = monoedit;
	w->nrows = INITIAL_N_ROW;
	w->monoedit_buffer = malloc(N_COL_CHAR*INITIAL_N_ROW);
	w->monoedit_buffer_cap_lines = INITIAL_N_ROW;
	memset(w->monoedit_buffer, ' ', N_COL_CHAR*INITIAL_N_ROW);
	SendMessage(monoedit, MONOEDIT_WM_SET_CSIZE, N_COL_CHAR, INITIAL_N_ROW);
	SendMessage(monoedit, MONOEDIT_WM_SET_BUFFER, 0, (LPARAM) w->monoedit_buffer);
	SendMessage(monoedit, WM_SETFONT, (WPARAM) w->mono_font, 0);
	mainwindow_update_monoedit_buffer(w, 0, INITIAL_N_ROW);
	/* subclass monoedit window */
	SetWindowLong(monoedit, GWL_USERDATA, (LONG) w);
	w->monoedit_wndproc = (WNDPROC) SetWindowLong(monoedit, GWL_WNDPROC, (LONG) monoedit_wndproc);
	SetFocus(monoedit);
	mainwindow_update_cursor_pos(w);
	/* create command area */
	cmdedit = CreateWindow("EDIT",
				   "",
				   WS_CHILD | WS_VISIBLE,
				   CW_USEDEFAULT,
				   CW_USEDEFAULT,
				   CW_USEDEFAULT,
				   CW_USEDEFAULT,
				   hwnd,
				   0,
				   instance,
				   0);
	SendMessage(cmdedit, WM_SETFONT, (WPARAM) w->mono_font, 0);
	w->cmdedit = cmdedit;
	// subclass command window
	SetWindowLong(cmdedit, GWL_USERDATA, (LONG) w);
	w->cmdedit_wndproc = (WNDPROC) SetWindowLong(cmdedit, GWL_WNDPROC, (LONG) cmdedit_wndproc);
}

void mainwindow_resize_monoedit(struct mainwindow *w, int width, int height)
{
	int new_nrows = height/w->charheight;
	if (new_nrows > w->nrows) {
		if (new_nrows > w->monoedit_buffer_cap_lines) {
			w->monoedit_buffer = realloc(w->monoedit_buffer, N_COL_CHAR*new_nrows);
			w->monoedit_buffer_cap_lines = new_nrows;
			SendMessage(w->monoedit, MONOEDIT_WM_SET_BUFFER, 0, (LPARAM) w->monoedit_buffer);
		}
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, w->nrows, new_nrows - w->nrows);
			mainwindow_update_monoedit_tags(w);
		}
		w->nrows = new_nrows;
	}
	SendMessage(w->monoedit, MONOEDIT_WM_SET_CSIZE, -1, height/w->charheight);
	SetWindowPos(w->monoedit,
		     0,
		     0,
		     0,
		     width,
		     height,
		     SWP_NOMOVE | SWP_NOZORDER);
}

LRESULT CALLBACK
wndproc(HWND hwnd,
	UINT message,
	WPARAM wparam,
	LPARAM lparam)
{
	struct mainwindow *w = (void *) GetWindowLong(hwnd, 0);
	switch (message) {
	case WM_NCCREATE:
		w = ((LPCREATESTRUCT)lparam)->lpCreateParams;
		w->hwnd = hwnd;
		SetWindowLong(hwnd, 0, (LONG) w);
		return TRUE;
	case WM_NCDESTROY:
		if (w) {
			free(w);
		}
		return 0;
	case WM_CREATE:
		mainwindow_handle_wm_create(w, (LPCREATESTRUCT) lparam);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_SIZE:
		{
			int width  = LOWORD(lparam);
			int height = HIWORD(lparam);
			int cmd_y;
			if (height >= w->charheight) {
				cmd_y = height - w->charheight;
			} else {
				cmd_y = 0;
			}
			mainwindow_resize_monoedit(w, width, cmd_y);
			SetWindowPos(w->cmdedit,
				     0,
				     0,
				     cmd_y,
				     width,
				     w->charheight,
				     SWP_NOZORDER);
		}
		return 0;
	case WM_CHAR:
		if (mainwindow_handle_char(w, wparam)) {
		} else {
		}
		return 0;
	}
	return DefWindowProc(hwnd, message, wparam, lparam);
}

ATOM register_wndclass(void)
{
	WNDCLASS wndclass = {0};

	wndclass.lpfnWndProc = wndproc;
	wndclass.cbWndExtra = sizeof(long);
	wndclass.hInstance = GetModuleHandle(0);
	wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	wndclass.lpszClassName = "MonoEditDemo";
	return RegisterClass(&wndclass);
}

static void format_error_code(char *buf, size_t buflen, DWORD error_code)
{
#if 0
   DWORD FormatMessage(
    DWORD dwFlags,	// source and processing options
    LPCVOID lpSource,	// pointer to  message source
    DWORD dwMessageId,	// requested message identifier
    DWORD dwLanguageId,	// language identifier for requested message
    LPTSTR lpBuffer,	// pointer to message buffer
    DWORD nSize,	// maximum size of message buffer
    va_list *Arguments 	// address of array of message inserts
   );
#endif
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
		      0,
		      error_code,
		      0,
		      buf,
		      buflen,
		      0);
}

int mainwindow_open_file(struct mainwindow *w, const char *path)
{
	static char errfmt_open[] = "Failed to open %s: %s\n";
	static char errfmt_size[] = "Failed to retrieve size of %s: %s\n";
	char errtext[512];

	w->file = CreateFile(path,
			    GENERIC_READ,
			    FILE_SHARE_READ,
			    0, // lpSecurityAttributes
			    OPEN_EXISTING,
			    0,
			    0);
	if (w->file == INVALID_HANDLE_VALUE) {
		format_error_code(errtext, sizeof errtext, GetLastError());
		fprintf(stderr, errfmt_open, path, errtext);
		return -1;
	}
	DWORD file_size_high;
	w->file_size = GetFileSize(w->file, &file_size_high);
	if (w->file_size == 0xffffffff) {
		DWORD error_code = GetLastError();
		if (error_code) {
			format_error_code(errtext, sizeof errtext, GetLastError());
			fprintf(stderr, errfmt_size, path, errtext);
			return -1;
		}
	}
	if (file_size_high) {
		fprintf(stderr, errfmt_open, path, "file is too large");
		return -1;
	}
	DEBUG_PRINTF("file size: %u (0x%x)\n", w->file_size, w->file_size);
	w->total_lines = w->file_size >> LOG2_N_COL;
	if (w->file_size&(N_COL-1)) {
		w->total_lines += 1;
	}
	return 0;
}


int mainwindow_init_cache(struct mainwindow *w)
{
	uint8_t *cache_data = malloc(N_CACHE_BLOCK*0x1000);
	if (!cache_data) {
		return -1;
	}
	for (int i=0; i<N_CACHE_BLOCK; i++) {
		w->cache[i].tag = 0;
		w->cache[i].data = cache_data + i*0x1000;
	}
	return 0;
}

static int file_chooser_dialog(char *buf, int buflen)
{
	buf[0] = 0;
	OPENFILENAME ofn = {0};
	ofn.lStructSize = sizeof ofn;
	ofn.hInstance = GetModuleHandle(0);
	ofn.lpstrFile = buf;
	ofn.nMaxFile = buflen;
	if (!GetOpenFileName(&ofn)) return -1;
	return 0;
}

int APIENTRY
WinMain(HINSTANCE instance,
	HINSTANCE prev_instance,
	LPSTR cmdline,
	int show)
{
	char filepath[512];
	struct mainwindow *w;

	if (!monoedit_register_class()) {
		return 1;
	}
	if (!register_wndclass()) {
		return 1;
	}
	if (!cmdline[0]) {
		// no command line argument
		if (file_chooser_dialog(filepath, sizeof filepath) < 0) {
			return 1;
		}
		cmdline = filepath;
	}
	// initialize mainwindow struct
	w = calloc(1, sizeof *w);
	w->interactive = true;
	if (mainwindow_open_file(w, cmdline) < 0) {
		return 1;
	}
	if (mainwindow_init_cache(w) < 0) {
		return 1;
	}
	mainwindow_init_font(w);
	mainwindow_init_lua(w);
	RECT rect = { 0, 0, w->charwidth*N_COL_CHAR, w->charheight*(INITIAL_N_ROW+1) };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindow("MonoEditDemo", // class name
				 "Whex", // window title
				 WS_OVERLAPPEDWINDOW, // window style
				 CW_USEDEFAULT, // initial x position
				 CW_USEDEFAULT, // initial y position
				 rect.right - rect.left, // initial width
				 rect.bottom - rect.top, // initial height
				 0,
				 0,
				 instance,
				 w); // window-creation data
	ShowWindow(hwnd, show);
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	// window closed now
	CloseHandle(w->file);
	return msg.wParam;
}

// commands

const char *mainwindow_cmd_goto(struct mainwindow *w, char *arg)
{
	long long address;
	if (sscanf(arg, "%I64x", &address) == 1) {
		if (address > w->file_size) {
			return "address out of range";
		}
		mainwindow_goto_address(w, address);
		return 0;
	}
	return "invalid argument";
}

const char *mainwindow_cmd_find_hex(struct mainwindow *w, char *arg)
{
	return mainwindow_find(w, arg, false);
}

const char *mainwindow_cmd_find_text(struct mainwindow *w, char *arg)
{
	return mainwindow_find(w, arg, true);
}

/* arg unused */
const char *mainwindow_cmd_findnext(struct mainwindow *w, char *arg)
{
	return mainwindow_repeat_search(w, false);
}

/* arg unused */
const char *mainwindow_cmd_findprev(struct mainwindow *w, char *arg)
{
	return mainwindow_repeat_search(w, true);
}

const char *mainwindow_cmd_lua(struct mainwindow *w, char *arg)
{
	int error;
	lua_State *L = w->lua_state;

	error = luaL_loadbuffer(L, arg, strlen(arg), "line") ||
		lua_pcall(L, 0, 0, 0);
	if (error) {
		const char *err = lua_tostring(L, -1);
		return err;
	}
	return 0;
}

const char *mainwindow_cmd_hl(struct mainwindow *w, char *arg)
{
	long long start = 0;
	int len = 0;
	sscanf(arg, "%I64d%d", &start, &len);
	w->hl_start = start;
	w->hl_len = len;
	if (w->interactive) {
		mainwindow_update_monoedit_tags(w);
		InvalidateRect(w->monoedit, 0, FALSE);
	}
	return 0;
}

const char *mainwindow_parse_and_execute_command(struct mainwindow *w, char *cmd)
{
	DEBUG_PRINTF("execute command {%s}\n", cmd);
	char *p = cmd;
	while (*p == ' ') p++;
	char *start = p;
	while (iswordchar(*p)) p++;
	char *end = p;
	cmdproc_t cmdproc = 0;
	switch (end-start) {
	case 2:
		if (!memcmp(start, "hl", 2)) {
			cmdproc = mainwindow_cmd_hl;
		}
	case 3:
		if (!memcmp(start, "lua", 3)) {
			cmdproc = mainwindow_cmd_lua;
		}
		break;
	case 4:
		if (!memcmp(start, "goto", 4)) {
			cmdproc = mainwindow_cmd_goto;
		} else if (!memcmp(start, "find", 4)) {
			cmdproc = mainwindow_cmd_find_hex;
		}
		break;
	case 8:
		if (!memcmp(start, "findnext", 8)) {
			cmdproc = mainwindow_cmd_findnext;
		} else if (!memcmp(start, "findprev", 8)) {
			cmdproc = mainwindow_cmd_findprev;
		} else if (!memcmp(start, "findtext", 8)) {
			cmdproc = mainwindow_cmd_find_text;
		}
		break;
	}
	if (cmdproc) {
		return cmdproc(w, end);
	}
	return "invalid command";
}

void mainwindow_init_lua(struct mainwindow *w)
{
	// try not to initialize twice
	assert(!w->lua_state);
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	// store `w` at REGISTRY[0]
	lua_pushinteger(L, 0);
	lua_pushlightuserdata(L, w);
	lua_settable(L, LUA_REGISTRYINDEX);
	// register C functions
	lua_pushcfunction(L, lapi_getbyte);
	lua_setglobal(L, "getbyte");
	w->lua_state = L;
}

static long long clamp(long long x, long long min, long long max)
{
	if (x < min) return min;
	if (x > max) return max;
	return x;
}

void mainwindow_update_monoedit_tags(struct mainwindow *w)
{
	long long start = w->hl_start;
	int len = w->hl_len;
	long long view_start = w->current_line * N_COL;
	long long view_end = (w->current_line + w->nrows) * N_COL;
	long long start_clamp = clamp(start, view_start, view_end) - view_start;
	long long end_clamp = clamp(start + len, view_start, view_end) - view_start;
	HWND w1 = w->monoedit;
	SendMessage(w1, MONOEDIT_WM_CLEAR_TAGS, 0, 0);
	if (end_clamp > start_clamp) {
		struct tag tag;
		tag.attr = 1;
		int tag_first_line = start_clamp >> LOG2_N_COL;
		int tag_last_line = (end_clamp-1) >> LOG2_N_COL; // inclusive
		int end_x = end_clamp & (N_COL-1);
		if (end_x == 0) {
			end_x = N_COL;
		}
		if (tag_last_line > tag_first_line) {
			tag.line = tag_first_line;
			tag.start = 10 + (start_clamp & (N_COL-1)) * 3;
			tag.len = (N_COL - (start_clamp & (N_COL-1))) * 3 - 1;
			SendMessage(w1, MONOEDIT_WM_ADD_TAG, 0, (LPARAM) &tag);
			for (int i=tag_first_line+1; i<tag_last_line; i++) {
				tag.line = i;
				tag.start = 10;
				tag.len = N_COL*3-1;
				SendMessage(w1, MONOEDIT_WM_ADD_TAG, 0, (LPARAM) &tag);
			}
			tag.line = tag_last_line;
			tag.start = 10;
			tag.len = end_x * 3 - 1;
			SendMessage(w1, MONOEDIT_WM_ADD_TAG, 0, (LPARAM) &tag);
		} else {
			// single line
			tag.line = tag_first_line;
			tag.start = 10 + (start_clamp & (N_COL-1)) * 3;
			tag.len = (end_x - (start_clamp & (N_COL-1))) * 3 - 1;
			SendMessage(w1, MONOEDIT_WM_ADD_TAG, 0, (LPARAM) &tag);
		}
	}
}
