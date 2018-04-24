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
#include "buffer.h"
#include "ui.h"
#include "monoedit.h"
#include "tree.h"
#include "unicode.h"
#include "resource.h"
#include "printf.h"

#include <commctrl.h>

#define INITIAL_N_ROW 32
#define LOG2_N_COL 4
#define N_COL (1<<LOG2_N_COL)
#define N_COL_CHAR (16+4*N_COL)

#define BUFSIZE 512

#define NELEM(x) (sizeof(x)/sizeof(*x))

enum {
	IDC_STATUS_BAR = 0x100,
	IDM_FILE_OPEN,
	IDM_FILE_SAVE,
	IDM_FILE_SAVEAS,
	IDM_FILE_CLOSE,
	IDM_TOOLS_LOAD_PLUGIN,
	IDM_PLUGIN_0,
};

Region rgn;
TCHAR mydir[MAX_PATH];
int mydir_len;

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

typedef struct {
	TCHAR *text;
	TCHAR *title;
} InputBoxConfig;

LRESULT CALLBACK med_wndproc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
static ATOM register_wndclass(void);
static void format_error_code(TCHAR *, size_t, DWORD);
int file_chooser_dialog(HWND, TCHAR *, int);
static int start_gui(int, UI *, TCHAR *);
void update_monoedit_buffer(UI *, int, int);
void set_current_line(UI *, long long);
static void update_window_title(UI *ui);
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
void handle_char_normal(UI *, int);
void handle_char_replace(UI *, int);
void init_font(UI *);
void handle_wm_create(UI *, LPCREATESTRUCT);
int open_file(UI *, TCHAR *);
void close_file(UI *);
void update_ui(UI *);
void update_monoedit_tags(UI *);
void move_forward(UI *);
void move_backward(UI *);
void move_next_field(UI *);
void move_prev_field(UI *);
TCHAR *inputbox(UI *, TCHAR *title);
bool parse_addr(TCHAR *, long long *);
void errorbox(HWND, TCHAR *);
void msgbox(HWND, const char *, ...);
Tree *convert_tree(Region *, lua_State *);
int load_plugin(UI *, const char *);
int api_buffer_peek(lua_State *L);
int api_buffer_peekstr(lua_State *L);
int api_buffer_tree(lua_State *L);
void getluaobj(lua_State *L, const char *name);
void luaerrorbox(HWND hwnd, lua_State *L);
int init_luatk(lua_State *L);
void update_plugin_menu(UI *ui);
int load_filetype_plugin(UI *, const TCHAR *);
int save_file(UI *);

LRESULT CALLBACK
med_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	UI *ui = (UI *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

	switch (msg) {
	case WM_LBUTTONDOWN:
		{
			int x = LOWORD(lparam);
			int y = HIWORD(lparam);
			int cx = x / ui->charwidth;
			int cy = y / ui->charheight;
			SetFocus(hwnd);
			if (cx >= 10 && cx < 10+N_COL*3 && cy < ui->nrow) {
				bool lownib = (cx-10)%3;
				cx = (cx-10)/3;
				long long pos = ((ui->current_line + cy) << LOG2_N_COL) + cx;
				if (pos < ui->buffer->file_size) {
					ui->cursor_x = cx;
					ui->cursor_y = cy;
					ui->cursor_at_low_nibble = lownib;
					ui->handle_char = handle_char_normal;
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
		case VK_ESCAPE:
			ui->handle_char = handle_char_normal;
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

static ATOM
register_wndclass(void)
{
	WNDCLASS wndclass = {0};

	wndclass.lpfnWndProc = wndproc;
	wndclass.cbWndExtra = sizeof(LONG_PTR);
	wndclass.hInstance = GetModuleHandle(0);
	wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	wndclass.lpszClassName = TEXT("WHEX");
	return RegisterClass(&wndclass);
}

static void
format_error_code(TCHAR *buf, size_t buflen, DWORD error_code)
{
#if 0
	DWORD FormatMessage
		(
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

static HMENU
create_menu(void)
{
	HMENU mainmenu, m;

	mainmenu = CreateMenu();

	m = CreateMenu();
	AppendMenu(m, MF_STRING, IDM_FILE_OPEN, TEXT("Open..."));
	AppendMenu(m, MF_STRING, IDM_FILE_SAVE, TEXT("Save"));
	AppendMenu(m, MF_STRING, IDM_FILE_SAVEAS, TEXT("Save As..."));
	AppendMenu(m, MF_STRING, IDM_FILE_CLOSE, TEXT("Close"));
	AppendMenu(mainmenu, MF_POPUP, (UINT_PTR) m, TEXT("File"));

	m = CreateMenu();
	AppendMenu(m, MF_STRING, IDM_TOOLS_LOAD_PLUGIN, TEXT("Load Plugin..."));
	AppendMenu(mainmenu, MF_POPUP, (UINT_PTR) m, TEXT("Tools"));

	/* appearance will be overridden by update_ui() */
	AppendMenu(mainmenu, MF_SEPARATOR, 0, 0);

	return mainmenu;
}

/* filepath is owned */
static int
start_gui(int show, UI *ui, TCHAR *filepath)
{
	InitCommonControls();
	if (!med_register_class()) return 1;
	if (!register_wndclass()) return 1;
	int ret = open_file(ui, filepath);
	free(filepath);
	if (ret < 0) return 1;
	init_font(ui);
	//RECT rect = { 0, 0, ui->charwidth*N_COL_CHAR, ui->charheight*(INITIAL_N_ROW+1) };
	//AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	HMENU menu = create_menu();
	HWND hwnd = CreateWindow(TEXT("WHEX"), // class name
				 0, // window title
				 WS_OVERLAPPEDWINDOW, // window style
				 CW_USEDEFAULT, // initial x position
				 CW_USEDEFAULT, // initial y position
				 CW_USEDEFAULT, // initial width
				 CW_USEDEFAULT, // initial height
				 0,
				 menu,
				 ui->instance,
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

	AllocConsole();
	freopen("CON", "w", stdout);

	rinit(&rgn);
	void *top = rgn.cur;
	argv = cmdline_to_argv(&rgn, GetCommandLine(), &argc);

	/* parse command line arguments */
	if (argc == 1) {
		if (file_chooser_dialog(0, openfilename, BUFSIZE))
			return 1;
		filepath = openfilename;
	} else if (argc == 2) {
		filepath = argv[1];
	} else {
		return 1;
	}

	HMODULE thisexe = GetModuleHandle(0);
	GetModuleFileName(thisexe, mydir, NELEM(mydir));
	mydir_len = lstrlen(mydir);
	while (mydir_len >= 1 && mydir[mydir_len-1] != '\\')
		mydir_len--;
	mydir[mydir_len] = 0;

	filepath = lstrdup(filepath);
	rfree(&rgn, top);

	lua_State *L = luaL_newstate();
	luaL_openlibs(L);

	luaL_newmetatable(L, "buffer");
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);
	lua_rawset(L, -3);
	lua_pushstring(L, "peek");
	lua_pushcfunction(L, api_buffer_peek);
	lua_rawset(L, -3);
	lua_pushstring(L, "peekstr");
	lua_pushcfunction(L, api_buffer_peekstr);
	lua_rawset(L, -3);
	lua_pushstring(L, "tree");
	lua_pushcfunction(L, api_buffer_tree);
	lua_rawset(L, -3);
	lua_pop(L, 1);

	lua_newtable(L);
	lua_pushstring(L, "buffer");
	Buffer *b = lua_newuserdata(L, sizeof *b);
	luaL_setmetatable(L, "buffer");
	lua_rawset(L, -3);
	lua_pushstring(L, "plugin");
	lua_newtable(L);
	lua_rawset(L, -3);
	lua_setglobal(L, "whex");

	if (init_luatk(L)) return -1;

	lua_pushstring(L, "ftdet");
	TCHAR *ftdet_path = malloc((mydir_len + 11) * sizeof *ftdet_path);
	_wsprintf(ftdet_path, TEXT("%sftdet.lua"), mydir);
	if (luaL_dofile(L, ftdet_path)) {
		luaerrorbox(0, L);
		lua_pop(L, 2);
	} else {
		lua_rawset(L, LUA_REGISTRYINDEX);
	}
	free(ftdet_path);

	UI *ui = ralloc0(&rgn, sizeof *ui);
	ui->lua = L;
	ui->buffer = b;
	ui->instance = instance;
	ui->handle_char = handle_char_normal;

	return start_gui(show, ui, filepath);
}

void
update_monoedit_buffer(UI *ui, int buffer_line, int num_lines)
{
	Buffer *b = ui->buffer;
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
			uint8_t *data = buf_get_data(b, addr);
			int base = addr & (CACHE_BLOCK_SIZE-1);
			_wsprintf(p, TEXT("%08llx:"), addr);
			p += 9;
			int end = 0;
			if (abs_line+1 >= ui->total_lines) {
				end = b->file_size & (N_COL-1);
			}
			if (!end) {
				end = N_COL;
			}
			for (int j=0; j<end; j++) {
				//*p++ = j && !(j&7) ? '-' : ' ';
				_wsprintf(p, TEXT(" %02x"), data[base|j]);
				p += 3;
			}
			for (int j=end; j<N_COL; j++) {
				_wsprintf(p, TEXT("   "));
				p += 3;
			}
			_wsprintf(p, TEXT("  "));
			p += 2;
			for (int j=0; j<end; j++) {
				uint8_t c = data[base|j];
				*p++ = c < 0x20 || c > 0x7e ? '.' : c;
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
	Buffer *b = ui->buffer;
	const TCHAR *type_name = TEXT("unknown");
	TCHAR value_buf[80];
	value_buf[0] = 0;
	char *path = 0;
	TCHAR *tpath = 0;
	void *top = rgn.cur;
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
				ival = buf_getbyte(b, leaf->start);
				_wsprintf(value_buf, TEXT("%u (%02x)"), ival, ival);
				break;
			case 2:
				type_name = TEXT("uint16");
				ival = buf_getbyte(b, leaf->start) |
					buf_getbyte(b, leaf->start + 1) << 8;
				_wsprintf(value_buf, TEXT("%u (%04x)"), ival, ival);
				break;
			case 4:
				type_name = TEXT("uint32");
				ival = buf_getbyte(b, leaf->start)
					| buf_getbyte(b, leaf->start + 1) << 8
					| buf_getbyte(b, leaf->start + 2) << 16
					| buf_getbyte(b, leaf->start + 3) << 24;
				_wsprintf(value_buf, TEXT("%u (%08x)"), ival, ival);
				break;
			case 8:
				type_name = TEXT("uint64");
				ival = buf_getbyte(b, leaf->start)
					| buf_getbyte(b, leaf->start + 1) << 8
					| buf_getbyte(b, leaf->start + 2) << 16
					| buf_getbyte(b, leaf->start + 3) << 24;
				ival_hi = buf_getbyte(b, leaf->start + 4)
					| buf_getbyte(b, leaf->start + 5) << 8
					| buf_getbyte(b, leaf->start + 6) << 16
					| buf_getbyte(b, leaf->start + 7) << 24;
				llval = ((long long) ival_hi) << 32 | ival;
				_wsprintf(value_buf, TEXT("%llu (%016llx)"), llval, llval);
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
				uint8_t c = buf_getbyte(b, leaf->start + i);
				if (c >= 0x20 && c < 0x7f) {
					*p++ = c;
				} else {
					_wsprintf(p, TEXT("\\x%02x"), c);
					p += 4;
				}
			}
			*p++ = '"';
			if (n < leaf->len) {
				_wsprintf(p, TEXT("..."));
			}
			break;
		}
		path = tree_path(&rgn, leaf);
#ifdef UNICODE
		tpath = mbcs_to_utf16_r(&rgn, path);
#else
		tpath = path;
#endif
	}
	TCHAR cursor_pos_buf[17];
	_wsprintf(cursor_pos_buf, TEXT("%llx"), cursor_pos(ui));
	SendMessage(ui->status_bar, SB_SETTEXT, 0, (LPARAM) cursor_pos_buf);
	SendMessage(ui->status_bar, SB_SETTEXT, 1, (LPARAM) type_name);
	SendMessage(ui->status_bar, SB_SETTEXT, 2, (LPARAM) value_buf);
	SendMessage(ui->status_bar, SB_SETTEXT, 3, (LPARAM) tpath);
	rfree(&rgn, top);
}

// should be invoked when position in file is changed
void
update_field_info(UI *ui)
{
	if (ui->filepath) {
		Tree *tree = ui->buffer->tree;
		if (tree) {
			struct tree *leaf = tree_lookup(tree, cursor_pos(ui));
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
	} else {
		HWND statusbar = ui->status_bar;
		SendMessage(statusbar, SB_SETTEXT, 0, 0);
		SendMessage(statusbar, SB_SETTEXT, 1, 0);
		SendMessage(statusbar, SB_SETTEXT, 2, 0);
		SendMessage(statusbar, SB_SETTEXT, 3, 0);
	}
}

void
update_cursor_pos(UI *ui)
{
	int cx = 10+ui->cursor_x*3 + ui->cursor_at_low_nibble;
	SendMessage(ui->monoedit, MED_WM_SET_CURSOR_POS, cx, ui->cursor_y);
	update_field_info(ui);
}

long long
cursor_pos(UI *ui)
{
	return ((ui->current_line + ui->cursor_y) << LOG2_N_COL) + ui->cursor_x;
}

void
goto_address(UI *ui, long long addr)
{
	long long line = addr >> LOG2_N_COL;
	int col = addr & (N_COL-1);
	assert(addr >= 0 && addr < ui->buffer->file_size);
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

void
error_prompt(UI *ui, const char *errmsg)
{
	const TCHAR *terrmsg;
#ifdef UNICODE
	void *top = rgn.cur;
	terrmsg = mbcs_to_utf16_r(&rgn, errmsg);
#else
	terrmsg = errmsg;
#endif
	MessageBox(ui->hwnd, terrmsg, TEXT("Error"), MB_ICONERROR);
#ifdef UNICODE
	rfree(&rgn, top);
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
	if (cursor_pos(ui) + N_COL < ui->buffer->file_size) {
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
	long long filesize = ui->buffer->file_size;
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
	if (ui->cursor_at_low_nibble) {
		ui->cursor_at_low_nibble = 0;
	} else if (ui->cursor_x) {
		ui->cursor_x--;
		ui->cursor_at_low_nibble = 1;
	} else return;
	update_cursor_pos(ui);
}

void
move_right(UI *ui)
{
	if (!ui->cursor_at_low_nibble) {
		ui->cursor_at_low_nibble = 1;
	} else if (ui->cursor_x < (N_COL-1) && cursor_pos(ui)+1 < ui->buffer->file_size) {
		ui->cursor_x++;
		ui->cursor_at_low_nibble = 0;
	} else return;
	update_cursor_pos(ui);
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
	long long filesize = ui->buffer->file_size;
	if (filesize >= N_COL*ui->nrow && cursor_pos(ui) < filesize - N_COL*ui->nrow) {
		scroll_down_page(ui);
	}
}

void
handle_char_normal(UI *ui, int c)
{
	TCHAR *text;
	long long addr;

	switch (c) {
	case 8: // backspace
		move_backward(ui);
		break;
	case ' ':
		move_forward(ui);
		break;
	case ';':
	case ':':
		text = inputbox(ui, TEXT("Command"));
		if (text) {
			free(text);
		}
		SetFocus(ui->monoedit);
		break;
	case '/':
	case '?':
		text = inputbox(ui, TEXT("Text Search"));
		if (text) {
			free(text);
		}
		SetFocus(ui->monoedit);
		break;
	case '\\':
	case '|':
		text = inputbox(ui, TEXT("Hex Search"));
		if (text) {
			free(text);
		}
		SetFocus(ui->monoedit);
		break;
	case 'g':
		text = inputbox(ui, TEXT("Go to address"));
		if (text) {
			if (parse_addr(text, &addr)) {
				if (addr >= 0 && addr < ui->buffer->file_size) {
					goto_address(ui, addr);
				} else {
					errorbox(ui->hwnd, TEXT("Address out of range"));
				}
			} else {
				errorbox(ui->hwnd, TEXT("Syntax error"));
			}
			free(text);
		}
		SetFocus(ui->monoedit);
		break;
	case 'N':
		//execute_command(ui, cmd_findprev, 0);
		break;
	case 'b':
		move_prev_field(ui);
		break;
	case 'h':
		move_left(ui);
		break;
	case 'j':
		move_down(ui);
		break;
	case 'k':
		move_up(ui);
		break;
	case 'l':
		move_right(ui);
		break;
	case 'n':
		break;
	case 'w':
		move_next_field(ui);
		break;
	case 'R':
		ui->handle_char = handle_char_replace;
		break;
	}
}

void
handle_char_replace(UI *ui, int c)
{
	uint8_t val;
	if (c >= '0' && c <= '9') {
		val = c-'0';
	} else if (c >= 'a' && c <= 'f') {
		val = 10+(c-'a');
	} else if (c >= 'A' && c <= 'F') {
		val = 10+(c-'A');
	} else return;

	int cy = ui->cursor_y;
	int cx = ui->cursor_x;
	long long pos = cursor_pos(ui);
	uint8_t b = buf_getbyte(ui->buffer, pos);
	if (ui->cursor_at_low_nibble) {
		val |= b&0xf0;
		buf_setbyte(ui->buffer, pos, val);
		ui->med_buffer[N_COL_CHAR*cy + (11+cx*3)] = c|32;
		ui->cursor_at_low_nibble = 0;
		move_forward(ui);
	} else {
		val = val<<4 | (b&0x0f);
		buf_setbyte(ui->buffer, pos, val);
		ui->med_buffer[N_COL_CHAR*cy + (10+cx*3)] = c|32;
		ui->cursor_at_low_nibble = 1;
		SendMessage(ui->monoedit, MED_WM_SET_CURSOR_POS, 11+cx*3, cy);
		/* TODO this is inefficient */
		InvalidateRect(ui->monoedit, 0, FALSE);
	}
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
	for (int i=0; i<N_COL_CHAR*INITIAL_N_ROW; i++)
		ui->med_buffer[i] = ' ';
	SendMessage(monoedit, MED_WM_SET_CSIZE, N_COL_CHAR, INITIAL_N_ROW);
	SendMessage(monoedit, MED_WM_SET_BUFFER, 0, (LPARAM) ui->med_buffer);
	SendMessage(monoedit, WM_SETFONT, (WPARAM) ui->mono_font, 0);
	update_monoedit_buffer(ui, 0, INITIAL_N_ROW);
	/* subclass monoedit window */
	SetWindowLongPtr(monoedit, GWLP_USERDATA, (LONG_PTR) ui);
	ui->med_wndproc = (WNDPROC) SetWindowLongPtr(monoedit, GWLP_WNDPROC, (LONG_PTR) med_wndproc);
	SetFocus(monoedit);
	update_cursor_pos(ui);

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
		status_bar_height; // status bar
	AdjustWindowRect(&rect, GetWindowLongPtr(hwnd, GWL_STYLE), TRUE);
	SetWindowPos(hwnd, 0, 0, 0,
		     rect.right - rect.left,
		     rect.bottom - rect.top,
		     SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);

	update_window_title(ui);
}

void
resize_monoedit(UI *ui, int width, int height)
{
	int new_nrow = height/ui->charheight;
	if (ui->filepath && new_nrow > ui->nrow) {
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
	WORD idc;

	switch (msg) {
	case WM_NCCREATE:
		ui = ((LPCREATESTRUCT)lparam)->lpCreateParams;
		ui->hwnd = hwnd;
		SetWindowLongPtr(hwnd, 0, (LONG_PTR) ui);
		break;
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
			/* will crash if we continue */
			if (wparam == SIZE_MINIMIZED) return 0;
			int wid = LOWORD(lparam);
			// adjust status bar geometry automatically
			SendMessage(ui->status_bar, WM_SIZE, 0, 0);
			RECT status_bar_rect;
			GetWindowRect(ui->status_bar, &status_bar_rect);
			// translate top-left from screen to client
			ScreenToClient(hwnd, (LPPOINT) &status_bar_rect);
			resize_monoedit(ui, wid, status_bar_rect.top);
		}
		return 0;
	case WM_CHAR:
		ui->handle_char(ui, wparam);
		return 0;
	case WM_COMMAND:
		idc = LOWORD(wparam);
		{
			int pluginfunc = idc - IDM_PLUGIN_0;
			if (pluginfunc >= 0 && pluginfunc < ui->npluginfunc) {
				lua_State *L = ui->lua;
				getluaobj(L, "plugin");
				lua_rawgeti(L, -1, 1+pluginfunc);
				getluaobj(L, "buffer");
				if (lua_pcall(L, 1, 0, 0)) {
					luaerrorbox(hwnd, L);
				}
				lua_pop(L, 1);
				return 0;
			}
		}
		switch (idc) {
			TCHAR path[BUFSIZE];
		case IDM_FILE_OPEN:
			if (file_chooser_dialog(hwnd, path, BUFSIZE) == 0) {
				if (ui->filepath) close_file(ui);
				open_file(ui, path);
				update_ui(ui);
			}
			break;
		case IDM_FILE_SAVE:
			if (save_file(ui)) {
				errorbox(hwnd, TEXT("Could not save file"));
			}
			break;
		case IDM_FILE_CLOSE:
			close_file(ui);
			update_ui(ui);
			break;
		case IDM_TOOLS_LOAD_PLUGIN:
			if (file_chooser_dialog(hwnd, path, BUFSIZE) == 0) {
				char *luafilepath;
#ifdef UNICODE
				void *top = rgn.cur;
				luafilepath = utf16_to_mbcs(&rgn, path);
#else
				luafilepath = path;
#endif
				load_plugin(ui, luafilepath);
#ifdef UNICODE
				rfree(&rgn, top);
#endif
			}
			break;
		}
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void
update_window_title(UI *ui)
{
	if (ui->filepath) {
		int pathlen = lstrlen(ui->filepath);
		void *top = rgn.cur;
		TCHAR *title = ralloc(&rgn, (pathlen+13) * sizeof *title);
		_wsprintf(title, TEXT("%s%s - WHEX"),
			  ui->filepath,
			  ui->readonly ? TEXT(" [RO]") : TEXT(""));
		SetWindowText(ui->hwnd, title);
		rfree(&rgn, top);
	} else {
		SetWindowText(ui->hwnd, TEXT("WHEX"));
	}
}

/* does not update UI */
int
open_file(UI *ui, TCHAR *path)
{
	TCHAR errtext[BUFSIZE];
	Buffer *b = ui->buffer;
	HANDLE file;
	bool readonly = 0;

	file = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
			  0, // lpSecurityAttributes
			  OPEN_EXISTING, 0, 0);
	if (file == INVALID_HANDLE_VALUE &&
	    GetLastError() == ERROR_SHARING_VIOLATION) {
		readonly = 1;
		file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ,
				  0, // lpSecurityAttributes
				  OPEN_EXISTING, 0, 0);
	}
	if (file == INVALID_HANDLE_VALUE) {
		format_error_code(errtext, BUFSIZE, GetLastError());
		errorbox(ui->hwnd, errtext);
		return -1;
	}
	if (buf_init(ui->buffer, file) < 0) {
		CloseHandle(file);
		return -1;
	}
	ui->filepath = lstrdup(path);
	ui->total_lines = b->file_size >> LOG2_N_COL;
	if (b->file_size&(N_COL-1)) {
		ui->total_lines += 1;
	}
	ui->med_buffer = malloc(N_COL_CHAR*ui->nrow*sizeof(TCHAR));
	ui->med_buffer_nrow = ui->nrow;
	SendMessage(ui->monoedit, MED_WM_SET_BUFFER, 0,
		    (LPARAM) ui->med_buffer);
	ui->readonly = readonly;

	load_filetype_plugin(ui, path);

	return 0;
}

void
close_file(UI *ui)
{
	buf_finalize(ui->buffer);
	free(ui->med_buffer);
	ui->med_buffer = 0;
	ui->med_buffer_nrow = 0;
	free(ui->filepath);
	ui->filepath = 0;
	ui->total_lines = 0;
	ui->current_line = 0;
	ui->cursor_y = 0;
	ui->cursor_x = 0;
	ui->hl_start = 0;
	ui->hl_len = 0;
	ui->npluginfunc = 0;
	if (ui->plugin_name) {
		free(ui->plugin_name);
		ui->plugin_name = 0;
		ui->npluginfunc = 0;
		for (int i=0; i<ui->npluginfunc; i++) {
			free(ui->plugin_funcname[i]);
		}
		free(ui->plugin_funcname);
		ui->plugin_funcname = 0;
	}

	SendMessage(ui->monoedit, MED_WM_SET_BUFFER, 0, 0);
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

/* keeps everything in sync */
void
update_ui(UI *ui)
{
	static const int toggle_menus[] = {
		IDM_FILE_SAVEAS,
		IDM_FILE_CLOSE,
		IDM_TOOLS_LOAD_PLUGIN,
	};

	HMENU menu = GetMenu(ui->hwnd);
	MENUITEMINFO mii = { sizeof mii };
	mii.fMask = MIIM_STATE;

	update_window_title(ui);
	if (ui->filepath) {
		ShowWindow(ui->monoedit, SW_SHOW);
		update_monoedit_buffer(ui, 0, ui->nrow);
		update_monoedit_tags(ui);
		update_cursor_pos(ui);
		mii.fState = ui->readonly ? MFS_GRAYED : MFS_ENABLED;
		SetMenuItemInfo(menu, IDM_FILE_SAVE, FALSE, &mii);
		mii.fState = MFS_ENABLED;
	} else {
		ShowWindow(ui->monoedit, SW_HIDE);
		mii.fState = MFS_GRAYED;
		SetMenuItemInfo(menu, IDM_FILE_SAVE, FALSE, &mii);
	}
	for (int i=0; i<NELEM(toggle_menus); i++) {
		SetMenuItemInfo(menu, toggle_menus[i], FALSE, &mii);
	}
	InvalidateRect(ui->monoedit, 0, FALSE);
	update_field_info(ui);
	update_plugin_menu(ui);
}

void
move_forward(UI *ui)
{
	Buffer *b = ui->buffer;
	if (cursor_pos(ui) < b->file_size) {
		if (ui->cursor_x < N_COL-1) {
			ui->cursor_at_low_nibble = 1;
			move_right(ui);
		} else {
			ui->cursor_x = 0;
			ui->cursor_at_low_nibble = 0;
			move_down(ui);
		}
	}
}

void
move_backward(UI *ui)
{
	if (cursor_pos(ui) > 0) {
		ui->cursor_at_low_nibble = 0;
		if (ui->cursor_x > 0) {
			move_left(ui);
			ui->cursor_at_low_nibble = 0;
		} else {
			ui->cursor_x = N_COL-1;
			move_up(ui);
		}
	}
}

void
move_next_field(UI *ui)
{
	Buffer *b = ui->buffer;
	if (b->tree) {
		long long cur = cursor_pos(ui);
		struct tree *leaf = tree_lookup(b->tree, cur);
		if (leaf) {
			goto_address(ui, leaf->start + leaf->len);
		}
	}
}

void
move_prev_field(UI *ui)
{
	Buffer *b = ui->buffer;
	if (b->tree) {
		long long cur = cursor_pos(ui);
		struct tree *leaf = tree_lookup(b->tree, cur);
		if (leaf) {
			struct tree *prev_leaf = tree_lookup(b->tree, cur-1);
			if (prev_leaf) {
				goto_address(ui, prev_leaf->start);
			}
		}
	}
}

void
goto_bol(UI *ui)
{
	if (ui->buffer->file_size > 0) {
		goto_address(ui, (ui->current_line + ui->cursor_y) * N_COL);
	}
}

void
goto_eol(UI *ui)
{
	long long filesize = ui->buffer->file_size;
	if (filesize > 0) {
		long long addr = (ui->current_line + ui->cursor_y + 1) * N_COL - 1;
		if (addr >= filesize) {
			addr = filesize-1;
		}
		goto_address(ui, addr);
	}
}

BOOL CALLBACK
inputbox_dlgproc(HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam)
{
	InputBoxConfig *conf;
	TCHAR *text;
	HWND edit;
	int len;

	switch (msg) {
	case WM_INITDIALOG:
		SetWindowLongPtr(dlg, GWLP_USERDATA, lparam);
		conf = (InputBoxConfig *) lparam;
		SetWindowText(dlg, conf->title);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wparam)) {
		case IDOK:
			edit = GetDlgItem(dlg, IDC_INPUTBOX_EDIT);
			len = GetWindowTextLength(edit)+1;
			text = malloc(len * sizeof *text);
			GetWindowText(edit, text, len);
			conf = (InputBoxConfig *)
				GetWindowLongPtr(dlg, GWLP_USERDATA);
			conf->text = text;
			EndDialog(dlg, 1);
			return TRUE;
		case IDCANCEL:
			EndDialog(dlg, 0);
			return TRUE;
		}
	}
	return FALSE;
}

TCHAR *
inputbox(UI *ui, TCHAR *title)
{
	InputBoxConfig conf;
	conf.text = 0;
	conf.title = title;
	if (DialogBoxParam(ui->instance, TEXT("INPUTBOX"), ui->hwnd,
			   inputbox_dlgproc, (LPARAM) &conf) > 0)
		return conf.text;
	return 0;
}

bool
parse_addr(TCHAR *s, long long *addr)
{
	long long n = 0;
	if (!*s) return 0;
	do {
		TCHAR c = *s++;
		int d;
		if (c >= '0' && c <= '9') {
			d = c-'0';
		} else if (c >= 'a' && c <= 'f') {
			d = 10+(c-'a');
		} else if (c >= 'A' && c <= 'F') {
			d = 10+(c-'A');
		} else {
			return false;
		}
		n = (n<<4)|d;
	} while (*s);
	*addr = n;
	return true;
}

void
errorbox(HWND hwnd, TCHAR *msg)
{
	MessageBox(hwnd, msg, TEXT("Error"), MB_OK | MB_ICONERROR);
}

void
luaerrorbox(HWND hwnd, lua_State *L)
{
	MessageBoxA(hwnd, lua_tostring(L, -1), "Error", MB_OK | MB_ICONERROR);
}

void
msgbox(HWND hwnd, const char *fmt, ...)
{
	char msg[BUFSIZE];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(msg, fmt, ap);
	va_end(ap);
	MessageBoxA(hwnd, msg, "WHEX", MB_OK);
}

void
getluaobj(lua_State *L, const char *name)
{
	lua_getglobal(L, "whex");
	lua_pushstring(L, name);
	lua_rawget(L, -2);
	lua_remove(L, -2);
}

int
load_plugin(UI *ui, const char *path)
{
	lua_State *L = ui->lua;

	if (luaL_dofile(L, path)) {
		luaerrorbox(ui->hwnd, L);
		return -1;
	}

	/* plugin information is stored as lua table at top of stack */
	luaL_checktype(L, -1, LUA_TTABLE);

	//getluaobj(L, "buffer");
	lua_pushstring(L, "parser");
	lua_rawget(L, -2);
	luaL_checktype(L, -1, LUA_TFUNCTION);
	getluaobj(L, "buffer");
	if (lua_pcall(L, 1, 1, 0)) {
		luaerrorbox(ui->hwnd, L);
		return -1;
	}

	/* Get the internal node */
	if (lua_pcall(L, 0, 1, 0)) {
		luaerrorbox(ui->hwnd, L);
		return -1;
	}

	Buffer *b = ui->buffer;
	Tree *tree = convert_tree(&b->tree_rgn, L);
	getluaobj(L, "buffer");
	lua_insert(L, -2);
	lua_setuservalue(L, -2);
	lua_pop(L, 1);
	if (b->tree) {
		rfreeall(&b->tree_rgn);
	}
	b->tree = tree;

	lua_pushstring(L, "name");
	lua_rawget(L, -2); /* plugin.name */
	const char *plugin_name = luaL_checkstring(L, -1);
	TCHAR *tplugin_name;
#ifdef UNICODE
	tplugin_name = mbcs_to_utf16(plugin_name);
#else
	tplugin_name = strdup(plugin_name);
#endif
	lua_pop(L, 1);
	if (ui->plugin_name) {
		free(ui->plugin_name);
	}
	ui->plugin_name = tplugin_name;

	getluaobj(L, "plugin");
	lua_pushstring(L, "functions");
	lua_gettable(L, -3);
	luaL_checktype(L, -1, LUA_TTABLE);
	int n = luaL_len(L, -1);
	ui->npluginfunc = n;
	ui->plugin_funcname = malloc(n * sizeof *ui->plugin_funcname);
	for (int i=0; i<n; i++) {
		lua_geti(L, -1, 1+i); /* push {func, funcname} */
		lua_geti(L, -1, 1); /* push func */
		lua_seti(L, -4, 1+i);
		lua_geti(L, -1, 2); /* push funcname */
		const char *funcname = luaL_checkstring(L, -1);
		TCHAR *tfuncname;
#ifdef UNICODE
		tfuncname = mbcs_to_utf16(funcname);
#else
		tfuncname = strdup(funcname);
#endif
		lua_pop(L, 2);
		ui->plugin_funcname[i] = tfuncname;
	}
	lua_pop(L, 3);

	update_plugin_menu(ui);
	return 0;
}

void
update_plugin_menu(UI *ui)
{
	HMENU mainmenu = GetMenu(ui->hwnd);

	if (ui->plugin_name) {
		HMENU plugin_menu = CreateMenu();
		for (int i=0; i<ui->npluginfunc; i++) {
			AppendMenu(plugin_menu, MF_STRING, IDM_PLUGIN_0+i,
				   ui->plugin_funcname[i]);
		}
		ModifyMenu(mainmenu, 2, MF_BYPOSITION | MF_POPUP,
			   (UINT_PTR) plugin_menu, ui->plugin_name);
	} else {
		ModifyMenu(mainmenu, 2, MF_BYPOSITION | MF_STRING | MF_GRAYED,
			   0, TEXT("Plugin"));
	}

	DrawMenuBar(ui->hwnd);
}

int
load_filetype_plugin(UI *ui, const TCHAR *path)
{
	int i = lstrlen(path);
	while (i >= 1) {
		switch (path[i-1]) {
		case '/':
		case '\\':
			return 0;
		case '.':
			goto det;
		}
		i--;
	}
	return 0;
	lua_State *L;
det:
	L = ui->lua;
	lua_pushstring(L, "ftdet");
	lua_rawget(L, LUA_REGISTRYINDEX);
	lua_pushstring(L, path+i);
	if (lua_pcall(L, 1, 1, 0)) {
		// error
		return -1;
	}
	const char *ft = 0;
	if (lua_isstring(L, -1)) {
		ft = luaL_checkstring(L, -1);
	}
	int ret;
	if (ft) {
		char *buf = malloc(mydir_len+8+strlen(ft)+4+1);
		_wsprintf(buf, TEXT("%splugin_%s.lua"), mydir, ft);
		ret = load_plugin(ui, buf);
		free(buf);
	} else {
		ret = -1;
	}
	lua_pop(L, 1);
	return ret;
}

int
save_file(UI *ui)
{
	return buf_save(ui->buffer);
}
