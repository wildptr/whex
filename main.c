#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "monoedit.h"

#define N_CACHE 16
#define INITIAL_N_ROW 32

struct mainwindow_state {
	HWND self;
	HWND monoedit;
	HWND status_edit;
	/* returns 1 if a full command has been recognized or the character is
	 * not recognized, 0 otherwise */
	int (*char_handler)(int c);
	int cmd_cap;
	int cmd_len;
	char *cmd;
	int cmd_arg;
};

/* struct mainwindow_state associated with main window */
struct mainwindow_state *g_state;
/* each cache line holds 0x1000 bytes */
struct cache_entry {
	uint32_t tag;
	uint8_t *data;
} g_cache[N_CACHE];
uint32_t g_file_size;
uint32_t g_total_lines;
HANDLE g_file;
char *g_monoedit_buffer;
/* capacity of buffer, in lines (80 bytes per line) */
uint32_t g_monoedit_buffer_cap_lines;
uint32_t g_current_line;
/* number of lines displayed */
uint32_t g_nrows;
WNDPROC g_monoedit_wndproc;
/* current position in file */
/* invariant: g_cursor_pos = (g_current_line + g_cursor_y) * 16 + g_cursor_x */
uint32_t g_cursor_pos;
uint32_t g_cursor_x;
uint32_t g_cursor_y;
char *g_last_search_pattern;
uint32_t g_last_search_pattern_len;
int g_charwidth;
int g_charheight;
HFONT g_mono_font;

bool cache_valid(int cache)
{
	return g_cache[cache].tag != -1;
};

int find_cache(uint32_t address)
{
	static int next_cache = 0;

	for (int i=0; i<N_CACHE; i++) {
		if (cache_valid(i) && address - g_cache[i].tag <= 0x1000) return i;
	}

	uint32_t tag = address & ~0xfff;
	SetFilePointer(g_file, tag, 0, FILE_BEGIN);
	DWORD nread;
	int ret = next_cache;
	ReadFile(g_file, g_cache[ret].data, 0x1000, &nread, 0);
	g_cache[ret].tag = tag;
	next_cache = ret+1&15;

	printf("loaded %08x into cache %d\n", tag, ret);

	return ret;
}

uint8_t getbyte(uint32_t address)
{
	int cache = find_cache(address);
	return g_cache[cache].data[address & 0xfff];
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

uint32_t kmp_search(const uint8_t *pat, uint32_t len, uint32_t start)
{
	assert(len);
	uint32_t T[len];
	/* T[0] is left undefined */
	kmp_table(T, pat, len);
	uint32_t m = start;
	uint32_t i = 0;
	while (m+i < g_file_size) {
		if (pat[i] == getbyte(m+i)) {
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
	return g_file_size;
}

/*
 * ...|xx|x..
 * position of first mark = start
 * number of bytes after the second mark = N-(start+len-1)
 */
uint32_t kmp_search_backward(const uint8_t *pat, uint32_t len, uint32_t start)
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
	while (m+i < g_file_size) {
		if (pat[i] == getbyte(g_file_size-1-(m+i))) {
			if (i == len-1) return g_file_size-(m+len);
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
	return g_file_size;
}

void update_monoedit_buffer(uint32_t buffer_line, uint32_t num_lines)
{
	uint32_t absolute_line = g_current_line + buffer_line;
	uint32_t absolute_line_end = absolute_line + num_lines;
	while (absolute_line < absolute_line_end) {
		char *p = &g_monoedit_buffer[buffer_line*80];
		uint32_t address = absolute_line << 4;
		if (absolute_line >= g_total_lines) {
			memset(p, ' ', 80);
		} else {
			int cache = find_cache(address);
			int base = address & 0xfff;
			sprintf(p, "%08x: ", address);
			p += 10;
			int end = 0;
		       	if (absolute_line+1 >= g_total_lines) {
				end = g_file_size&15;
			}
			if (!end) {
				end = 16;
			}
			for (int j=0; j<end; j++) {
				sprintf(p, "%02x ", g_cache[cache].data[base|j]);
				p += 3;
			}
			for (int j=end; j<16; j++) {
				sprintf(p, "   ");
				p += 3;
			}
			sprintf(p, "  ");
			p += 2;
			for (int j=0; j<end; j++) {
				uint8_t b = g_cache[cache].data[base|j];
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

void goto_line(uint32_t line)
{
	assert(line <= g_total_lines);
	g_current_line = line;
	update_monoedit_buffer(0, g_nrows);
	InvalidateRect(g_state->monoedit, 0, FALSE);
}

void update_cursor_pos(void)
{
	SendMessage(g_state->monoedit,
		    MONOEDIT_WM_SET_CURSOR_POS,
		    10+g_cursor_x*3,
		    g_cursor_y);
}

void goto_address(uint32_t address)
{
	uint32_t line = address >> 4;
	uint32_t col = address & 15;
	uint32_t line1;
	assert(address <= g_file_size);
	if (line >= g_nrows >> 1) {
		line1 = line - (g_nrows >> 1);
	} else {
		line1 = 0;
	}
	goto_line(line1);
	SendMessage(g_state->monoedit, MONOEDIT_WM_SET_CURSOR_POS, 10+col*3, 0);
	g_cursor_pos = address;
	g_cursor_x = col;
	g_cursor_y = line - line1;
	update_cursor_pos();
}

const char *cmd_goto(char *arg)
{
	const char *p = arg;
	uint32_t address;
	if (sscanf(arg, "%x", &address) == 1) {
		if (address > g_file_size) {
			return "address out of range";
		}
		goto_address(address);
		return 0;
	}
	return "invalid argument";
}

uint8_t hexval(char c)
{
	return c > '9' ? 10+(c|32)-'a' : c-'0';
}

uint8_t hextobyte(const uint8_t *p)
{
	return hexval(p[0]) << 4 | hexval(p[1]);
}

char *find_binary_or_text(char *arg, bool istext)
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
	if (g_last_search_pattern) {
		free(g_last_search_pattern);
	}
	g_last_search_pattern = malloc(patlen);
	memcpy(g_last_search_pattern, pat, patlen);
	g_last_search_pattern_len = patlen;
	uint32_t matchpos = kmp_search(pat, patlen, g_cursor_pos);
	if (!istext) {
		free(pat);
	}
	if (matchpos == g_file_size) {
		return "pattern not found";
	}
	goto_address(matchpos);
	return 0;
}

const char *cmd_find(char *arg)
{
	return find_binary_or_text(arg, false);
}

const char *cmd_findtext(char *arg)
{
	return find_binary_or_text(arg, true);
}

char *repeat_search(bool reverse)
{
	if (!g_last_search_pattern) {
		return "no previous pattern";
	}
	uint32_t (*search_func)(const uint8_t *pat, uint32_t patlen, uint32_t start);
	uint32_t start;
	if (reverse) {
		uint32_t tmp = g_cursor_pos + g_last_search_pattern_len - 1;
		if (g_file_size >= tmp) {
			start = g_file_size - tmp;
		} else {
			return "pattern not found";
			//start = g_file_size;
		}
		search_func = kmp_search_backward;
	} else {
		if (g_cursor_pos+1 < g_file_size) {
			start = g_cursor_pos+1;
		} else {
			return "pattern not found";
			//start = 0;
		}
		search_func = kmp_search;
	}
	uint32_t matchpos = search_func(g_last_search_pattern, 
					g_last_search_pattern_len,
					start);
	if (matchpos == g_file_size) {
		return "pattern not found";
	}
	goto_address(matchpos);
	return 0;
}

/* arg unused */
const char *cmd_findnext(char *arg)
{
	return repeat_search(false);
}

/* arg unused */
const char *cmd_findprev(char *arg)
{
	return repeat_search(true);
}

const char *execute_command(char *cmd)
{
	printf("execute cmd {%s}\n", cmd);
	char *p = cmd;
	while (*p == ' ') p++;
	char *start = p;
	while (iswordchar(*p)) p++;
	char *end = p;
	const char *(*cmdproc)(char *) = 0;
	switch (end-start) {
	case 4:
		if (!memcmp(start, "goto", 4)) {
			cmdproc = cmd_goto;
		} else if (!memcmp(start, "find", 4)) {
			cmdproc = cmd_find;
		}
		break;
	case 8:
		if (!memcmp(start, "findnext", 8)) {
			cmdproc = cmd_findnext;
		} else if (!memcmp(start, "findprev", 8)) {
			cmdproc = cmd_findprev;
		} else if (!memcmp(start, "findtext", 8)) {
			cmdproc = cmd_findtext;
		}
		break;
	}
	if (cmdproc) {
		return cmdproc(end);
	}
	return "invalid command";
}

void execute_command_directly(const char *(*cmdproc)(char *), char *arg)
{
	const char *errmsg = cmdproc(arg);
	if (errmsg) {
		MessageBox(g_state->self, errmsg, "Error", MB_ICONERROR);
	}
}

void scroll_up_line(void)
{
	if (g_current_line) {
		g_current_line--;
		SendMessage(g_state->monoedit, MONOEDIT_WM_SCROLL, 0, -1);
		memmove(g_monoedit_buffer+80, g_monoedit_buffer, 80*(g_nrows-1));
		update_monoedit_buffer(0, 1);
	}
}

void scroll_down_line(void)
{
	if (g_current_line < g_total_lines) {
		g_current_line++;
		SendMessage(g_state->monoedit, MONOEDIT_WM_SCROLL, 0, 1);
		memmove(g_monoedit_buffer, g_monoedit_buffer+80, 80*(g_nrows-1));
		update_monoedit_buffer(g_nrows-1, 1);
	}
}

void move_up(void)
{
	if (g_cursor_pos >= 16) {
		g_cursor_pos -= 16;
		if (g_cursor_y) {
			g_cursor_y--;
			update_cursor_pos();
		} else {
			scroll_up_line();
		}
	}
}

void move_down(void)
{
	if (g_file_size >= 16 && g_cursor_pos < g_file_size - 16) {
		g_cursor_pos += 16;
		if (g_cursor_y < g_nrows-1) {
			g_cursor_y++;
			update_cursor_pos();
		} else {
			scroll_down_line();
		}
	}
}

void move_left(void)
{
	if (g_cursor_x) {
		g_cursor_x--;
		g_cursor_pos--;
		update_cursor_pos();
	}
}

void move_right(void)
{
	if (g_cursor_x < 15 && g_cursor_pos+1 < g_file_size) {
		g_cursor_x++;
		g_cursor_pos++;
		update_cursor_pos();
	}
}

void scroll_up_page(void)
{
	if (g_current_line >= g_nrows) {
		g_current_line -= g_nrows;
		InvalidateRect(g_state->monoedit, 0, FALSE);
		update_monoedit_buffer(0, g_nrows);
	} else {
		uint32_t delta = g_current_line;
		g_current_line = 0;
		SendMessage(g_state->monoedit, MONOEDIT_WM_SCROLL, 0, -delta);
		memmove(g_monoedit_buffer+80*delta, g_monoedit_buffer, 80*(g_nrows-delta));
		update_monoedit_buffer(0, delta);
	}
}

void scroll_down_page(void)
{
	if (g_current_line + g_nrows <= g_total_lines) {
		g_current_line += g_nrows;
		InvalidateRect(g_state->monoedit, 0, FALSE);
		update_monoedit_buffer(0, g_nrows);
	} else {
		uint32_t delta = g_total_lines - g_current_line;
		g_current_line = g_total_lines;
		SendMessage(g_state->monoedit, MONOEDIT_WM_SCROLL, 0, delta);
		memmove(g_monoedit_buffer, g_monoedit_buffer+80*delta, 80*(g_nrows-delta));
		update_monoedit_buffer(g_nrows-delta, delta);
	}
}

void move_up_page(void)
{
	if (g_cursor_pos >= 16*g_nrows) {
		g_cursor_pos -= 16*g_nrows;
		scroll_up_page();
	}
}

void move_down_page(void)
{
	if (g_file_size >= 16*g_nrows && g_cursor_pos < g_file_size - 16*g_nrows) {
		g_cursor_pos += 16*g_nrows;
		scroll_down_page();
	}
}

int char_handler_normal(int c);

int char_handler_command(int c)
{
	switch (c) {
	case '\r':
		switch (g_state->cmd[0]) {
		case '/':
			execute_command_directly(cmd_find, g_state->cmd+1);
			break;
		case ':':
			execute_command(g_state->cmd+1);
			break;
		case 'g':
			execute_command_directly(cmd_goto, g_state->cmd+1);
			break;
		}
		/* fallthrough */
	case 27: // escape
		g_state->char_handler = char_handler_normal;
		return 1;
	}
	return 0;
}

void add_char_to_command(char c)
{
	HWND status_edit = g_state->status_edit;
	int len = g_state->cmd_len;
	if (c == 8) {
		if (len > 0) {
			SendMessage(status_edit, EM_SETSEL, len-1, len);
			SendMessage(status_edit, EM_REPLACESEL, TRUE, (LPARAM) "");
			g_state->cmd[--g_state->cmd_len] = 0;
		}
	} else {
		char str[2] = {c};
		if (len < g_state->cmd_cap) {
			SendMessage(status_edit, EM_SETSEL, len, len);
			SendMessage(status_edit, EM_REPLACESEL, TRUE, (LPARAM) str);
			g_state->cmd[g_state->cmd_len++] = c;
		}
	}
}

int char_handler_normal(int c)
{
	switch (c) {
	case '/':
	case ':':
	case 'g':
		g_state->char_handler = char_handler_command;
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
		g_state->cmd_arg = g_state->cmd_arg*10 + (c-'0');
		return 0;
	case 'N':
		execute_command_directly(cmd_findprev, 0);
		return 1;
	case 'h':
		move_left();
		return 1;
	case 'j':
		move_down();
		return 1;
	case 'k':
		move_up();
		return 1;
	case 'l':
		move_right();
		return 1;
	case 'n':
		execute_command_directly(cmd_findnext, 0);
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
	switch (message) {
	case WM_LBUTTONDOWN:
		{
			int x = LOWORD(lparam);
			int y = HIWORD(lparam);
			SetFocus(hwnd);
			int cx = x / g_charwidth;
			int cy = y / g_charheight;
			if (cx >= 10 && cx < 58) {
				cx = (cx-10)/3;
				uint32_t pos = (g_current_line + cy << 4) + cx;
				if (pos < g_file_size) {
					g_cursor_pos = pos;
					g_cursor_x = cx;
					g_cursor_y = cy;
					update_cursor_pos();
				}
			}
		}
		return 0;
	case WM_KEYDOWN:
		switch (wparam) {
		case VK_UP:
			move_up();
			break;
		case VK_DOWN:
			move_down();
			break;
		case VK_LEFT:
			move_left();
			break;
		case VK_RIGHT:
			move_right();
			break;
		case VK_PRIOR:
			move_up_page();
			break;
		case VK_NEXT:
			move_down_page();
			break;
		case VK_HOME:
			goto_address(0);
			break;
		case VK_END:
			if (g_file_size) {
				goto_address(g_file_size-1);
			}
			break;
		}
		return 0;
	}
	return CallWindowProc(g_monoedit_wndproc, hwnd, message, wparam, lparam);
}

void init_font(void)
{
	g_mono_font = GetStockObject(OEM_FIXED_FONT);
	HDC dc = GetDC(0);
	printf("dc=%x\n", dc);
	SelectObject(dc, g_mono_font);
	TEXTMETRIC tm;
	GetTextMetrics(dc, &tm);
	g_charwidth = tm.tmAveCharWidth;
	g_charheight = tm.tmHeight;
	ReleaseDC(0, dc);
}

void handle_wm_create(struct mainwindow_state *st, LPCREATESTRUCT create)
{
	HWND hwnd = st->self;
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
	st->monoedit = monoedit;
	g_nrows = INITIAL_N_ROW;
	g_monoedit_buffer = malloc(80*INITIAL_N_ROW);
	g_monoedit_buffer_cap_lines = INITIAL_N_ROW;
	memset(g_monoedit_buffer, ' ', 80*INITIAL_N_ROW);
	SendMessage(monoedit, MONOEDIT_WM_SET_CSIZE, 80, INITIAL_N_ROW);
	SendMessage(monoedit, MONOEDIT_WM_SET_BUFFER, 0, (LPARAM) g_monoedit_buffer);
	SendMessage(monoedit, WM_SETFONT, (WPARAM) g_mono_font, 0);
	update_monoedit_buffer(0, INITIAL_N_ROW);
	g_monoedit_wndproc = (WNDPROC) SetWindowLong(monoedit, GWL_WNDPROC, (LONG) monoedit_wndproc);
	SetFocus(monoedit);
	update_cursor_pos();
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
	st->status_edit = status_edit;
	SendMessage(status_edit, WM_SETFONT, (WPARAM) g_mono_font, 0);
}

void resize_monoedit(uint32_t width, uint32_t height)
{
	uint32_t new_nrows = height/g_charheight;
	if (new_nrows > g_nrows) {
		if (new_nrows > g_monoedit_buffer_cap_lines) {
			g_monoedit_buffer = realloc(g_monoedit_buffer, 80*new_nrows);
			g_monoedit_buffer_cap_lines = new_nrows;
			SendMessage(g_state->monoedit, MONOEDIT_WM_SET_BUFFER, 0, (LPARAM) g_monoedit_buffer);
		}
		update_monoedit_buffer(g_nrows, new_nrows - g_nrows);
		g_nrows = new_nrows;
	}
	SendMessage(g_state->monoedit, MONOEDIT_WM_SET_CSIZE, -1, height/g_charheight);
	SetWindowPos(g_state->monoedit,
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
	struct mainwindow_state *st = (void *) GetWindowLong(hwnd, 0);
	switch (message) {
	case WM_NCCREATE:
		st = calloc(1, sizeof *st);
		printf("st=%p\n", st);
		if (!st) {
			return FALSE;
		}
		g_state = st;
		st->self = hwnd;
		st->char_handler = char_handler_normal;
		st->cmd_cap = 64;
		st->cmd = calloc(1, st->cmd_cap);
		SetWindowLong(hwnd, 0, (LONG) st);
		return TRUE;
	case WM_NCDESTROY:
		if (st) {
			free(st);
		}
		return 0;
	case WM_CREATE:
		handle_wm_create(st, (LPCREATESTRUCT) lparam);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_SIZE:
		{
			uint32_t width  = LOWORD(lparam);
			uint32_t height = HIWORD(lparam);
			uint32_t cmd_y, cmd_height;
			if (height >= g_charheight) {
				cmd_y = height - g_charheight;
				cmd_height = g_charheight;
			} else {
				cmd_y = 0;
				cmd_height = height;
			}
			resize_monoedit(width, cmd_y);
			SetWindowPos(st->status_edit,
				     0,
				     0,
				     cmd_y,
				     width,
				     g_charheight,
				     SWP_NOZORDER);
		}
		return 0;
	case WM_CHAR:
		if (st->char_handler(wparam)) {
			memset(g_state->cmd, 0, g_state->cmd_len);
			g_state->cmd_len = 0;
			g_state->cmd_arg = 0;
			SetWindowText(g_state->status_edit, "");
		} else {
			add_char_to_command(wparam);
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

int open_file(const char *path) 
{
	static char errfmt_open[] = "Failed to open %s: %s\n";
	static char errfmt_size[] = "Failed to retrieve size of %s: %s\n";
	char errtext[512];

	g_file = CreateFile(path,
			    GENERIC_READ,
			    FILE_SHARE_READ,
			    0, // lpSecurityAttributes
			    OPEN_EXISTING,
			    0,
			    0);
	if (g_file == INVALID_HANDLE_VALUE) {
		format_error_code(errtext, sizeof errtext, GetLastError());
		fprintf(stderr, errfmt_open, path, errtext);
		return -1;
	}
	DWORD file_size_high;
	g_file_size = GetFileSize(g_file, &file_size_high);
	if (g_file_size == 0xffffffff) {
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
	printf("file size: %u (0x%x)\n", g_file_size, g_file_size);
	g_total_lines = g_file_size >> 4;
	if (g_file_size&15) {
		g_total_lines += 1;
	}
	return 0;
}

int init_cache(void)
{
	uint8_t *cache_data = malloc(N_CACHE*0x1000);
	if (!cache_data) {
		return -1;
	}
	for (int i=0; i<N_CACHE; i++) {
		g_cache[i].tag = -1;
		g_cache[i].data = cache_data + i*0x1000;
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
	if (!monoedit_register_class()) {
		return 1;
	}
	if (!register_wndclass()) {
		return 1;
	}
	char filepath[512];
	if (!cmdline[0]) {
		if (file_chooser_dialog(filepath, sizeof filepath) < 0) {
			return 1;
		}
		cmdline = filepath;
	}
	if (open_file(cmdline) < 0) {
		return 1;
	}
	if (init_cache() < 0) {
		return 1;
	}
	init_font();
	RECT rect = { 0, 0, g_charwidth*80, g_charheight*(INITIAL_N_ROW+1) };
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
				 0);
	ShowWindow(hwnd, show);
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	CloseHandle(g_file);
	monoedit_unregister_class();
	return msg.wParam;
}
