#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <windows.h>

#include "util.h"
#include "whex.h"
#include "ui.h"
#include "monoedit.h"
#include "tree.h"
#include "unicode.h"

#include <commctrl.h>

#define INITIAL_N_ROW 32
#define LOG2_N_COL 4
#define N_COL (1<<LOG2_N_COL)
#define N_COL_CHAR (16+4*N_COL)

#define BUFSIZE 512

enum {
	IDC_STATUS_BAR = 0x100
};

char *strdup(const char *);
void register_lua_globals(lua_State *L);

#if 0
static bool
iswordchar(char c)
{
	return isalnum(c) || c == '_';
}

static uint8_t
hexval(char c)
{
	return c > '9' ? 10+(c|32)-'a' : c-'0';
}

static uint8_t
hextobyte(const uint8_t *p)
{
	return hexval(p[0]) << 4 | hexval(p[1]);
}
#endif

LRESULT CALLBACK med_wndproc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK cmdedit_wndproc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
ATOM register_wndclass(void);
static void format_error_code(TCHAR *, size_t, DWORD);
static int file_chooser_dialog(TCHAR *, int);
static int start_gui(HINSTANCE, int, UI *, TCHAR *);
void update_monoedit_buffer(UI *, int, int);
void set_current_line(UI *, long long);
void update_status_text(UI *, struct tree *);
void update_field_info(UI *);
void update_cursor_pos(UI *);
long long cursor_pos(UI *);
void goto_address(UI *, long long);
void error_prompt(UI *, const char *);
void scroll_up_line(UI *);
void scroll_down_line(UI *);
void move_up(UI *);
void move_down(UI *);
void move_left(UI *);
void move_right(UI *);
void move_up_page(UI *);
void move_down_page(UI *);
void goto_bol(UI *);
void goto_eol(UI *);
int handle_char(UI *, int);
void init_font(UI *);
void handle_wm_create(UI *, LPCREATESTRUCT);
int open_file(UI *, TCHAR *);
void update_ui(UI *);
void init_lua(UI *);
void update_monoedit_tags(UI *);
void move_forward(UI *);
void move_backward(UI *);
void move_next_field(UI *);
void move_prev_field(UI *);

LRESULT CALLBACK
med_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	UI *ui = (UI *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
	Whex *w = ui->whex;

	switch (msg) {
	case WM_LBUTTONDOWN:
		{
			int x = LOWORD(lparam);
			int y = HIWORD(lparam);
			SetFocus(hwnd);
			int cx = x / ui->charwidth;
			int cy = y / ui->charheight;
			if (cx >= 10 && cx < 10+N_COL*3 && cy < ui->nrow) {
				cx = (cx-10)/3;
				long long pos = ((ui->current_line + cy) << LOG2_N_COL) + cx;
				if (pos < w->file_size) {
					ui->cursor_x = cx;
					ui->cursor_y = cy;
					update_cursor_pos(ui);
				}
			}
		}
		return 0;
	case WM_KEYDOWN:
		switch (wparam) {
		case VK_UP:
			move_up(ui);
			break;
		case VK_DOWN:
			move_down(ui);
			break;
		case VK_LEFT:
			move_left(ui);
			break;
		case VK_RIGHT:
			move_right(ui);
			break;
		case VK_PRIOR:
			move_up_page(ui);
			break;
		case VK_NEXT:
			move_down_page(ui);
			break;
		case VK_HOME:
			goto_bol(ui);
			break;
		case VK_END:
			goto_eol(ui);
			break;
		}
		return 0;
	case WM_MOUSEWHEEL:
		{
			int delta = (short) HIWORD(wparam);
			if (delta > 0) {
				int n = delta / WHEEL_DELTA;
				while (n--) {
					scroll_up_line(ui);
				}
			} else {
				int n = (-delta) / WHEEL_DELTA;
				while (n--) {
					scroll_down_line(ui);
				}
			}
		}
		return 0;
	}
	return CallWindowProc(ui->med_wndproc, hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK
cmdedit_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	UI *ui = (UI *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

	switch (msg) {
	case WM_CHAR:
		switch (wparam) {
			char *cmd;
			TCHAR *tcmd;
			int buflen;
			const char *errmsg;
			void *top;
		case '\r':
			buflen = GetWindowTextLength(hwnd)+1;
			top = ui->rgn->cur;
			tcmd = ralloc(ui->rgn, buflen * sizeof *tcmd);
			GetWindowText(hwnd, tcmd, buflen);
#ifdef UNICODE
			cmd = utf16_to_mbcs(ui->rgn, tcmd);
#else
			cmd = tcmd;
#endif
			errmsg = 0;
			switch (cmd[0]) {
			case '/':
				//execute_command(w, cmd_find_text, cmd+1);
				break;
			case ':':
				//errmsg = parse_and_execute_command(w, cmd+1);
				break;
			case '\\':
				//execute_command(w, cmd_find_hex, cmd+1);
				break;
			case 'g':
				//execute_command(w, cmd_goto, cmd+1);
				break;
			}
			rfree(ui->rgn, top);
			if (errmsg) {
				error_prompt(ui, errmsg);
			}
			// fallthrough
		case 27: // escape
			SetWindowText(hwnd, TEXT(""));
			SetFocus(ui->monoedit);
			return 0;
		}
	}

	return CallWindowProc(ui->cmdedit_wndproc, hwnd, msg, wparam, lparam);
}

ATOM
register_wndclass(void)
{
	WNDCLASS wndclass = {0};

	wndclass.lpfnWndProc = wndproc;
	wndclass.cbWndExtra = sizeof(LONG_PTR);
	wndclass.hInstance = GetModuleHandle(0);
	wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	wndclass.lpszClassName = TEXT("WHEX");
	return RegisterClass(&wndclass);
}

static void
format_error_code(TCHAR *buf, size_t buflen, DWORD error_code)
{
#if 0
   DWORD FormatMessage(
    DWORD dwFlags,	// source and processing options
    LPCVOID lpSource,	// pointer to message source
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

static int
file_chooser_dialog(TCHAR *buf, int buflen)
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

static int
start_gui(HINSTANCE instance, int show, UI *ui, TCHAR *filepath)
{
	InitCommonControls();
	if (!med_register_class()) return 1;
	if (!register_wndclass()) return 1;
	if (open_file(ui, filepath) < 0) return 1;
	init_font(ui);
	//RECT rect = { 0, 0, ui->charwidth*N_COL_CHAR, ui->charheight*(INITIAL_N_ROW+1) };
	//AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindow(TEXT("WHEX"), // class name
				 0, // window title
				 WS_OVERLAPPEDWINDOW, // window style
				 CW_USEDEFAULT, // initial x position
				 CW_USEDEFAULT, // initial y position
				 CW_USEDEFAULT, // initial width
				 CW_USEDEFAULT, // initial height
				 0,
				 0,
				 instance,
				 ui); // window-creation data
	ShowWindow(hwnd, show);
	update_ui(ui);
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	// window closed now
	return msg.wParam;
}

/* TODO: handle backslash */
static TCHAR **
cmdline_to_argv(Region *r, TCHAR *cmdline, int *argc)
{
	int narg = 0;
	List head = {0};
	List *last = &head;
	TCHAR *p = cmdline;
	TCHAR *q;
	TCHAR *arg;
	do {
		int n;
		q = p;
		if (*q == '"') {
			p++;
			do q++; while (*q && *q != '"');
			/* argument between p and q */
			n = q-p;
			if (*q) q++;
		} else {
			while (*q && !isspace(*q)) q++;
			/* argument between p and q */
			n = q-p;
		}
		narg++;
		arg = ralloc(r, (n+1) * sizeof *arg);
		memcpy(arg, p, n*sizeof(TCHAR));
		arg[n] = 0;
		APPEND(last, arg, r);
		p = q;
		while (isspace(*p)) p++;
	} while (*p);
	int i=0;
	TCHAR **argv = ralloc(r, (narg+1) * sizeof *argv);
	FOREACH(head.next, arg) argv[i++] = arg;
	argv[narg] = 0;
	*argc = narg;
	return argv;
}

TCHAR *
lstrdup(const TCHAR *s)
{
	int nb = (lstrlen(s)+1) * sizeof(TCHAR);
	TCHAR *ret = malloc(nb);
	memcpy(ret, s, nb);
	return ret;
}

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE _prev_instance, LPSTR _cmdline, int show)
{
	TCHAR openfilename[BUFSIZE];
	int argc;
	TCHAR **argv;
	TCHAR *filepath;
	Region rgn;

	rinit(&rgn);
	void *top = rgn.cur;
	argv = cmdline_to_argv(&rgn, GetCommandLine(), &argc);

	/* parse command line arguments */
	if (argc == 1) {
		if (file_chooser_dialog(openfilename, BUFSIZE))
			return 1;
		filepath = openfilename;
	} else if (argc == 2) {
		filepath = argv[1];
	} else {
		return 1;
	}

	filepath = lstrdup(filepath);
	rfree(&rgn, top);

	Whex *w = ralloc(&rgn, sizeof *w);
	UI *ui = ralloc0(&rgn, sizeof *ui);
	ui->whex = w;
	ui->rgn = &rgn;
	init_lua(ui);

	return start_gui(instance, show, ui, filepath);
}

void
update_monoedit_buffer(UI *ui, int buffer_line, int num_lines)
{
	Whex *w = ui->whex;
	long long abs_line = ui->current_line + buffer_line;
	long long abs_line_end = abs_line + num_lines;
	while (abs_line < abs_line_end) {
		TCHAR *line = ui->med_buffer + buffer_line*N_COL_CHAR;
		TCHAR *p = line;
		long long addr = abs_line << LOG2_N_COL;
		if (abs_line >= ui->total_lines) {
			for (int i=0; i<N_COL_CHAR; i++) {
				p[i] = ' ';
			}
		} else {
			int block = whex_find_cache(w, addr);
			int base = addr & (CACHE_BLOCK_SIZE-1);
			wsprintf(p, TEXT("%08I64x:"), addr);
			p += 9;
			int end = 0;
		       	if (abs_line+1 >= ui->total_lines) {
				end = w->file_size & (N_COL-1);
			}
			if (!end) {
				end = N_COL;
			}
			for (int j=0; j<end; j++) {
				//*p++ = j && !(j&7) ? '-' : ' ';
				wsprintf(p, TEXT(" %02x"), w->cache[block].data[base|j]);
				p += 3;
			}
			for (int j=end; j<N_COL; j++) {
				wsprintf(p, TEXT("   "));
				p += 3;
			}
			wsprintf(p, TEXT("  "));
			p += 2;
			for (int j=0; j<end; j++) {
				uint8_t b = w->cache[block].data[base|j];
				*p++ = b < 0x20 || b > 0x7e ? '.' : b;
			}
			// fill the rest of the line with spaces
			for (int j = p-line; j < N_COL_CHAR; j++) {
				line[j] = ' ';
			}
		}
		buffer_line++;
		abs_line++;
	}
}

void
set_current_line(UI *ui, long long line)
{
	assert(line <= ui->total_lines);
	ui->current_line = line;
	update_monoedit_buffer(ui, 0, ui->nrow);
	update_monoedit_tags(ui);
	update_cursor_pos(ui);
	InvalidateRect(ui->monoedit, 0, FALSE);
}

void
update_status_text(UI *ui, struct tree *leaf)
{
	Whex *w = ui->whex;
	const TCHAR *type_name = TEXT("unknown");
	TCHAR value_buf[80];
	value_buf[0] = 0;
	char *path = 0;
	TCHAR *tpath = 0;
	void *top = ui->rgn->cur;
	if (leaf) {
		switch (leaf->type) {
			TCHAR *p;
		case F_RAW:
			type_name = TEXT("raw");
			break;
		case F_UINT:
			switch (leaf->len) {
				int ival;
				int ival_hi;
				long long llval;
			case 1:
				type_name = TEXT("uint8");
				ival = whex_getbyte(w, leaf->start);
				wsprintf(value_buf, TEXT("%u (%02x)"), ival, ival);
				break;
			case 2:
				type_name = TEXT("uint16");
				ival = whex_getbyte(w, leaf->start) |
					whex_getbyte(w, leaf->start + 1) << 8;
				wsprintf(value_buf, TEXT("%u (%04x)"), ival, ival);
				break;
			case 4:
				type_name = TEXT("uint32");
				ival = whex_getbyte(w, leaf->start)
					| whex_getbyte(w, leaf->start + 1) << 8
					| whex_getbyte(w, leaf->start + 2) << 16
					| whex_getbyte(w, leaf->start + 3) << 24;
				wsprintf(value_buf, TEXT("%u (%08x)"), ival, ival);
				break;
			case 8:
				type_name = TEXT("uint64");
				ival = whex_getbyte(w, leaf->start)
					| whex_getbyte(w, leaf->start + 1) << 8
					| whex_getbyte(w, leaf->start + 2) << 16
					| whex_getbyte(w, leaf->start + 3) << 24;
				ival_hi = whex_getbyte(w, leaf->start + 4)
					| whex_getbyte(w, leaf->start + 5) << 8
					| whex_getbyte(w, leaf->start + 6) << 16
					| whex_getbyte(w, leaf->start + 7) << 24;
				llval = ((long long) ival_hi) << 32 | ival;
				wsprintf(value_buf, TEXT("%I64u (%016I64x)"), llval, llval);
				break;
			default:
				type_name = TEXT("uint");
			}
			break;
		case F_INT:
			// TODO
			type_name = TEXT("int");
			break;
		case F_ASCII:
			type_name = TEXT("ascii");
			p = value_buf;
			int n = leaf->len;
			if (n > 16) n = 16;
			*p++ = '"';
			for (int i=0; i<n; i++) {
				uint8_t b = whex_getbyte(w, leaf->start + i);
				if (b >= 0x20 && b < 0x7f) {
					*p++ = b;
				} else {
					wsprintf(p, TEXT("\\x%02x"), b);
					p += 4;
				}
			}
			*p++ = '"';
			if (n < leaf->len) {
				wsprintf(p, TEXT("..."));
			}
			break;
		}
		path = tree_path(ui->rgn, leaf);
#ifdef UNICODE
		tpath = mbcs_to_utf16(ui->rgn, path);
#else
		tpath = path;
#endif
	}
	TCHAR cursor_pos_buf[17];
	wsprintf(cursor_pos_buf, TEXT("%I64x"), cursor_pos(ui));
	SendMessage(ui->status_bar, SB_SETTEXT, 0, (LPARAM) cursor_pos_buf);
	SendMessage(ui->status_bar, SB_SETTEXT, 1, (LPARAM) type_name);
	SendMessage(ui->status_bar, SB_SETTEXT, 2, (LPARAM) value_buf);
	SendMessage(ui->status_bar, SB_SETTEXT, 3, (LPARAM) tpath);
	rfree(ui->rgn, top);
}

// should be invoked when cursor_pos is changed
void
update_field_info(UI *ui)
{
	Whex *w = ui->whex;
	if (w->tree) {
		struct tree *leaf = tree_lookup(w->tree, cursor_pos(ui));
		if (leaf) {
			ui->hl_start = leaf->start;
			ui->hl_len = leaf->len;
		} else {
			ui->hl_start = 0;
			ui->hl_len = 0;
		}
		update_monoedit_tags(ui);
		InvalidateRect(ui->monoedit, 0, FALSE);
		update_status_text(ui, leaf);
	} else {
		update_status_text(ui, 0);
	}
}

void
update_cursor_pos(UI *ui)
{
	SendMessage(ui->monoedit,
		    MED_WM_SET_CURSOR_POS,
		    10+ui->cursor_x*3,
		    ui->cursor_y);
	update_field_info(ui);
	SendMessage(ui->monoedit, MED_WM_SET_CURSOR_POS,
		    10+ui->cursor_x*3, ui->cursor_y);
}

long long
cursor_pos(UI *ui)
{
	return ((ui->current_line + ui->cursor_y) << LOG2_N_COL) + ui->cursor_x;
}

void
goto_address(UI *ui, long long addr)
{
	Whex *w = ui->whex;
	long long line = addr >> LOG2_N_COL;
	int col = addr & (N_COL-1);
	assert(addr >= 0 && addr < w->file_size);
	ui->cursor_x = col;
	if (line >= ui->current_line && line < ui->current_line + ui->nrow) {
		ui->cursor_y = line - ui->current_line;
		update_cursor_pos(ui);
	} else {
		long long line1;
		if (line >= ui->nrow >> 1) {
			line1 = line - (ui->nrow >> 1);
		} else {
			line1 = 0;
		}
		ui->cursor_y = line - line1;
		set_current_line(ui, line1);
	}
}

#if 0
char *
find(UI *ui, char *arg, bool istext)
{
	Whex *w = ui->whex;
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
			if (patlen == 0) {
				return "empty pattern";
			}
			pat = start;
		}
	} else {
		int slen = strlen((char*)p);
		if (slen == 0) {
			return "empty pattern";
		}
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
	if (ui->last_search_pattern) {
		free(ui->last_search_pattern);
	}
	ui->last_search_pattern = malloc(patlen);
	memcpy(ui->last_search_pattern, pat, patlen);
	ui->last_search_pattern_len = patlen;
	long long matchpos = whex_kmp_search(w, pat, patlen, cursor_pos(ui));
	if (!istext) {
		free(pat);
	}
	if (matchpos == w->file_size) {
		return "pattern not found";
	}
	goto_address(ui, matchpos);
	return 0;
}

char *
repeat_search(UI *ui, bool reverse)
{
	Whex *w = ui->whex;
	if (!w->last_search_pattern) {
		return "no previous pattern";
	}
	long long (*search_func)(Whex *w, const uint8_t *pat, int patlen, long long start);
	long long start;
	if (reverse) {
		long long tmp = cursor_pos(ui) + w->last_search_pattern_len - 1;
		if (w->file_size >= tmp) {
			start = w->file_size - tmp;
		} else {
			return "pattern not found";
		}
		search_func = whex_kmp_search_backward;
	} else {
		long long cursor_pos = cursor_pos(ui);
		if (cursor_pos+1 < w->file_size) {
			start = cursor_pos+1;
		} else {
			return "pattern not found";
			//start = 0;
		}
		search_func = whex_kmp_search;
	}
	long long matchpos = search_func(w, w->last_search_pattern,
					w->last_search_pattern_len,
					start);
	if (matchpos == w->file_size) {
		return "pattern not found";
	}
	goto_address(w, matchpos);
	return 0;
}
#endif

void
error_prompt(UI *ui, const char *errmsg)
{
	const TCHAR *terrmsg;
#ifdef UNICODE
	void *top = ui->rgn->cur;
	terrmsg = mbcs_to_utf16(ui->rgn, errmsg);
#else
	terrmsg = errmsg;
#endif
	MessageBox(ui->hwnd, terrmsg, TEXT("Error"), MB_ICONERROR);
#ifdef UNICODE
	rfree(ui->rgn, top);
#endif
}

void
scroll_up_line(UI *ui)
{
	if (ui->current_line) {
		ui->current_line--;
		SendMessage(ui->monoedit, MED_WM_SCROLL, 0, -1);
		memmove(ui->med_buffer+N_COL_CHAR, ui->med_buffer, N_COL_CHAR*(ui->nrow-1)*sizeof(TCHAR));
		update_monoedit_buffer(ui, 0, 1);
		update_monoedit_tags(ui);
		update_cursor_pos(ui);
	}
}

void
scroll_down_line(UI *ui)
{
	if (cursor_pos(ui) + N_COL < ui->whex->file_size) {
		ui->current_line++;
		SendMessage(ui->monoedit, MED_WM_SCROLL, 0, 1);
		memmove(ui->med_buffer, ui->med_buffer+N_COL_CHAR, N_COL_CHAR*(ui->nrow-1)*sizeof(TCHAR));
		update_monoedit_buffer(ui, ui->nrow-1, 1);
		update_monoedit_tags(ui);
		update_cursor_pos(ui);
	}
}

void
move_up(UI *ui)
{
	if (cursor_pos(ui) >= N_COL) {
		if (ui->cursor_y) {
			ui->cursor_y--;
			update_cursor_pos(ui);
		} else {
			scroll_up_line(ui);
		}
	}
}

void
move_down(UI *ui)
{
	long long filesize = ui->whex->file_size;
	if (filesize >= N_COL && cursor_pos(ui) < filesize - N_COL) {
		if (ui->cursor_y < ui->nrow-1) {
			ui->cursor_y++;
			update_cursor_pos(ui);
		} else {
			scroll_down_line(ui);
		}
	}
}

void
move_left(UI *ui)
{
	if (ui->cursor_x) {
		ui->cursor_x--;
		update_cursor_pos(ui);
	}
}

void
move_right(UI *ui)
{
	if (ui->cursor_x < (N_COL-1) && cursor_pos(ui)+1 < ui->whex->file_size) {
		ui->cursor_x++;
		update_cursor_pos(ui);
	}
}

void
scroll_up_page(UI *ui)
{
	if (ui->current_line >= ui->nrow) {
		ui->current_line -= ui->nrow;
		update_monoedit_buffer(ui, 0, ui->nrow);
		update_monoedit_tags(ui);
		update_cursor_pos(ui);
		InvalidateRect(ui->monoedit, 0, FALSE);
	} else {
		long long delta = ui->current_line;
		ui->current_line = 0;
		SendMessage(ui->monoedit, MED_WM_SCROLL, 0, -delta);
		memmove(ui->med_buffer+N_COL_CHAR*delta, ui->med_buffer, N_COL_CHAR*(ui->nrow-delta)*sizeof(TCHAR));
		update_monoedit_buffer(ui, 0, delta);
		update_monoedit_tags(ui);
		update_cursor_pos(ui);
	}
}

void
scroll_down_page(UI *ui)
{
	if (ui->current_line + ui->nrow <= ui->total_lines) {
		ui->current_line += ui->nrow;
		update_monoedit_buffer(ui, 0, ui->nrow);
		update_monoedit_tags(ui);
		update_cursor_pos(ui);
		InvalidateRect(ui->monoedit, 0, FALSE);
	} else {
		long long delta = ui->total_lines - ui->current_line;
		ui->current_line = ui->total_lines;
		SendMessage(ui->monoedit, MED_WM_SCROLL, 0, delta);
		memmove(ui->med_buffer, ui->med_buffer+N_COL_CHAR*delta, N_COL_CHAR*(ui->nrow-delta)*sizeof(TCHAR));
		update_monoedit_buffer(ui, ui->nrow-delta, delta);
		update_monoedit_tags(ui);
		update_cursor_pos(ui);
	}
}

void
move_up_page(UI *ui)
{
	if (cursor_pos(ui) >= N_COL*ui->nrow) {
		scroll_up_page(ui);
	}
}

void
move_down_page(UI *ui)
{
	long long filesize = ui->whex->file_size;
	if (filesize >= N_COL*ui->nrow && cursor_pos(ui) < filesize - N_COL*ui->nrow) {
		scroll_down_page(ui);
	}
}

int
handle_char(UI *ui, int c)
{
	switch (c) {
		TCHAR buf[2];
	case 8: // backspace
		move_backward(ui);
		break;
	case ' ':
		move_forward(ui);
		break;
	case '/':
	case ':':
	case '\\':
	case 'g':
		buf[0] = c;
		buf[1] = 0;
		SetWindowText(ui->cmdedit, buf);
		SetFocus(ui->cmdedit);
		// place caret at end of text
		SendMessage(ui->cmdedit, EM_SETSEL, 1, 1);
		return 1;
	case 'N':
		//execute_command(ui, cmd_findprev, 0);
		return 1;
	case 'b':
		move_prev_field(ui);
		return 1;
	case 'h':
		move_left(ui);
		return 1;
	case 'j':
		move_down(ui);
		return 1;
	case 'k':
		move_up(ui);
		return 1;
	case 'l':
		move_right(ui);
		return 1;
	case 'n':
		//execute_command(w, cmd_findnext, 0);
		return 1;
	case 'w':
		move_next_field(ui);
		return 1;
	}
	return 1;
}

void
init_font(UI *ui)
{
	static LOGFONT logfont = {
		.lfHeight = 16,
		.lfFaceName = TEXT("Courier New"),
	};

	HDC dc;
	TEXTMETRIC tm;
	HFONT mono_font;

	//mono_font = GetStockObject(OEM_FIXED_FONT);
	mono_font = CreateFontIndirect(&logfont);
	dc = GetDC(0);
	SelectObject(dc, mono_font);
	GetTextMetrics(dc, &tm);
	ui->mono_font = mono_font;
	ui->charwidth = tm.tmAveCharWidth;
	ui->charheight = tm.tmHeight;
	ReleaseDC(0, dc);
}

void
handle_wm_create(UI *ui, LPCREATESTRUCT create)
{
	HWND hwnd = ui->hwnd;
	HWND monoedit;
	HWND cmdedit;
	HWND status_bar;

	HINSTANCE instance = create->hInstance;
	/* create and initialize MonoEdit */
	monoedit = CreateWindow(TEXT("MonoEdit"),
				TEXT(""),
				WS_CHILD | WS_VISIBLE,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				hwnd,
				0,
				instance,
				0);
	ui->monoedit = monoedit;
	ui->nrow = INITIAL_N_ROW;
	ui->med_buffer = malloc(N_COL_CHAR*INITIAL_N_ROW*sizeof(TCHAR));
	ui->med_buffer_nrow = INITIAL_N_ROW;
	for (int i=0; i<N_COL_CHAR*INITIAL_N_ROW; i++) {
		ui->med_buffer[i] = ' ';
	}
	SendMessage(monoedit, MED_WM_SET_CSIZE, N_COL_CHAR, INITIAL_N_ROW);
	SendMessage(monoedit, MED_WM_SET_BUFFER, 0, (LPARAM) ui->med_buffer);
	SendMessage(monoedit, WM_SETFONT, (WPARAM) ui->mono_font, 0);
	update_monoedit_buffer(ui, 0, INITIAL_N_ROW);
	/* subclass monoedit window */
	SetWindowLongPtr(monoedit, GWLP_USERDATA, (LONG_PTR) ui);
	ui->med_wndproc = (WNDPROC) SetWindowLongPtr(monoedit, GWLP_WNDPROC, (LONG_PTR) med_wndproc);
	SetFocus(monoedit);
	update_cursor_pos(ui);

	/* create command window */
	cmdedit = CreateWindow(TEXT("EDIT"),
			       TEXT(""),
			       WS_CHILD | WS_VISIBLE,
			       CW_USEDEFAULT,
			       CW_USEDEFAULT,
			       CW_USEDEFAULT,
			       CW_USEDEFAULT,
			       hwnd,
			       0,
			       instance,
			       0);
	SendMessage(cmdedit, WM_SETFONT, (WPARAM) ui->mono_font, 0);
	ui->cmdedit = cmdedit;
	// subclass command window
	SetWindowLongPtr(cmdedit, GWLP_USERDATA, (LONG_PTR) ui);
	ui->cmdedit_wndproc = (WNDPROC) SetWindowLongPtr(cmdedit, GWLP_WNDPROC, (LONG_PTR) cmdedit_wndproc);

	// create status bar
	status_bar = CreateStatusWindow(WS_CHILD | WS_VISIBLE,
					NULL,
					hwnd,
					IDC_STATUS_BAR);
	int parts[] = { 64, 128, 256, -1 };
	SendMessage(status_bar, SB_SETPARTS, 4, (LPARAM) parts);
	ui->status_bar = status_bar;

	// get height of status bar
	RECT rect;
	GetWindowRect(status_bar, &rect);
	int status_bar_height = rect.bottom - rect.top;

	// adjust size of main window
	GetWindowRect(hwnd, &rect);
	rect.right = rect.left + ui->charwidth * N_COL_CHAR;
	rect.bottom = rect.top +
		ui->charheight * INITIAL_N_ROW + // MonoEdit
		ui->charheight + // command window
		status_bar_height; // status bar
	AdjustWindowRect(&rect, GetWindowLongPtr(hwnd, GWL_STYLE), FALSE);
	SetWindowPos(hwnd, 0, 0, 0,
		     rect.right - rect.left,
		     rect.bottom - rect.top,
		     SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);

	SetWindowText(hwnd, TEXT("WHEX"));
}

void
resize_monoedit(UI *ui, int width, int height)
{
	int new_nrow = height/ui->charheight;
	if (new_nrow > ui->nrow) {
		if (new_nrow > ui->med_buffer_nrow) {
			ui->med_buffer = realloc(ui->med_buffer, N_COL_CHAR*new_nrow*sizeof(TCHAR));
			ui->med_buffer_nrow = new_nrow;
			SendMessage(ui->monoedit, MED_WM_SET_BUFFER, 0, (LPARAM) ui->med_buffer);
		}
		update_monoedit_buffer(ui, ui->nrow, new_nrow - ui->nrow);
		update_monoedit_tags(ui);
	}
	ui->nrow = new_nrow;
	SendMessage(ui->monoedit, MED_WM_SET_CSIZE, -1, height/ui->charheight);
	SetWindowPos(ui->monoedit,
		     0,
		     0,
		     0,
		     width,
		     height,
		     SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);
	InvalidateRect(ui->monoedit, 0, FALSE);
}

LRESULT CALLBACK
wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	UI *ui = (UI *) GetWindowLongPtr(hwnd, 0);

	switch (msg) {
	case WM_NCCREATE:
		ui = ((LPCREATESTRUCT)lparam)->lpCreateParams;
		ui->hwnd = hwnd;
		SetWindowLongPtr(hwnd, 0, (LONG_PTR) ui);
		return TRUE;
	case WM_NCDESTROY:
		if (ui) {
			free(ui);
		}
		return 0;
	case WM_CREATE:
		handle_wm_create(ui, (LPCREATESTRUCT) lparam);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_SIZE:
		{
			// adjust status bar geometry automatically
			SendMessage(ui->status_bar, WM_SIZE, 0, 0);
			RECT status_bar_rect;
			GetWindowRect(ui->status_bar, &status_bar_rect);
			// translate top-left from screen to client
			ScreenToClient(ui->hwnd, (LPPOINT) &status_bar_rect);
			int width  = LOWORD(lparam);
			//int height = HIWORD(lparam);
			int cmd_y = status_bar_rect.top - ui->charheight;
			if (cmd_y < 0) cmd_y = 0;
			resize_monoedit(ui, width, cmd_y);
			SetWindowPos(ui->cmdedit,
				     0,
				     0,
				     cmd_y,
				     width,
				     ui->charheight,
				     SWP_NOZORDER);
		}
		return 0;
	case WM_CHAR:
		if (handle_char(ui, wparam)) {
		} else {
		}
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

// usually you want to update UI after this
int
open_file(UI *ui, TCHAR *path)
{
	static char errfmt_open[] = "Failed to open %s: %s\n";

	TCHAR errtext[BUFSIZE];
	Whex *w = ui->whex;

	HANDLE file;
	file = CreateFile(path,
			  GENERIC_READ,
			  FILE_SHARE_READ,
			  0, // lpSecurityAttributes
			  OPEN_EXISTING,
			  0,
			  0);
	if (file == INVALID_HANDLE_VALUE) {
		format_error_code(errtext, BUFSIZE, GetLastError());
		fprintf(stderr, errfmt_open, path, errtext);
		return -1;
	}
	if (whex_init(ui->whex, file) < 0) {
		CloseHandle(file);
		return -1;
	}
	DEBUG_PRINTF("file size: %I64u (0x%I64x)\n", w->file_size, w->file_size);
	ui->filepath = path;
	ui->total_lines = w->file_size >> LOG2_N_COL;
	if (w->file_size&(N_COL-1)) {
		ui->total_lines += 1;
	}
	return 0;
}

void
init_lua(UI *ui)
{
	// try not to initialize twice
	assert(!ui->lua);
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	// store ui at REGISTRY[0]
	lua_pushinteger(L, 0);
	lua_pushlightuserdata(L, ui);
	lua_settable(L, LUA_REGISTRYINDEX);
	// register C functions
	//register_lua_globals(L);
	ui->lua = L;
}

static long long
clamp(long long x, long long min, long long max)
{
	if (x < min) return min;
	if (x > max) return max;
	return x;
}

void
update_monoedit_tags(UI *ui)
{
	long long start = ui->hl_start;
	long long len = ui->hl_len;
	long long view_start = ui->current_line * N_COL;
	long long view_end = (ui->current_line + ui->nrow) * N_COL;
	long long start_clamp = clamp(start, view_start, view_end) - view_start;
	long long end_clamp = clamp(start + len, view_start, view_end) - view_start;
	HWND w1 = ui->monoedit;
	SendMessage(w1, MED_WM_CLEAR_TAGS, 0, 0);
	if (end_clamp > start_clamp) {
		MedTag tag;
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
			SendMessage(w1, MED_WM_ADD_TAG, 0, (LPARAM) &tag);
			for (int i=tag_first_line+1; i<tag_last_line; i++) {
				tag.line = i;
				tag.start = 10;
				tag.len = N_COL*3-1;
				SendMessage(w1, MED_WM_ADD_TAG, 0, (LPARAM) &tag);
			}
			tag.line = tag_last_line;
			tag.start = 10;
			tag.len = end_x * 3 - 1;
			SendMessage(w1, MED_WM_ADD_TAG, 0, (LPARAM) &tag);
		} else {
			// single line
			tag.line = tag_first_line;
			tag.start = 10 + (start_clamp & (N_COL-1)) * 3;
			tag.len = (end_x - (start_clamp & (N_COL-1))) * 3 - 1;
			SendMessage(w1, MED_WM_ADD_TAG, 0, (LPARAM) &tag);
		}
	}
}

void
update_ui(UI *ui)
{
	update_monoedit_buffer(ui, 0, ui->nrow);
	update_monoedit_tags(ui);
	update_cursor_pos(ui);
	InvalidateRect(ui->monoedit, 0, FALSE);
}

void
move_forward(UI *ui)
{
	Whex *w = ui->whex;
	if (cursor_pos(ui) < w->file_size) {
		if (ui->cursor_x < N_COL-1) {
			move_right(ui);
		} else {
			ui->cursor_x = 0;
			move_down(ui);
		}
	}
}

void
move_backward(UI *ui)
{
	if (cursor_pos(ui) > 0) {
		if (ui->cursor_x > 0) {
			move_left(ui);
		} else {
			ui->cursor_x = N_COL-1;
			move_up(ui);
		}
	}
}

void
move_next_field(UI *ui)
{
	Whex *w = ui->whex;
	if (w->tree) {
		long long cur = cursor_pos(ui);
		struct tree *leaf = tree_lookup(w->tree, cur);
		if (leaf) {
			goto_address(ui, leaf->start + leaf->len);
		}
	}
}

void
move_prev_field(UI *ui)
{
	Whex *w = ui->whex;
	if (w->tree) {
		long long cur = cursor_pos(ui);
		struct tree *leaf = tree_lookup(w->tree, cur);
		if (leaf) {
			struct tree *prev_leaf = tree_lookup(w->tree, cur-1);
			if (prev_leaf) {
				goto_address(ui, prev_leaf->start);
			}
		}
	}
}

void
goto_bol(UI *ui)
{
	if (ui->whex->file_size > 0) {
		goto_address(ui, (ui->current_line + ui->cursor_y) * N_COL);
	}
}

void
goto_eol(UI *ui)
{
	long long filesize = ui->whex->file_size;
	if (filesize > 0) {
		long long addr = (ui->current_line + ui->cursor_y + 1) * N_COL - 1;
		if (addr >= filesize) {
			addr = filesize-1;
		}
		goto_address(ui, addr);
	}
}
