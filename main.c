#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "monoedit.h"

#define N_CACHE_BLOCK 16
#define INITIAL_N_ROW 32

/* each cache block holds 0x1000 bytes */
struct cache_entry {
	uint32_t tag;
	uint8_t *data;
};

struct mainwindow {
	HWND hwnd;
	HWND monoedit;
	HWND status_edit;
	/* returns 1 if a full command has been recognized or the character is
	 * not recognized, 0 otherwise */
	int (*char_handler)(struct mainwindow *w, int c);
	int cmd_cap;
	int cmd_len;
	char *cmd;
	int cmd_arg;
	uint32_t file_size;
	uint32_t total_lines;
	HANDLE file;
	char *monoedit_buffer;
	/* capacity of buffer, in lines (80 bytes per line) */
	uint32_t monoedit_buffer_cap_lines;
	uint32_t current_line;
	/* number of lines displayed */
	uint32_t nrows;
	WNDPROC monoedit_wndproc;
	/* invariant: cursor_pos = (current_line + cursor_y) * 16 + cursor_x */
	/* current position in file */
	uint32_t cursor_pos;
	uint32_t cursor_x;
	uint32_t cursor_y;
	char *last_search_pattern;
	uint32_t last_search_pattern_len;
	int charwidth;
	int charheight;
	HFONT mono_font;
	struct cache_entry cache[N_CACHE_BLOCK];
};

typedef const char *(*cmdproc_t)(struct mainwindow *, char *);

bool mainwindow_cache_valid(struct mainwindow *w, int block)
{
	return w->cache[block].tag != -1;
};

int mainwindow_find_cache(struct mainwindow *w, uint32_t address)
{
	static int next_cache = 0;

	for (int i=0; i<N_CACHE_BLOCK; i++) {
		if (mainwindow_cache_valid(w, i) && address - w->cache[i].tag <= 0x1000) return i;
	}

	uint32_t tag = address & ~0xfff;
	SetFilePointer(w->file, tag, 0, FILE_BEGIN);
	DWORD nread;
	int ret = next_cache;
	ReadFile(w->file, w->cache[ret].data, 0x1000, &nread, 0);
	w->cache[ret].tag = tag;
	next_cache = ret+1&15;

	printf("loaded %08x into cache block %d\n", tag, ret);

	return ret;
}

uint8_t mainwindow_getbyte(struct mainwindow *w, uint32_t address)
{
	int block = mainwindow_find_cache(w, address);
	return w->cache[block].data[address & 0xfff];
}

void kmp_table(uint32_t *T, const uint8_t *pat, uint32_t len)
{
	uint32_t pos = 2;
	uint32_t cnd = 0;
	T[1] = 0;
	while (pos < len) {
		if (pat[pos-1] == pat[cnd]) {
			T[pos] = cnd+1;
			cnd++;
			pos++;
		} else if (cnd > 0) {
			cnd = T[cnd];
		} else {
			T[pos] = 0;
			pos++;
		}
	}
}

uint32_t mainwindow_kmp_search(struct mainwindow *w, const uint8_t *pat, uint32_t len, uint32_t start)
{
	assert(len);
	uint32_t T[len];
	/* T[0] is left undefined */
	kmp_table(T, pat, len);
	uint32_t m = start;
	uint32_t i = 0;
	while (m+i < w->file_size) {
		if (pat[i] == mainwindow_getbyte(w, m+i)) {
			if (i == len-1) return m;
			i++;
		} else {
			if (i) {
				m += i-T[i];
				i = T[i];
			} else {
				m++;
				i = 0;
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
uint32_t mainwindow_kmp_search_backward(struct mainwindow *w, const uint8_t *pat, uint32_t len, uint32_t start)
{
	assert(len);
	uint32_t T[len];
	uint8_t revpat[len];
	for (uint32_t i=0; i<len; i++) {
		revpat[i] = pat[len-1-i];
	}
	pat = revpat;
	/* T[0] is left undefined */
	kmp_table(T, pat, len);
	uint32_t m = start;
	uint32_t i = 0;
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
				i = 0;
			}
		}
	}
	/* no match */
	return w->file_size;
}

void mainwindow_update_monoedit_buffer(struct mainwindow *w, uint32_t buffer_line, uint32_t num_lines)
{
	uint32_t absolute_line = w->current_line + buffer_line;
	uint32_t absolute_line_end = absolute_line + num_lines;
	while (absolute_line < absolute_line_end) {
		char *p = &w->monoedit_buffer[buffer_line*80];
		uint32_t address = absolute_line << 4;
		if (absolute_line >= w->total_lines) {
			memset(p, ' ', 80);
		} else {
			int cache = mainwindow_find_cache(w, address);
			int base = address & 0xfff;
			sprintf(p, "%08x: ", address);
			p += 10;
			int end = 0;
		       	if (absolute_line+1 >= w->total_lines) {
				end = w->file_size&15;
			}
			if (!end) {
				end = 16;
			}
			for (int j=0; j<end; j++) {
				sprintf(p, "%02x ", w->cache[cache].data[base|j]);
				p += 3;
			}
			for (int j=end; j<16; j++) {
				sprintf(p, "   ");
				p += 3;
			}
			sprintf(p, "  ");
			p += 2;
			for (int j=0; j<end; j++) {
				uint8_t b = w->cache[cache].data[base|j];
				p[j] = b < 0x20 || b > 0x7e ? '.' : b;
			}
			for (int j=end; j<16; j++) {
				p[j] = ' ';
			}
		}
		buffer_line++;
		absolute_line++;
	}
}

bool iswordchar(char c)
{
	return isalnum(c) || c == '_';
}

uint8_t hexval(char c)
{
	return c > '9' ? 10+(c|32)-'a' : c-'0';
}

uint8_t hextobyte(const uint8_t *p)
{
	return hexval(p[0]) << 4 | hexval(p[1]);
}

void mainwindow_goto_line(struct mainwindow *w, uint32_t line)
{
	assert(line <= w->total_lines);
	w->current_line = line;
	mainwindow_update_monoedit_buffer(w, 0, w->nrows);
	InvalidateRect(w->monoedit, 0, FALSE);
}

void mainwindow_update_cursor_pos(struct mainwindow *w)
{
	SendMessage(w->monoedit,
		    MONOEDIT_WM_SET_CURSOR_POS,
		    10+w->cursor_x*3,
		    w->cursor_y);
}

void mainwindow_goto_address(struct mainwindow *w, uint32_t address)
{
	uint32_t line = address >> 4;
	uint32_t col = address & 15;
	uint32_t line1;
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

const char *mainwindow_cmd_goto(struct mainwindow *w, char *arg)
{
	const char *p = arg;
	uint32_t address;
	if (sscanf(arg, "%x", &address) == 1) {
		if (address > w->file_size) {
			return "address out of range";
		}
		mainwindow_goto_address(w, address);
		return 0;
	}
	return "invalid argument";
}

char *mainwindow_find(struct mainwindow *w, char *arg, bool istext)
{
	uint32_t patlen;
	uint8_t *pat;
	char *p = arg;
	while (*p == ' ') p++;
	if (istext) {
		if (*p == '"') {
			/* TODO */
			return "TODO";
		} else {
			char *start = p;
			while (iswordchar(*p)) p++;
			if (*p) {
				/* trailing character(s) found */
				return "invalid argument";
			}
			patlen = p - start;
			pat = start;
		}
	} else {
		uint32_t slen = strlen(p);
		if (slen&1 || !slen) {
			return "invalid argument";
		}
		patlen = slen >> 1;
		pat = malloc(patlen);
		for (uint32_t i=0; i<patlen; i++) {
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
	uint32_t matchpos = mainwindow_kmp_search(w, pat, patlen, w->cursor_pos);
	if (!istext) {
		free(pat);
	}
	if (matchpos == w->file_size) {
		return "pattern not found";
	}
	mainwindow_goto_address(w, matchpos);
	return 0;
}

const char *mainwindow_cmd_find_hex(struct mainwindow *w, char *arg)
{
	return mainwindow_find(w, arg, false);
}

const char *mainwindow_cmd_find_text(struct mainwindow *w, char *arg)
{
	return mainwindow_find(w, arg, true);
}

char *mainwindow_repeat_search(struct mainwindow *w, bool reverse)
{
	if (!w->last_search_pattern) {
		return "no previous pattern";
	}
	uint32_t (*search_func)(struct mainwindow *w, const uint8_t *pat, uint32_t patlen, uint32_t start);
	uint32_t start;
	if (reverse) {
		uint32_t tmp = w->cursor_pos + w->last_search_pattern_len - 1;
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
	uint32_t matchpos = search_func(w, w->last_search_pattern, 
					w->last_search_pattern_len,
					start);
	if (matchpos == w->file_size) {
		return "pattern not found";
	}
	mainwindow_goto_address(w, matchpos);
	return 0;
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

const char *mainwindow_parse_and_execute_command(struct mainwindow *w, char *cmd)
{
	printf("execute command {%s}\n", cmd);
	char *p = cmd;
	while (*p == ' ') p++;
	char *start = p;
	while (iswordchar(*p)) p++;
	char *end = p;
	cmdproc_t cmdproc = 0;
	switch (end-start) {
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
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, -1);
		memmove(w->monoedit_buffer+80, w->monoedit_buffer, 80*(w->nrows-1));
		mainwindow_update_monoedit_buffer(w, 0, 1);
	}
}

void mainwindow_scroll_down_line(struct mainwindow *w)
{
	if (w->current_line < w->total_lines) {
		w->current_line++;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, 1);
		memmove(w->monoedit_buffer, w->monoedit_buffer+80, 80*(w->nrows-1));
		mainwindow_update_monoedit_buffer(w, w->nrows-1, 1);
	}
}

void mainwindow_move_up(struct mainwindow *w)
{
	if (w->cursor_pos >= 16) {
		w->cursor_pos -= 16;
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
	if (w->file_size >= 16 && w->cursor_pos < w->file_size - 16) {
		w->cursor_pos += 16;
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
	if (w->cursor_x < 15 && w->cursor_pos+1 < w->file_size) {
		w->cursor_x++;
		w->cursor_pos++;
		mainwindow_update_cursor_pos(w);
	}
}

void mainwindow_scroll_up_page(struct mainwindow *w)
{
	if (w->current_line >= w->nrows) {
		w->current_line -= w->nrows;
		InvalidateRect(w->monoedit, 0, FALSE);
		mainwindow_update_monoedit_buffer(w, 0, w->nrows);
	} else {
		uint32_t delta = w->current_line;
		w->current_line = 0;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, -delta);
		memmove(w->monoedit_buffer+80*delta, w->monoedit_buffer, 80*(w->nrows-delta));
		mainwindow_update_monoedit_buffer(w, 0, delta);
	}
}

void mainwindow_scroll_down_page(struct mainwindow *w)
{
	if (w->current_line + w->nrows <= w->total_lines) {
		w->current_line += w->nrows;
		InvalidateRect(w->monoedit, 0, FALSE);
		mainwindow_update_monoedit_buffer(w, 0, w->nrows);
	} else {
		uint32_t delta = w->total_lines - w->current_line;
		w->current_line = w->total_lines;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, delta);
		memmove(w->monoedit_buffer, w->monoedit_buffer+80*delta, 80*(w->nrows-delta));
		mainwindow_update_monoedit_buffer(w, w->nrows-delta, delta);
	}
}

void mainwindow_move_up_page(struct mainwindow *w)
{
	if (w->cursor_pos >= 16*w->nrows) {
		w->cursor_pos -= 16*w->nrows;
		mainwindow_scroll_up_page(w);
	}
}

void mainwindow_move_down_page(struct mainwindow *w)
{
	if (w->file_size >= 16*w->nrows && w->cursor_pos < w->file_size - 16*w->nrows) {
		w->cursor_pos += 16*w->nrows;
		mainwindow_scroll_down_page(w);
	}
}

int mainwindow_char_handler_normal(struct mainwindow *w, int c);

int mainwindow_char_handler_command(struct mainwindow *w, int c)
{
	const char *errmsg = 0;

	switch (c) {
	case '\r':
		switch (w->cmd[0]) {
		case '/':
			mainwindow_execute_command(w, mainwindow_cmd_find_text, w->cmd+1);
			break;
		case ':':
			errmsg = mainwindow_parse_and_execute_command(w, w->cmd+1);
			break;
		case '\\':
			mainwindow_execute_command(w, mainwindow_cmd_find_hex, w->cmd+1);
			break;
		case 'g':
			mainwindow_execute_command(w, mainwindow_cmd_goto, w->cmd+1);
			break;
		}
		if (errmsg) {
			puts(errmsg);
		}
		/* fallthrough */
	case 27: // escape
		w->char_handler = mainwindow_char_handler_normal;
		return 1;
	default:
		return 0;
	}
}

void mainwindow_add_char_to_command(struct mainwindow *w, char c)
{
	HWND status_edit = w->status_edit;
	int len = w->cmd_len;
	if (c == 8) {
		// backspace
		if (len > 0) {
			SendMessage(status_edit, EM_SETSEL, len-1, len);
			SendMessage(status_edit, EM_REPLACESEL, TRUE, (LPARAM) "");
			w->cmd[--w->cmd_len] = 0;
		}
	} else {
		char str[2] = {c};
		if (len < w->cmd_cap) {
			SendMessage(status_edit, EM_SETSEL, len, len);
			SendMessage(status_edit, EM_REPLACESEL, TRUE, (LPARAM) str);
			w->cmd[w->cmd_len++] = c;
		}
	}
}

int mainwindow_char_handler_normal(struct mainwindow *w, int c)
{
	switch (c) {
	case '/':
	case ':':
	case '\\':
	case 'g':
		w->char_handler = mainwindow_char_handler_command;
		return 0;
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		w->cmd_arg = w->cmd_arg*10 + (c-'0');
		return 0;
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
			if (cx >= 10 && cx < 58) {
				cx = (cx-10)/3;
				uint32_t pos = (w->current_line + cy << 4) + cx;
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
	}
	return CallWindowProc(w->monoedit_wndproc, hwnd, message, wparam, lparam);
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
	HWND status_edit;

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
	w->monoedit_buffer = malloc(80*INITIAL_N_ROW);
	w->monoedit_buffer_cap_lines = INITIAL_N_ROW;
	memset(w->monoedit_buffer, ' ', 80*INITIAL_N_ROW);
	SendMessage(monoedit, MONOEDIT_WM_SET_CSIZE, 80, INITIAL_N_ROW);
	SendMessage(monoedit, MONOEDIT_WM_SET_BUFFER, 0, (LPARAM) w->monoedit_buffer);
	SendMessage(monoedit, WM_SETFONT, (WPARAM) w->mono_font, 0);
	mainwindow_update_monoedit_buffer(w, 0, INITIAL_N_ROW);
	/* subclass monoedit window */
	SetWindowLong(monoedit, GWL_USERDATA, (LONG) w);
	w->monoedit_wndproc = (WNDPROC) SetWindowLong(monoedit, GWL_WNDPROC, (LONG) monoedit_wndproc);
	SetFocus(monoedit);
	mainwindow_update_cursor_pos(w);
	/* create command area */
	status_edit = CreateWindow("EDIT",
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
	w->status_edit = status_edit;
	SendMessage(status_edit, WM_SETFONT, (WPARAM) w->mono_font, 0);
}

void mainwindow_resize_monoedit(struct mainwindow *w, uint32_t width, uint32_t height)
{
	uint32_t new_nrows = height/w->charheight;
	if (new_nrows > w->nrows) {
		if (new_nrows > w->monoedit_buffer_cap_lines) {
			w->monoedit_buffer = realloc(w->monoedit_buffer, 80*new_nrows);
			w->monoedit_buffer_cap_lines = new_nrows;
			SendMessage(w->monoedit, MONOEDIT_WM_SET_BUFFER, 0, (LPARAM) w->monoedit_buffer);
		}
		mainwindow_update_monoedit_buffer(w, w->nrows, new_nrows - w->nrows);
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
		w->char_handler = mainwindow_char_handler_normal;
		w->cmd_cap = 64;
		w->cmd = calloc(1, w->cmd_cap);
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
			uint32_t width  = LOWORD(lparam);
			uint32_t height = HIWORD(lparam);
			uint32_t cmd_y, cmd_height;
			if (height >= w->charheight) {
				cmd_y = height - w->charheight;
				cmd_height = w->charheight;
			} else {
				cmd_y = 0;
				cmd_height = height;
			}
			mainwindow_resize_monoedit(w, width, cmd_y);
			SetWindowPos(w->status_edit,
				     0,
				     0,
				     cmd_y,
				     width,
				     w->charheight,
				     SWP_NOZORDER);
		}
		return 0;
	case WM_CHAR:
		if (w->char_handler(w, wparam)) {
			memset(w->cmd, 0, w->cmd_len);
			w->cmd_len = 0;
			w->cmd_arg = 0;
			SetWindowText(w->status_edit, "");
		} else {
			mainwindow_add_char_to_command(w, wparam);
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

void format_error_code(char *buf, size_t buflen, DWORD error_code)
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
	DWORD lasterr = GetLastError();
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
	printf("file size: %u (0x%x)\n", w->file_size, w->file_size);
	w->total_lines = w->file_size >> 4;
	if (w->file_size&15) {
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
		w->cache[i].tag = -1;
		w->cache[i].data = cache_data + i*0x1000;
	}
	return 0;
}

int file_chooser_dialog(char *buf, int buflen)
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
	w = calloc(1, sizeof *w);
	if (mainwindow_open_file(w, cmdline) < 0) {
		return 1;
	}
	if (mainwindow_init_cache(w) < 0) {
		return 1;
	}
	mainwindow_init_font(w);
	RECT rect = { 0, 0, w->charwidth*80, w->charheight*(INITIAL_N_ROW+1) };
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
	CloseHandle(w->file);
	monoedit_unregister_class();
	return msg.wParam;
}
