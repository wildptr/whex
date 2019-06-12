#define _CRT_SECURE_NO_WARNINGS
#include "u.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <windows.h>
#include <commctrl.h>
#if _MSC_VER <= 1200
#include "vc6compat.h"
#undef putc
#endif

#include "buffer.h"
#include "tree.h"
#include "unicode.h"
#include "resource.h"
#include "monoedit.h"
#include "winutil.h"

#define INITIAL_N_ROW 32
#define LOG2_N_COL 4
#define N_COL (1<<LOG2_N_COL)
#define N_COL_CHAR (16+4*N_COL)

#define BUFSIZE 512

enum {
	ID_MONOEDIT = 0x100,
	ID_STATUS_BAR,
	ID_FILE_OPEN,
	ID_FILE_SAVE,
	ID_FILE_SAVEAS,
	ID_FILE_CLOSE,
	ID_FILE_EXIT,
	ID_TOOLS_LOAD_PLUGIN,
	ID_TOOLS_RUN_LUA_SCRIPT,
	ID_PLUGIN_0,
};

enum {
	MODE_NORMAL,
	MODE_REPLACE,
};

enum {
	POS_BEFORE,
	POS_HINIB,
	POS_LONIB,
	POS_ASCII,
	POS_GAP
};

typedef struct {
	TCHAR *text;
	TCHAR *title;
} InputBoxConfig;

typedef struct {
	Buffer *buffer;
	HWND hwnd;
	HWND monoedit;
	TCHAR *filepath; // property
	WNDPROC med_wndproc;
	int cursor_y; // property cursor_pos
	int cursor_x; // property cursor_pos
	int charwidth;
	int charheight;
	HFONT mono_font;
	lua_State *lua;
	uint64 hl_start;
	uint64 hl_len;
	HWND status_bar;
	HINSTANCE instance;
	int npluginfunc;
	char *plugin_name; // property
	char **plugin_funcname;
	uchar mode;
	uchar cursor_fine_pos; // property cursor_pos
	uchar readonly;
	HWND treeview;
	uint64 replace_start;
	uchar *replace_buf;
	uint replace_buf_cap;
	uint replace_buf_len;
	Tree *tree; // property
	Region tree_rgn;
	HBRUSH bgbrush;
	uchar filepath_changed;
	uchar tree_changed;
	uchar plugin_name_changed;
	uchar cursor_pos_changed;
	uchar buffer_changed;
	char *last_pat;
	int last_pat_len;
} UI;

Region rgn;
/* GetOpenFileName() changes directory, so remember working directory when
   program starts */
char *program_dir;
int program_dir_len;

LRESULT CALLBACK med_wndproc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
static ATOM register_wndclass(void);
static int start_gui(int, UI *, TCHAR *);
void set_current_line(UI *, uint64);
static void update_window_title(UI *ui);
void update_status_text(UI *, Tree *);
void update_field_info(UI *);
void update_logical_cursor_pos(UI *);
void update_physical_cursor_pos(UI *);
uint64 cursor_pos(UI *);
uint64 current_line(UI *);
void goto_address(UI *, uint64);
void error_prompt(UI *, const TCHAR *);
void move_up(UI *);
void move_down(UI *);
void move_left(UI *);
void move_right(UI *);
void goto_bol(UI *);
void goto_eol(UI *);
void handle_char_normal(UI *, TCHAR);
void handle_char_replace(UI *, TCHAR);
void init_font(UI *);
void handle_wm_create(UI *, LPCREATESTRUCT);
int open_file(UI *, TCHAR *);
void close_file(UI *);
void update_ui(UI *);
void update_overlay(UI *);
void move_forward(UI *);
void move_backward(UI *);
void move_next_field(UI *);
void move_prev_field(UI *);
/* allocates result on heap */
TCHAR *inputbox(UI *, TCHAR *title);
int parse_addr(TCHAR *, uint64 *);
void errorbox(HWND, TCHAR *);
Tree *convert_tree(Region *, lua_State *);
int load_plugin(UI *, const char *);
int api_buffer_peek(lua_State *L);
int api_buffer_peeku16(lua_State *L);
int api_buffer_peeku32(lua_State *L);
int api_buffer_peeku64(lua_State *L);
int api_buffer_read(lua_State *L);
int api_buffer_tree(lua_State *L);
int api_buffer_size(lua_State *L);
int api_buffer_replace(lua_State *L);
int api_buffer_insert(lua_State *L);
void getluaobj(lua_State *L, const char *name);
void luaerrorbox(HWND hwnd, lua_State *L);
void update_plugin_menu(UI *ui);
int load_filetype_plugin(UI *, const TCHAR *);
int save_file_as(UI *, const TCHAR *);
void populate_treeview(UI *);
void format_leaf_value(UI *ui, Tree *t, char **ptypename, char **pvaluerepr);
int get_nrow(UI *ui);
void exit_replace_mode(UI *);
uchar cursor_in_gap(UI *);
int col_to_cx(UI *, int);
void ui_set_tree(UI *, Tree *);
void ui_set_filepath(UI *, TCHAR *);
void ui_set_plugin_name(UI *, char *);
void ui_set_cursor_pos(UI *, int, int, uchar);
char get_display_char(uchar b);

LRESULT CALLBACK
med_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	UI *ui = (UI *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

	switch (msg) {
	case WM_KEYDOWN:
		switch (ui->mode) {
		case MODE_NORMAL:
			switch (wparam) {
			case VK_HOME:
				if (GetKeyState(VK_CONTROL) < 0) {
					goto_address(ui, 0);
				} else {
					goto_bol(ui);
				}
				return 0;
			case VK_END:
				if (GetKeyState(VK_CONTROL) < 0) {
					goto_address(ui, buf_size(ui->buffer));
				} else {
					goto_eol(ui);
				}
				return 0;
			}
			break; /* to CallWindowProc */
		case MODE_REPLACE:
			switch (wparam) {
			case VK_ESCAPE:
				exit_replace_mode(ui);
				break;
			}
			return 0;
		default:
			assert(0);
		}
		break;
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

static HMENU
create_menu(void)
{
	HMENU mainmenu, m;

	mainmenu = CreateMenu();

	m = CreateMenu();
	AppendMenu(m, MF_STRING, ID_FILE_OPEN, TEXT("Open..."));
	AppendMenu(m, MF_STRING, ID_FILE_SAVE, TEXT("Save"));
	AppendMenu(m, MF_STRING, ID_FILE_SAVEAS, TEXT("Save As..."));
	AppendMenu(m, MF_STRING, ID_FILE_CLOSE, TEXT("Close"));
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	AppendMenu(m, MF_STRING, ID_FILE_EXIT, TEXT("Exit"));
	AppendMenu(mainmenu, MF_POPUP, (UINT_PTR) m, TEXT("File"));

	m = CreateMenu();
	AppendMenu(m, MF_STRING, ID_TOOLS_LOAD_PLUGIN, TEXT("Load Plugin..."));
	AppendMenu(m, MF_STRING, ID_TOOLS_RUN_LUA_SCRIPT,
		   TEXT("Run Lua Script..."));
	AppendMenu(mainmenu, MF_POPUP, (UINT_PTR) m, TEXT("Tools"));

	/* appearance will be overridden by update_ui() */
	AppendMenu(mainmenu, MF_SEPARATOR, 0, 0);

	return mainmenu;
}

/* filepath is owned */
static int
start_gui(int show, UI *ui, TCHAR *filepath)
{
	HMENU menu;
	HWND hwnd;
	MSG msg;

	InitCommonControls();
	if (!med_register_class()) return 1;
	if (!register_wndclass()) return 1;
	if (filepath) {
		int status = open_file(ui, filepath);
		free(filepath);
		if (status) return 0;
	} else {
		// ui_set_filepath(ui, 0);
		/* this won't work because (ui->filepath == 0) at the moment,
		   so nothing will happen */
		ui->filepath_changed = 1;
		/* the program will behave as if no file is open, saving,
		   editing etc. will be disabled */
	}
	init_font(ui);
	menu = create_menu();
	hwnd = CreateWindow
		(TEXT("WHEX"), 0 /* will be set later */, WS_OVERLAPPEDWINDOW,
		 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		 0, menu, ui->instance, ui);
	ShowWindow(hwnd, show);
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		if (!PeekMessage(&msg, 0, 0, 0, 0)) update_ui(ui);
	}
	return msg.wParam;
}

typedef struct tstring_list {
	TCHAR *hd;
	struct tstring_list *tl;
} TStringList;

/* TODO: handle backslash */
static TCHAR **
cmdline_to_argv(Region *r, TCHAR *cmdline, int *argc)
{
	int narg = 0;
	int i;
	TStringList *arglist, **plast = &arglist;
	TCHAR *p = cmdline;
	TCHAR *q;
	TCHAR *arg;
	TCHAR **argv;
	TStringList *node;

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
		NEWARRAY(arg, n+1, r);
		memcpy(arg, p, n * sizeof *arg);
		arg[n] = 0;
		node = alloca(sizeof *node);
		node->hd = arg;
		node->tl = 0;
		*plast = node;
		plast = &node->tl;
		p = q;
		while (isspace(*p)) p++;
	} while (*p);
	i = 0;
	NEWARRAY(argv, narg+1, r);
	for (node = arglist; node; node = node->tl)
		argv[i++] = node->hd;
	argv[narg] = 0;
	*argc = narg;
	return argv;
}

TCHAR *
lstrdup(const TCHAR *s)
{
	int nb = (lstrlen(s)+1) * sizeof(TCHAR);
	TCHAR *ret = xmalloc(nb);
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
	void *top;
	int i;
	lua_State *L;
	Buffer *b;
	UI *ui;

	program_dir = xmalloc(BUFSIZE);
	program_dir_len = GetCurrentDirectoryA(BUFSIZE, program_dir);
	if (program_dir_len > BUFSIZE) {
		free(program_dir);
		program_dir = xmalloc(program_dir_len);
		program_dir_len = GetCurrentDirectoryA(BUFSIZE, program_dir);
	}
	if (!program_dir_len) return 1;

	rinit(&rgn);
	top = rgn.cur;
	argv = cmdline_to_argv(&rgn, GetCommandLine(), &argc);

	i = 1;
	while (i < argc) {
		TCHAR *arg = argv[i];
		if (arg[0] == '-') {
			switch (arg[1]) {
			case 'd':
				AllocConsole();
				freopen("CON", "w", stdout);
				break;
			}
			i++;
		} else break;
	}
	argc -= i;
	argv += i;

	/* parse command line arguments */
	if (argc == 0) {
		if (open_file_chooser_dialog(0, openfilename, BUFSIZE)) {
			filepath = 0;
		} else {
			filepath = openfilename;
		}
	} else if (argc == 1) {
		filepath = argv[0];
	} else {
		return 1;
	}

	if (filepath) filepath = lstrdup(filepath);
	rfree(&rgn, top);

	L = luaL_newstate();
	luaL_openlibs(L);

	/* set Lua search path */
	lua_getglobal(L, "package");
	lua_pushfstring(L, "%s/?.lua", program_dir);
	lua_setfield(L, -2, "path");
	lua_pop(L, 1); /* global 'package' */

	luaL_newmetatable(L, "buffer");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, api_buffer_peek);
	lua_setfield(L, -2, "peek");
	lua_pushcfunction(L, api_buffer_peeku16);
	lua_setfield(L, -2, "peeku16");
	lua_pushcfunction(L, api_buffer_peeku32);
	lua_setfield(L, -2, "peeku32");
	lua_pushcfunction(L, api_buffer_peeku64);
	lua_setfield(L, -2, "peeku64");
	lua_pushcfunction(L, api_buffer_read);
	lua_setfield(L, -2, "read");
	lua_pushcfunction(L, api_buffer_tree);
	lua_setfield(L, -2, "tree");
	lua_pushcfunction(L, api_buffer_size);
	lua_setfield(L, -2, "size");
	lua_pushcfunction(L, api_buffer_replace);
	lua_setfield(L, -2, "replace");
	lua_pushcfunction(L, api_buffer_insert);
	lua_setfield(L, -2, "insert");
	lua_pop(L, 1); /* 'buffer' */

	lua_newtable(L); /* global 'whex' */
	b = lua_newuserdata(L, sizeof_Buffer);
	memset(b, 0, sizeof_Buffer);
	luaL_setmetatable(L, "buffer");
	lua_setfield(L, -2, "buffer");
	lua_newtable(L);
	lua_setfield(L, -2, "plugin");
	lua_newtable(L);
	lua_setfield(L, -2, "customtype");

	{
		char *ftdet_path = asprintf("%s/ftdet.lua", program_dir);
		if (luaL_dofile(L, ftdet_path)) {
			luaerrorbox(0, L);
			lua_pop(L, 1);
		} else {
			lua_setfield(L, -2, "ftdet");
		}
		free(ftdet_path);
	}

	lua_setglobal(L, "whex");

	NEW(ui, &rgn);
	memset(ui, 0, sizeof *ui);
	ui->lua = L;
	ui->buffer = b;
	ui->buffer_changed = 1;
	ui->instance = instance;
	ui->replace_buf_cap = 16;
	ui->replace_buf = xmalloc(ui->replace_buf_cap);

	return start_gui(show, ui, filepath);
}

void
set_current_line(UI *ui, uint64 line)
{
	med_set_current_line(ui->monoedit, line);
	update_logical_cursor_pos(ui);
}

void
update_status_text(UI *ui, Tree *leaf)
{
	HWND sb = ui->status_bar;
	if (cursor_in_gap(ui)) {
		SendMessage(sb, SB_SETTEXT, 0, 0);
	} else {
		uint64 pos = cursor_pos(ui);
		TCHAR buf[17];
		T(_sprintf)(buf, TEXT("%llx"), pos);
		SendMessage(sb, SB_SETTEXT, 0, (LPARAM) buf);
	}
	if (leaf) {
		char *typename;
		char *valuerepr;
		void *top = rgn.cur;
		char *path;
		format_leaf_value(ui, leaf, &typename, &valuerepr);
		path = tree_path(&rgn, leaf);
		SendMessage(sb, SB_SETTEXTA, 1, (LPARAM) typename);
		SendMessage(sb, SB_SETTEXTA, 2, (LPARAM) valuerepr);
		SendMessage(sb, SB_SETTEXTA, 3, (LPARAM) path);
		rfree(&rgn, top);
	} else {
		SendMessage(sb, SB_SETTEXT, 1, 0);
		SendMessage(sb, SB_SETTEXT, 2, 0);
		SendMessage(sb, SB_SETTEXT, 3, 0);
	}
}

/* should be invoked when position in file is changed */
void
update_field_info(UI *ui)
{
	if (ui->filepath) {
		Tree *tree = ui->tree;
		Tree *leaf = 0;
		med_clear_overlay(ui->monoedit);
		if (tree) {
			if (!cursor_in_gap(ui)) {
				leaf = tree_lookup(tree, cursor_pos(ui));
			}
			if (leaf) {
				ui->hl_start = leaf->start;
				ui->hl_len = leaf->len;
			} else {
				ui->hl_start = 0;
				ui->hl_len = 0;
			}
		}
		update_overlay(ui);
		med_update_backbuffer(ui->monoedit);
		InvalidateRect(ui->monoedit, 0, FALSE);
		update_status_text(ui, leaf);
	} else {
		HWND statusbar = ui->status_bar;
		SendMessage(statusbar, SB_SETTEXT, 0, 0);
		SendMessage(statusbar, SB_SETTEXT, 1, 0);
		SendMessage(statusbar, SB_SETTEXT, 2, 0);
		SendMessage(statusbar, SB_SETTEXT, 3, 0);
	}
}

void
update_logical_cursor_pos(UI *ui)
{
	int pos[2];
	int cy, cx;
	int cursor_y, cursor_x;
	uchar cursor_fine_pos;
	med_get_cursor_pos(ui->monoedit, pos);
	cy = pos[0];
	cx = pos[1];
	cursor_y = cy;
	if (cx < 9) {
		cursor_x = ui->cursor_x;
		cursor_fine_pos = POS_GAP;
	} else if (cx < 57) {
		cursor_x = (cx-9)/3;
		cursor_fine_pos = (cx-9)%3;
	} else if (cx < 59) {
		cursor_x = ui->cursor_x;
		cursor_fine_pos = POS_GAP;
	} else if (cx < 75) {
		cursor_x = cx-59;
		cursor_fine_pos = POS_ASCII;
	} else {
		cursor_x = ui->cursor_x;
		cursor_fine_pos = POS_GAP;
	}
	ui_set_cursor_pos(ui, cursor_y, cursor_x, cursor_fine_pos);
}

void
update_physical_cursor_pos(UI *ui)
{
	int cx;
	cx = col_to_cx(ui, ui->cursor_x);
	med_set_cursor_pos(ui->monoedit, ui->cursor_y, cx);
	ui->cursor_pos_changed = 1;
}

uchar
cursor_in_gap(UI *ui)
{
	return ui->cursor_fine_pos == POS_GAP;
}

uint64
cursor_pos(UI *ui)
{
	int pos[2];
	med_get_cursor_pos(ui->monoedit, pos);
	return ((current_line(ui) + pos[0]) << LOG2_N_COL) + ui->cursor_x;
}

uint64
current_line(UI *ui)
{
	return med_get_current_line(ui->monoedit);
}

int
col_to_cx(UI *ui, int col)
{
	switch (ui->cursor_fine_pos) {
	case POS_ASCII:
		return 59+col;
	case POS_GAP:
		assert(0);
		/* fallthrough */
	default:
		return 9+col*3+ui->cursor_fine_pos;
	}
}

void
goto_address(UI *ui, uint64 addr)
{
	uint64 line = addr >> LOG2_N_COL;
	int col = (int) addr & (N_COL-1);
	int cy, cx;
	uint64 curline;
	int nrow;

	assert(addr >= 0 && addr <= buf_size(ui->buffer));
	if (ui->cursor_fine_pos == POS_GAP) {
		ui->cursor_fine_pos = POS_HINIB;
	}
	cx = col_to_cx(ui, col);
	curline = current_line(ui);
	nrow = get_nrow(ui);
	if (line >= curline && line < curline + nrow) {
		cy = (int)(line - curline);
	} else {
		uint64 dstline;
		if (line >= nrow >> 1) {
			dstline = line - (nrow >> 1);
		} else {
			dstline = 0;
		}
		cy = (int)(line - dstline);
		set_current_line(ui, dstline);
	}
	ui_set_cursor_pos(ui, cy, col, ui->cursor_fine_pos);
	med_set_cursor_pos(ui->monoedit, cy, cx);
}

void
error_prompt(UI *ui, const TCHAR *errmsg)
{
	MessageBox(ui->hwnd, errmsg, TEXT("Error"), MB_ICONERROR);
}

void
move_up(UI *ui)
{
	med_move_up(ui->monoedit);
	update_logical_cursor_pos(ui);
}

void
move_down(UI *ui)
{
	med_move_down(ui->monoedit);
	update_logical_cursor_pos(ui);
}

void
move_left(UI *ui)
{
	med_move_left(ui->monoedit);
	update_logical_cursor_pos(ui);
}

void
move_right(UI *ui)
{
	med_move_right(ui->monoedit);
	update_logical_cursor_pos(ui);
}

int
read_nibble(TCHAR c)
{
	if (c >= '0' && c <= '9') return c-'0';
	c |= 32;
	if (c >= 'a' && c <= 'f') return c-'a'+10;
	return -1;
}

int
parse_hex_byte(TCHAR *s)
{
	int hi = read_nibble(*s);
	int lo;
	if (hi<0) return -1;
	lo = read_nibble(s[1]);
	if (lo<0) return -1;
	return hi<<4|lo;
}

char *
parse_hex_string(TCHAR *s, int *len)
{
	int n = lstrlen(s);
	char *pat = xmalloc(n);
	int p = 0;
	int b;
	for (;;) {
		while (*s == ' ') s++;
		if (!*s) {
			*len = p;
			return pat;
		}
		b = parse_hex_byte(s);
		if (b < 0) {
			free(pat);
			return 0;
		}
		s += 2;
		pat[p++] = b;
	}
}

/* pat is malloc'd; if pattern not found, shows message box */
void
search(UI *ui, char *pat, int patlen, int offset)
{
	if (patlen > 0) {
		uint64 start = cursor_pos(ui) + offset;
		uint64 bufsize, pos;
		int ret;
		if (start > (bufsize = buf_size(ui->buffer))) {
			start = bufsize;
		}
		ret = buf_kmp_search(ui->buffer, pat, patlen, start, &pos);
		if (pat != ui->last_pat) {
			free(ui->last_pat);
			ui->last_pat = pat;
			ui->last_pat_len = patlen;
		}
		if (ret == 0) {
			goto_address(ui, pos);
		} else {
			msgboxf(ui->hwnd, TEXT("Pattern not found"));
		}
	}
}

void
handle_char_normal(UI *ui, TCHAR c)
{
	TCHAR *text;
	uint64 addr;

	switch (c) {
	case 8: /* backspace */
		move_backward(ui);
		break;
	case ' ':
		move_forward(ui);
		break;
#if 0
	case ';':
	case ':':
		text = inputbox(ui, TEXT("Command"));
		if (text) {
			free(text);
		}
		SetFocus(ui->monoedit);
		break;
#endif
	case '/':
		text = inputbox(ui, TEXT("Text Search"));
		if (text) {
			char *pat;
			int patlen;
#ifdef UNICODE
			pat = utf16_to_mbcs(text);
			free(text);
#else
			pat = text;
#endif
			patlen = strlen(pat);
			search(ui, pat, patlen, 0);
		}
		SetFocus(ui->monoedit);
		break;
	case '\\':
		text = inputbox(ui, TEXT("Hex Search"));
		if (text) {
			int patlen;
			char *pat = parse_hex_string(text, &patlen);
			free(text);
			if (pat) {
				search(ui, pat, patlen, 0);
			} else {
				errorbox(ui->hwnd, TEXT("Syntax error"));
			}
		}
		SetFocus(ui->monoedit);
		break;
	case 'g':
		text = inputbox(ui, TEXT("Go to address"));
		if (text) {
			if (parse_addr(text, &addr)) {
				errorbox(ui->hwnd, TEXT("Syntax error"));
			} else {
				if (addr >= 0 && addr <= buf_size(ui->buffer)) {
					goto_address(ui, addr);
				} else {
					errorbox(ui->hwnd,
						 TEXT("Address out of range"));
				}
			}
			free(text);
		}
		SetFocus(ui->monoedit);
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
		search(ui, ui->last_pat, ui->last_pat_len, 1);
		break;
	case 'w':
		move_next_field(ui);
		break;
	case 'R':
		if (!cursor_in_gap(ui)) {
			ui->mode = MODE_REPLACE;
			ui->replace_start = cursor_pos(ui);
		} else {
			MessageBeep(-1);
		}
		break;
	case 'i':
		if (!cursor_in_gap(ui)) {
			/* TODO: reduce code duplication */
			TCHAR *text;
			if (ui->cursor_fine_pos == POS_ASCII) {
				text = inputbox(ui, TEXT("Insert Text"));
				if (text) {
					char *data;
					int datalen;
#ifdef UNICODE
					data = utf16_to_mbcs(text);
					free(text);
#else
					data = text;
#endif
					datalen = strlen(data);
					buf_insert(ui->buffer,
						   cursor_pos(ui),
						   data, datalen);
					ui->buffer_changed = 1;
				}
			} else {
				text = inputbox(ui, TEXT("Insert Hex"));
				if (text) {
					int datalen;
					char *data = parse_hex_string
						(text, &datalen);
					free(text);
					if (data) {
						buf_insert(ui->buffer,
							   cursor_pos(ui),
							   data, datalen);
						ui->buffer_changed = 1;
					} else {
						errorbox(ui->hwnd,
							 TEXT("Syntax error"));
					}
				}
			}
		} else {
			MessageBeep(-1);
		}
		break;
	}
}

static uchar
hexchar(uchar val)
{
	return val < 10 ? '0'+val : 'a'+(val-10);
}

void
handle_char_replace(UI *ui, TCHAR c)
{
	uchar val, b;
	int cy, cx;
	uint64 pos;
	uint64 bufsize;
	int med_cx;
	HWND med;
	uchar hinib, lonib;

	switch (ui->cursor_fine_pos) {
	case POS_LONIB:
	case POS_HINIB:
		if (c >= '0' && c <= '9') {
			val = c-'0';
		} else if (c >= 'a' && c <= 'f') {
			val = 10+(c-'a');
		} else if (c >= 'A' && c <= 'F') {
			val = 10+(c-'A');
		} else return;
		break;
	case POS_ASCII:
		val = (uchar) c;
		break;
	default:
		return;
	}

	cy = ui->cursor_y;
	cx = ui->cursor_x;
	pos = cursor_pos(ui);
	bufsize = buf_size(ui->buffer);
	med = ui->monoedit;
	switch (ui->cursor_fine_pos) {
	case POS_LONIB:
		b = ui->replace_buf[ui->replace_buf_len-1];
		val |= b&0xf0;
		ui->replace_buf[ui->replace_buf_len-1] = val;
		ui_set_cursor_pos(ui, ui->cursor_y, ui->cursor_x, POS_HINIB);
		med_cx = 11+cx*3;
		med_set_char(med, cy, med_cx, c|32);
		med_set_char(med, cy, 59+cx, get_display_char(val));
		med_update_backbuffer_row(med, cy);
		med_invalidate_char(med, cy, med_cx);
		med_invalidate_char(med, cy, 59+cx);
		move_forward(ui);
		break;
	case POS_HINIB:
		if (pos < bufsize) {
			b = buf_getbyte(ui->buffer, pos);
		} else {
			b = 0;
		}
		val = val<<4 | (b&0x0f);
		if (ui->replace_buf_len == ui->replace_buf_cap) {
			ui->replace_buf = xrealloc(ui->replace_buf,
						  ui->replace_buf_cap*2);
			ui->replace_buf_cap *= 2;
		}
		ui->replace_buf[ui->replace_buf_len++] = val;
		ui_set_cursor_pos(ui, ui->cursor_y, ui->cursor_x, POS_LONIB);
		med_set_cursor_pos(med, cy, 11+cx*3);
		med_cx = 10+cx*3;
		med_set_char(med, cy, med_cx, c|32);
		med_set_char(med, cy, 59+cx, get_display_char(val));
		med_update_backbuffer_row(med, cy);
		med_invalidate_char(med, cy, med_cx);
		med_invalidate_char(med, cy, 59+cx);
		break;
	case POS_ASCII:
		hinib = hexchar(val>>4);
		lonib = hexchar(val&15);
		ui->replace_buf[ui->replace_buf_len++] = val;
		med_set_cursor_pos(med, cy, 11+cx*3);
		med_set_char(med, cy, 10+cx*3, hinib);
		med_set_char(med, cy, 11+cx*3, lonib);
		med_set_char(med, cy, 59+cx, val);
		med_update_backbuffer_row(med, cy);
		med_invalidate_char(med, cy, 10+cx*3);
		med_invalidate_char(med, cy, 11+cx*3);
		med_invalidate_char(med, cy, 59+cx);
		move_forward(ui);
		break;
	}
}

void
init_font(UI *ui)
{
	static LOGFONT logfont = {
		16,	/* lfHeight */
		0,	/* lfWidth */
		0,	/* lfEscapement */
		0,	/* lfOrientation */
		0,	/* lfWeight */
		0,	/* lfItalic */
		0,	/* lfUnderline */
		0,	/* lfStrikeOut */
		0,	/* lfCharSet */
		0,	/* lfOutPrecision */
		0,	/* lfClipPrecision */
		0,	/* lfQuality */
		0,	/* lfPitchAndFamily */
		TEXT("Courier New") /* lfFaceName */
	};

	HDC dc;
	TEXTMETRIC tm;
	HFONT mono_font;

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
med_getline(uint64 ln, T(Buf) *p, void *arg, MedTagList *taglist)
{
	uchar data[N_COL];
	Buffer *b = (Buffer *) arg;
	uint64 bufsize = buf_size(b);
	uint64 addr = ln << LOG2_N_COL;
	uint64 start = ln << LOG2_N_COL;
	int end, i;

	assert(ln >= 0);
	if (start + N_COL <= bufsize) {
		end = N_COL;
	} else if (start >= bufsize) {
		end = 0;
	} else {
		end = (int)(bufsize - start);
	}
	T(bprintf)(p, TEXT("%.8llx:"), addr);
	if (end) {
		buf_read(b, data, addr, end);
	}
	for (i=end; i<N_COL; i++) data[i] = 0;
	for (i=0; i<N_COL; i++) {
		T(bprintf)(p, TEXT(" %.2x"), data[i]);
	}
	T(bprintf)(p, TEXT("  "));
	for (i=0; i<N_COL; i++) {
		uchar c = data[i];
		p->putc(p, (TCHAR)get_display_char(c));
	}

	if (end < N_COL) {
		int start, len;
		MedTextAttr attr;
		attr.flags = MED_ATTR_TEXT_COLOR;
		attr.text_color = RGB(192, 192, 192);
		start = 10 + end * 3;
		len = (N_COL - end) * 3 - 1;
		med_add_tag(taglist, start, len, &attr);
		start = 11 + N_COL*3 + end;
		len = N_COL - end;
		med_add_tag(taglist, start, len, &attr);
	}
}

void
handle_wm_create(UI *ui, LPCREATESTRUCT create)
{
	static int sbparts[] = { 64, 128, 256, -1 };

	HWND hwnd = ui->hwnd;
	HWND monoedit;
	HWND status_bar;
	HINSTANCE instance = create->hInstance;
	HWND treeview;
	RECT rect;
	int sbheight;

	/* create and initialize MonoEdit */
	MedConfig medconf;
	medconf.mask = MED_CONFIG_GETLINE | MED_CONFIG_FONT;
	medconf.getline = med_getline;
	medconf.getline_arg = ui->buffer;
	medconf.font = ui->mono_font;
	monoedit = CreateWindow(TEXT("MonoEdit"),
				TEXT(""),
				WS_CHILD | WS_VISIBLE,
				0, 0, 0, 0,
				hwnd, (HMENU) ID_MONOEDIT, instance, &medconf);
	ui->monoedit = monoedit;

	/* subclass monoedit window */
	SetWindowLongPtr(monoedit, GWLP_USERDATA, (LONG_PTR) ui);
	ui->med_wndproc = (WNDPROC) SetWindowLongPtr
		(monoedit, GWLP_WNDPROC, (LONG_PTR) med_wndproc);

	/* create tree view */
	treeview = CreateWindow(WC_TREEVIEW,
				TEXT(""),
				WS_CHILD | WS_VISIBLE |
				TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS,
				0, 0, 0, 0,
				hwnd, 0, instance, 0);
	ui->treeview = treeview;

	/* create status bar */
	status_bar = CreateStatusWindow(WS_CHILD | WS_VISIBLE,
					NULL,
					hwnd,
					ID_STATUS_BAR);
	SendMessage(status_bar, SB_SETPARTS, 4, (LPARAM) sbparts);
	ui->status_bar = status_bar;

	/* get height of status bar */
	GetWindowRect(status_bar, &rect);
	sbheight = rect.bottom - rect.top;

	/* adjust size of main window */
	GetWindowRect(hwnd, &rect);
	rect.right = rect.left + ui->charwidth * N_COL_CHAR + 256;
	rect.bottom = rect.top +
		ui->charheight * INITIAL_N_ROW + /* MonoEdit */
		sbheight; /* status bar */
	AdjustWindowRect(&rect, GetWindowLongPtr(hwnd, GWL_STYLE), TRUE);
	SetWindowPos(hwnd, 0, 0, 0,
		     rect.right - rect.left,
		     rect.bottom - rect.top,
		     SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);

	/* set focus to edit area */
	SetFocus(monoedit);
}

LRESULT CALLBACK
wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	UI *ui = (UI *) GetWindowLongPtr(hwnd, 0);
	uint id;
	lua_State *L;

	switch (msg) {
	case WM_NCCREATE:
		ui = ((LPCREATESTRUCT)lparam)->lpCreateParams;
		ui->hwnd = hwnd;
		ui->bgbrush = (HBRUSH)
			GetClassLongPtr(hwnd, GCLP_HBRBACKGROUND);
		SetWindowLongPtr(hwnd, 0, (LONG_PTR) ui);
		break;
	case WM_CREATE:
		handle_wm_create(ui, (LPCREATESTRUCT) lparam);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			FillRect(dc, &ps.rcPaint, ui->bgbrush);
			EndPaint(hwnd, &ps);
		}
		return 0;
	case WM_SIZE:
		{
			int wid;
			int med_hei, med_wid;
			RECT r;
			/* will crash if we continue */
			if (wparam == SIZE_MINIMIZED) return 0;
			wid = LOWORD(lparam);
			med_wid = 80*ui->charwidth;
			if (ui->status_bar) {
				/* adjust status bar geometry automatically */
				SendMessage(ui->status_bar, WM_SIZE, 0, 0);
				GetWindowRect(ui->status_bar, &r);
				/* translate top-left from screen to client */
				ScreenToClient(hwnd, (LPPOINT) &r);
				med_hei = r.top;
			} else {
				GetClientRect(hwnd, &r);
				med_hei = r.bottom;
			}
			assert(med_wid > 0);
			if (med_hei < 0) med_hei = 0;
			SetWindowPos(ui->monoedit, 0, 0, 0, med_wid, med_hei,
				     SWP_NOMOVE | SWP_NOZORDER);
			SetWindowPos(ui->treeview, 0,
				     med_wid, 0, wid - med_wid, med_hei,
				     SWP_NOZORDER);
		}
		return 0;
	case WM_CHAR:
		switch (ui->mode) {
		case MODE_NORMAL:
			handle_char_normal(ui, (TCHAR) wparam);
			break;
		case MODE_REPLACE:
			handle_char_replace(ui, (TCHAR) wparam);
			break;
		default:
			assert(0);
		}
		return 0;
	case WM_COMMAND:
		id = LOWORD(wparam);
		{
			int pluginfunc = id - ID_PLUGIN_0;
			if (pluginfunc >= 0 && pluginfunc < ui->npluginfunc) {
				L = ui->lua;
				getluaobj(L, "plugin");
				lua_geti(L, -1, 1+pluginfunc);
				getluaobj(L, "buffer");
				if (lua_pcall(L, 1, 0, 0)) {
					luaerrorbox(hwnd, L);
				}
				/* If an error occurred, pops the error.
				   Otherwise pops 'plugin'. */
				lua_pop(L, 1);
				return 0;
			}
		}
		switch (id) {
			TCHAR path[BUFSIZE];
			char *path_mbcs;
			int ret;
		case ID_MONOEDIT:
			if (HIWORD(wparam) == MED_NOTIFY_POS_CHANGED) {
				switch (ui->mode) {
				case MODE_REPLACE:
					exit_replace_mode(ui);
					/* fallthrough */
				case MODE_NORMAL:
					update_logical_cursor_pos(ui);
					break;
				default:
					assert(0);
				}
				ui->cursor_pos_changed = 1;
			}
			break;
		case ID_FILE_OPEN:
			if (open_file_chooser_dialog(hwnd, path, BUFSIZE) == 0) {
				if (ui->filepath) close_file(ui);
				open_file(ui, path);
				med_reset_position(ui->monoedit);
			}
			break;
		case ID_FILE_SAVE:
			if (!ui->filepath) goto save_as;
			if (buf_save_in_place(ui->buffer)) {
				errorbox(hwnd, TEXT("Could not save file"));
			}
			break;
		case ID_FILE_SAVEAS:
save_as:
			if (save_file_chooser_dialog(hwnd, path, BUFSIZE)) break;
			if (save_file_as(ui, path)) {
				errorbox(hwnd, TEXT("Could not save file"));
			}
			break;
		case ID_FILE_CLOSE:
			close_file(ui);
			break;
		case ID_FILE_EXIT:
			SendMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
			break;
		case ID_TOOLS_LOAD_PLUGIN:
			if (open_file_chooser_dialog(hwnd, path, BUFSIZE)) break;
#ifdef UNICODE
			path_mbcs = utf16_to_mbcs(path);
#else
			path_mbcs = path;
#endif
			load_plugin(ui, path_mbcs);
#ifdef UNICODE
			free(path_mbcs);
#endif
			break;
		case ID_TOOLS_RUN_LUA_SCRIPT:
			if (open_file_chooser_dialog(hwnd, path, BUFSIZE)) break;
#ifdef UNICODE
			path_mbcs = utf16_to_mbcs(path);
#else
			path_mbcs = path;
#endif
			L = ui->lua;
			ret = luaL_dofile(L, path_mbcs);
#ifdef UNICODE
			free(path_mbcs);
#endif
			if (ret) {
				luaerrorbox(ui->hwnd, L);
				lua_pop(L, 1);
				break;
			}
			break;
		}
		return 0;
	case WM_ERASEBKGND:
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
		T(_sprintf)(title, TEXT("%s%s - WHEX"),
			  ui->filepath,
			  ui->readonly ? TEXT(" [RO]") : TEXT(""));
		SetWindowText(ui->hwnd, title);
		rfree(&rgn, top);
	} else {
		SetWindowText(ui->hwnd, TEXT("WHEX"));
	}
}

/* pops up message box if something goes wrong */
int
open_file(UI *ui, TCHAR *path)
{
	TCHAR errtext[BUFSIZE];
	Buffer *b = ui->buffer;
	HANDLE file;
	uchar readonly = 0;

	file = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
			  0 /* lpSecurityAttributes */, OPEN_ALWAYS, 0, 0);
	/* if cannot open file in rw mode, try ro */
	if (file == INVALID_HANDLE_VALUE &&
	    GetLastError() == ERROR_SHARING_VIOLATION) {
		readonly = 1;
		file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, 0,
				  OPEN_EXISTING, 0, 0);
	}
	/* still cannot open file, fail */
	if (file == INVALID_HANDLE_VALUE) {
		format_error_code(errtext, BUFSIZE, GetLastError());
		errorbox(ui->hwnd, errtext);
		return -1;
	}
	/* initialize buffer */
	if (buf_init(ui->buffer, file) < 0) {
		CloseHandle(file);
		return -1;
	}

	ui_set_filepath(ui, lstrdup(path));
	ui->readonly = readonly;

	load_filetype_plugin(ui, path);

	return 0;
}

void
close_file(UI *ui)
{
	buf_finalize(ui->buffer);
	free(ui->filepath);
	ui_set_filepath(ui, 0);
	ui_set_cursor_pos(ui, 0, 0, 0);
	ui->hl_start = 0;
	ui->hl_len = 0;
	ui->npluginfunc = 0;
	if (ui->plugin_name) {
		int i;
		free(ui->plugin_name);
		ui_set_plugin_name(ui, 0);
		ui->npluginfunc = 0;
		for (i=0; i<ui->npluginfunc; i++) {
			free(ui->plugin_funcname[i]);
		}
		free(ui->plugin_funcname);
		ui->plugin_funcname = 0;
	}
	ui_set_tree(ui, 0);
	rfreeall(&ui->tree_rgn);
}

static uint64
clamp(uint64 x, uint64 min, uint64 max)
{
	if (x < min) return min;
	if (x > max) return max;
	return x;
}

void
update_overlay(UI *ui)
{
	uint64 start = ui->hl_start;
	uint64 len = ui->hl_len;
	uint64 curline = current_line(ui);
	uint64 view_start = curline << LOG2_N_COL;
	uint64 view_end = (curline + get_nrow(ui)) << LOG2_N_COL;
	int start_clamp =
		(int)(clamp(start, view_start, view_end) - view_start);
	int end_clamp =
		(int)(clamp(start + len, view_start, view_end) - view_start);
	HWND med = ui->monoedit;
	MedTextAttr attr;
	if (end_clamp > start_clamp) {
		int first_line = start_clamp >> LOG2_N_COL;
		int last_line = (end_clamp-1) >> LOG2_N_COL; /* inclusive */
		int end_x = end_clamp & (N_COL-1);
		int byteoff = start_clamp & (N_COL-1);
		int start, len;
		attr.flags = MED_ATTR_BG_COLOR;
		attr.bg_color = RGB(204, 204, 204);
		if (end_x == 0) {
			end_x = N_COL;
		}
		if (last_line > first_line) {
			int i;
			start = 10 + byteoff * 3;
			len = (N_COL - byteoff) * 3 - 1;
			med_add_overlay(med, first_line, start, len, &attr);
			start = 11 + N_COL*3 + byteoff;
			len = N_COL - byteoff;
			med_add_overlay(med, first_line, start, len, &attr);
			for (i=first_line+1; i<last_line; i++) {
				start = 10;
				len = N_COL*3-1;
				med_add_overlay(med, i, start, len, &attr);
				start = 11+N_COL*3;
				len = N_COL;
				med_add_overlay(med, i, start, len, &attr);
			}
			start = 10;
			len = end_x * 3 - 1;
			med_add_overlay(med, last_line, start, len, &attr);
			start = 11+N_COL*3;
			len = end_x;
			med_add_overlay(med, last_line, start, len, &attr);
		} else {
			/* single line */
			start = 10 + byteoff * 3;
			len = (end_x - byteoff) * 3 - 1;
			med_add_overlay(med, first_line, start, len, &attr);
			start = 11+N_COL*3+byteoff;
			len = end_x - byteoff;
			med_add_overlay(med, first_line, start, len, &attr);
		}
	}
	attr.flags = MED_ATTR_BG_COLOR;
	attr.bg_color = RGB(255, 255, 204);
	switch (ui->cursor_fine_pos) {
	case POS_HINIB:
	case POS_LONIB:
		med_add_overlay(med, ui->cursor_y, 59+ui->cursor_x, 1, &attr);
		break;
	case POS_ASCII:
		med_add_overlay(med, ui->cursor_y, 10+ui->cursor_x*3, 2, &attr);
		break;
	}
}

/* keeps everything in sync */
void
update_ui(UI *ui)
{
	static const int toggle_menus[] = {
		ID_FILE_SAVEAS,
		ID_FILE_CLOSE,
		ID_TOOLS_LOAD_PLUGIN,
	};

	HMENU menu;
	MENUITEMINFO mii;
	uint64 bufsize;
	uint64 total_lines;
	int i;

	mii.cbSize = sizeof mii;
	mii.fMask = MIIM_STATE;

	if (ui->cursor_pos_changed) {
		ui->cursor_pos_changed = 0;
		update_field_info(ui);
	}

	if (ui->filepath_changed) {
		ui->filepath_changed = 0;
		menu = GetMenu(ui->hwnd);
		update_window_title(ui);
		if (ui->filepath) {
			ShowWindow(ui->monoedit, SW_SHOW);
			ShowWindow(ui->treeview, SW_SHOW);
			mii.fState = ui->readonly ? MFS_GRAYED : MFS_ENABLED;
			SetMenuItemInfo(menu, ID_FILE_SAVE, FALSE, &mii);
			mii.fState = MFS_ENABLED;
		} else {
			ShowWindow(ui->monoedit, SW_HIDE);
			ShowWindow(ui->treeview, SW_HIDE);
			mii.fState = MFS_GRAYED;
			SetMenuItemInfo(menu, ID_FILE_SAVE, FALSE, &mii);
		}
		for (i=0; i<NELEM(toggle_menus); i++) {
			SetMenuItemInfo(menu, toggle_menus[i], FALSE, &mii);
		}
	}

	if (ui->buffer_changed) {
		ui->buffer_changed = 0;
		bufsize = buf_size(ui->buffer);
		total_lines = bufsize >> LOG2_N_COL;
		if (bufsize&(N_COL-1)) {
			total_lines++;
		}
		med_set_total_lines(ui->monoedit, total_lines);
		med_update_buffer(ui->monoedit);
		med_update_backbuffer(ui->monoedit);
		InvalidateRect(ui->monoedit, 0, FALSE);
	}

	if (ui->plugin_name_changed) {
		ui->plugin_name_changed = 0;
		update_plugin_menu(ui);
	}

	if (ui->tree_changed) {
		ui->tree_changed = 0;
		populate_treeview(ui);
	}
}

void
move_forward(UI *ui)
{
	if (cursor_in_gap(ui)) return;
	if (ui->cursor_x+1 < N_COL) {
		ui->cursor_x++;
		update_physical_cursor_pos(ui);
	} else {
		int nrow = get_nrow(ui);
		uint64 total_lines = med_get_total_lines(ui->monoedit);
		int cy = ui->cursor_y;
		if (current_line(ui) + (cy+1) < total_lines + nrow) {
			if (cy+1 < nrow) cy++;
			else med_scroll_down_line(ui->monoedit);
			ui_set_cursor_pos(ui, cy, 0, ui->cursor_fine_pos);
			update_physical_cursor_pos(ui);
		}
	}
}

void
move_backward(UI *ui)
{
	int cy;
	if (cursor_in_gap(ui)) return;
	cy = ui->cursor_y;
	if (ui->cursor_x > 0) {
		ui->cursor_x--;
		update_physical_cursor_pos(ui);
	} else {
		int cy = ui->cursor_y;
		if (current_line(ui) + cy > 0) {
			if (cy > 0) cy--;
			else med_scroll_up_line(ui->monoedit);
			ui_set_cursor_pos(ui, cy, N_COL-1, ui->cursor_fine_pos);
			update_physical_cursor_pos(ui);
		}
	}
}

void
move_next_field(UI *ui)
{
	Buffer *b = ui->buffer;
	Tree *tree = ui->tree;
	if (tree && !cursor_in_gap(ui)) {
		uint64 cur = cursor_pos(ui);
		Tree *leaf = tree_lookup(tree, cur);
		if (leaf) {
			goto_address(ui, leaf->start + leaf->len);
		}
	}
}

void
move_prev_field(UI *ui)
{
	Buffer *b = ui->buffer;
	Tree *tree = ui->tree;
	if (tree && !cursor_in_gap(ui)) {
		uint64 cur = cursor_pos(ui);
		Tree *leaf = tree_lookup(tree, cur);
		if (leaf) {
			Tree *prev_leaf = tree_lookup(tree, cur-1);
			if (prev_leaf) {
				goto_address(ui, prev_leaf->start);
			}
		}
	}
}

void
goto_bol(UI *ui)
{
	if (buf_size(ui->buffer) > 0) {
		goto_address(ui, (current_line(ui) + ui->cursor_y) * N_COL);
	}
}

void
goto_eol(UI *ui)
{
	uint64 bufsize = buf_size(ui->buffer);
	if (bufsize > 0) {
		uint64 addr = (current_line(ui) + ui->cursor_y + 1) * N_COL - 1;
		if (addr >= bufsize) {
			addr = bufsize-1;
		}
		goto_address(ui, addr);
	}
}

INT_PTR CALLBACK
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
			text = xmalloc(len * sizeof *text);
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

int
parse_addr(TCHAR *s, uint64 *addr)
{
	uint64 n = 0;
	if (!*s) return -1;
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
			return -1;
		}
		n = (n<<4)|d;
	} while (*s);
	*addr = n;
	return 0;
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
getluaobj(lua_State *L, const char *name)
{
	lua_getglobal(L, "whex");
	lua_getfield(L, -1, name);
	lua_remove(L, -2);
}

int
load_plugin(UI *ui, const char *path)
{
	lua_State *L = ui->lua;
	Buffer *b;
	Tree *tree;
	char *plugin_name;
	int n, i;

	if (luaL_dofile(L, path)) {
		luaerrorbox(ui->hwnd, L);
		lua_pop(L, 1);
		return -1;
	}

	/* plugin information is stored as lua table at top of stack */
	luaL_checktype(L, -1, LUA_TTABLE);

	lua_getfield(L, -1, "parser"); /* plugin.parser */
	luaL_checktype(L, -1, LUA_TFUNCTION);
	getluaobj(L, "buffer");
	if (lua_pcall(L, 1, 1, 0)) {
		luaerrorbox(ui->hwnd, L);
		lua_pop(L, 1);
		return -1;
	}

	/* Get the internal node */
	if (lua_pcall(L, 0, 1, 0)) {
		luaerrorbox(ui->hwnd, L);
		lua_pop(L, 1);
		return -1;
	}

	b = ui->buffer;
	tree = convert_tree(&ui->tree_rgn, L);
	getluaobj(L, "buffer");
	lua_insert(L, -2);
	lua_setuservalue(L, -2);
	lua_pop(L, 1);
	if (ui->tree) {
		rfreeall(&ui->tree_rgn);
	}
	ui_set_tree(ui, tree);

	lua_getfield(L, -1, "name"); /* plugin.name */
	plugin_name = _strdup(luaL_checkstring(L, -1));
	lua_pop(L, 1);
	if (ui->plugin_name) {
		free(ui->plugin_name);
	}
	ui_set_plugin_name(ui, plugin_name);

	getluaobj(L, "plugin");
	lua_pushstring(L, "functions");
	lua_gettable(L, -3);
	luaL_checktype(L, -1, LUA_TTABLE);
	n = (int) luaL_len(L, -1);
	ui->npluginfunc = n;
	ui->plugin_funcname = xmalloc(n * sizeof *ui->plugin_funcname);
	for (i=0; i<n; i++) {
		char *funcname;
		lua_geti(L, -1, 1+i); /* push {func, funcname} */
		lua_geti(L, -1, 1); /* push func */
		lua_seti(L, -4, 1+i);
		lua_geti(L, -1, 2); /* push funcname */
		funcname = _strdup(luaL_checkstring(L, -1));
		lua_pop(L, 2);
		ui->plugin_funcname[i] = funcname;
	}
	lua_pop(L, 3);

	return 0;
}

void
update_plugin_menu(UI *ui)
{
	HMENU mainmenu = GetMenu(ui->hwnd);

	if (ui->plugin_name) {
		int i;
		HMENU plugin_menu = CreateMenu();
		for (i=0; i<ui->npluginfunc; i++) {
			AppendMenuA(plugin_menu, MF_STRING, ID_PLUGIN_0+i,
				    ui->plugin_funcname[i]);
		}
		ModifyMenuA(mainmenu, 2, MF_BYPOSITION | MF_POPUP,
			    (UINT_PTR) plugin_menu, ui->plugin_name);
	} else {
		ModifyMenuA(mainmenu, 2, MF_BYPOSITION | MF_STRING | MF_GRAYED,
			    0, "Plugin");
	}

	DrawMenuBar(ui->hwnd);
}

/* does not update UI */
int
load_filetype_plugin(UI *ui, const TCHAR *path)
{
	lua_State *L;
	const char *ft;
	int ret;
	int i;

	i = lstrlen(path);
	while (i > 0) {
		TCHAR c = path[--i];
		if (c == '.') break;
		if (c == '/' || c == '\\') return 0;
	}
	if (!i) return 0;
	L = ui->lua;
	getluaobj(L, "ftdet");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return -1;
	}
	getluaobj(L, "buffer");
	if (lua_pcall(L, 1, 1, 0)) {
		luaerrorbox(ui->hwnd, L);
		lua_pop(L, 1);
		return -1;
	}
	ft = 0;
	if (lua_isstring(L, -1)) {
		ft = luaL_checkstring(L, -1);
	}
	if (ft) {
		char *path = asprintf("%s/filetype/%s.lua", program_dir, ft);
		ret = load_plugin(ui, path);
		free(path);
	} else {
		ret = -1;
	}
	lua_pop(L, 1);
	return ret;
}

int
save_file_as(UI *ui, const TCHAR *path)
{
	HANDLE dstfile =
		CreateFile(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
	int ret;
	if (dstfile == INVALID_HANDLE_VALUE) {
		TCHAR errtext[BUFSIZE];
		format_error_code(errtext, BUFSIZE, GetLastError());
		errorbox(ui->hwnd, errtext);
		return -1;
	}
	ret = buf_save(ui->buffer, dstfile);
	CloseHandle(dstfile);
	return ret;
}

static void
addtotree(HWND treeview, HTREEITEM parent, Tree *tree)
{
	TVINSERTSTRUCTA tvins;
	HTREEITEM item;
	int i;

	tvins.hParent = parent;
	tvins.hInsertAfter = TVI_LAST;
	tvins.item.mask = TVIF_TEXT;
	tvins.item.pszText = tree->name;

	item = (HTREEITEM)
		SendMessage(treeview, TVM_INSERTITEMA, 0, (LPARAM) &tvins);
	for (i=0; i<tree->n_child; i++)
		addtotree(treeview, item, tree->children[i]);
}

void
populate_treeview(UI *ui)
{
	Tree *tree = ui->tree;
	SendMessage(ui->treeview, TVM_DELETEITEM, 0, (LPARAM) TVI_ROOT);
	if (tree) addtotree(ui->treeview, 0, tree);
}

void
format_leaf_value(UI *ui, Tree *t, char **ptypename, char **pvaluerepr)
{
	char *buf;
	int vrlen;

	*ptypename = 0;
	*pvaluerepr = 0;

	switch (t->type) {
		lua_State *L;
		long ival;
	case F_RAW:
		*ptypename = "raw";
		return;
	case F_UINT:
		ival = t->intvalue;
		buf = ralloc(&rgn, 24);
		switch (t->len) {
		case 1:
			*ptypename = "uint8";
			*pvaluerepr = buf;
			_sprintf(buf, "%lu (%.2lx)", ival, ival);
			return;
		case 2:
			*ptypename = "uint16";
			*pvaluerepr = buf;
			_sprintf(buf, "%lu (%.4lx)", ival, ival);
			return;
		case 4:
			*ptypename = "uint32";
			*pvaluerepr = buf;
			_sprintf(buf, "%lu (%.8lx)", ival, ival);
			return;
		}
		*ptypename = "uint";
		return;
	case F_INT:
		ival = t->intvalue;
		buf = ralloc(&rgn, 12);
		switch (t->len) {
		case 1:
			*ptypename = "int8";
			*pvaluerepr = buf;
			_sprintf(buf, "%ld", ival);
			return;
		case 2:
			*ptypename = "int16";
			*pvaluerepr = buf;
			_sprintf(buf, "%ld", ival);
			return;
		case 4:
			*ptypename = "int32";
			*pvaluerepr = buf;
			_sprintf(buf, "%ld", ival);
			return;
		}
		*ptypename = "int";
		return;
	case F_ASCII:
		*ptypename = "ascii";
		return;
	case F_CUSTOM:
		L = ui->lua;
		*ptypename = t->custom_type_name;
		getluaobj(L, "customtype");
		assert(!lua_isnil(L, -1));
		lua_getfield(L, -1, t->custom_type_name);
		assert(!lua_isnil(L, -1));
		lua_getfield(L, -1, "format");
		assert(!lua_isnil(L, -1));
		getluaobj(L, "buffer");
		lua_getuservalue(L, -1);
		lua_remove(L, -2);
		lua_getfield(L, -1, "value");
		lua_remove(L, -2);
		lua_pushinteger(L, t->intvalue);
		vrlen = 0;
		if (lua_pcall(L, 2, 1, 0)) {
			puts(lua_tostring(L, -1));
		} else {
			size_t n;
			const char *vr = luaL_tolstring(L, -1, &n);
			char *vrdup;
			n += 1;
			vrdup = ralloc(&rgn, n);
			memcpy(vrdup, vr, n);
			*pvaluerepr = vrdup;
			vrlen = n;
		}
		lua_pop(L, 3);
		return;
	}
}

int
get_nrow(UI *ui)
{
	return med_get_nrow(ui->monoedit);
}

void
exit_replace_mode(UI *ui)
{
	uint64 bufsize = buf_size(ui->buffer);
	uint64 replace_end = ui->replace_start + ui->replace_buf_len;
	ui->mode = MODE_NORMAL;
	if (ui->replace_buf_len > 0) {
		if (replace_end > bufsize) {
			/* this many bytes inserted at end of buffer */
			size_t extra = (size_t)(replace_end - bufsize);
			/* may have to update total lines */
			uint64 total_lines;
			buf_insert(ui->buffer, bufsize, 0, extra);

			total_lines = replace_end >> LOG2_N_COL;
			if (replace_end&(N_COL-1)) {
				total_lines++;
			}
			med_set_total_lines(ui->monoedit, total_lines);
		}
		buf_replace(ui->buffer, ui->replace_start,
			    ui->replace_buf, ui->replace_buf_len);
		ui->replace_buf_len = 0;
		ui->buffer_changed = 1;
	}
}

void
ui_set_tree(UI *ui, Tree *tree)
{
	if (tree != ui->tree) {
		ui->tree = tree;
		ui->tree_changed = 1;
	}
}

void
ui_set_filepath(UI *ui, TCHAR *filepath)
{
	if (filepath != ui->filepath) {
		ui->filepath = filepath;
		ui->filepath_changed = 1;
		ui->buffer_changed = 1;
	}
}

void
ui_set_plugin_name(UI *ui, char *plugin_name)
{
	if (plugin_name != ui->plugin_name) {
		ui->plugin_name = plugin_name;
		ui->plugin_name_changed = 1;
	}
}

void
ui_set_cursor_pos(UI *ui, int cursor_y, int cursor_x, uchar cursor_fine_pos)
{
	if (cursor_y != ui->cursor_y || cursor_x != ui->cursor_x ||
	    cursor_fine_pos != ui->cursor_fine_pos) {
		ui->cursor_y = cursor_y;
		ui->cursor_x = cursor_x;
		ui->cursor_fine_pos = cursor_fine_pos;
		ui->cursor_pos_changed = 1;
	}
}

char
get_display_char(uchar b)
{
	if (b < ' ' || b > '~') return '.';
	return b;
}
