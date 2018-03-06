#ifndef _MAINWINDOW_H_
#define _MAINWINDOW_H_

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#ifdef DEBUG
#define DEBUG_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__);
#else
#define DEBUG_PRINTF(...) ((void)0)
#endif

#define N_CACHE_BLOCK 16
#define LOG2_CACHE_BLOCK_SIZE 12
#define CACHE_BLOCK_SIZE (1 << LOG2_CACHE_BLOCK_SIZE)

struct cache_entry {
	long long tag;
	uint8_t *data;
};

struct tree;

struct mainwindow {
	HWND hwnd;
	HWND monoedit;
	HWND cmdedit;
	/* returns 1 if a full command has been recognized or the character is
	 * not recognized, 0 otherwise */
	long long file_size;
	long long total_lines;
	HANDLE file;
	char *filepath;
	TCHAR *monoedit_buffer;
	/* capacity of buffer, in lines (80 bytes per line) */
	int monoedit_buffer_cap_lines;
	long long current_line;
	/* number of lines displayed */
	int nrows;
	WNDPROC monoedit_wndproc;
	WNDPROC cmdedit_wndproc;
	/* current position in file */
	int cursor_x;
	int cursor_y;
	uint8_t *last_search_pattern;
	int last_search_pattern_len;
	int charwidth;
	int charheight;
	HFONT mono_font;
	struct cache_entry cache[N_CACHE_BLOCK];
	lua_State *lua;
	long long hl_start;
	long long hl_len;
	bool interactive;
	struct tree *tree;
	struct arena tree_arena;
	HWND status_bar;
};

typedef const char *(*cmdproc_t)(struct mainwindow *, char *);

bool mainwindow_cache_valid(struct mainwindow *w, int block);
int mainwindow_find_cache(struct mainwindow *w, long long address);
uint8_t mainwindow_getbyte(struct mainwindow *w, long long address);
long long mainwindow_kmp_search(struct mainwindow *w, const uint8_t *pat, int len, long long start);
long long mainwindow_kmp_search_backward(struct mainwindow *w, const uint8_t *pat, int len, long long start);
void mainwindow_update_monoedit_buffer(struct mainwindow *w, int buffer_line, int num_lines);
void mainwindow_set_current_line(struct mainwindow *w, long long line);
void mainwindow_update_cursor_pos(struct mainwindow *w);
long long mainwindow_cursor_pos(struct mainwindow *w);
void mainwindow_goto_address(struct mainwindow *w, long long address);
char *mainwindow_find(struct mainwindow *w, char *arg, bool istext);
char *mainwindow_repeat_search(struct mainwindow *w, bool reverse);
void mainwindow_execute_command(struct mainwindow *w, cmdproc_t cmdproc, char *arg);
void mainwindow_scroll_up_line(struct mainwindow *w);
void mainwindow_scroll_down_line(struct mainwindow *w);
void mainwindow_move_up(struct mainwindow *w);
void mainwindow_move_down(struct mainwindow *w);
void mainwindow_move_left(struct mainwindow *w);
void mainwindow_move_right(struct mainwindow *w);
void mainwindow_scroll_up_page(struct mainwindow *w);
void mainwindow_scroll_down_page(struct mainwindow *w);
void mainwindow_move_up_page(struct mainwindow *w);
void mainwindow_move_down_page(struct mainwindow *w);
void mainwindow_add_char_to_command(struct mainwindow *w, char c);
void mainwindow_init_font(struct mainwindow *w);
void mainwindow_handle_wm_create(struct mainwindow *w, LPCREATESTRUCT create);
void mainwindow_resize_monoedit(struct mainwindow *w, int width, int height);
int mainwindow_open_file(struct mainwindow *w, const TCHAR *path);
int mainwindow_init_cache(struct mainwindow *w);
const char *mainwindow_parse_and_execute_command(struct mainwindow *w, char *cmd);
void mainwindow_init_lua(struct mainwindow *w);
void mainwindow_update_monoedit_tags(struct mainwindow *w);
void mainwindow_update_ui(struct mainwindow *w);
void mainwindow_move_forward(struct mainwindow *w);
void mainwindow_move_backward(struct mainwindow *w);
void mainwindow_move_next_field(struct mainwindow *w);
void mainwindow_move_prev_field(struct mainwindow *w);
void mainwindow_goto_bol(struct mainwindow *w);
void mainwindow_goto_eol(struct mainwindow *w);

#define DECLARE_CMD(x) const char *x(struct mainwindow *, char *)

DECLARE_CMD(mainwindow_cmd_edit);
DECLARE_CMD(mainwindow_cmd_find_hex);
DECLARE_CMD(mainwindow_cmd_find_text);
DECLARE_CMD(mainwindow_cmd_findnext);
DECLARE_CMD(mainwindow_cmd_findprev);
DECLARE_CMD(mainwindow_cmd_goto);
DECLARE_CMD(mainwindow_cmd_hl);
DECLARE_CMD(mainwindow_cmd_lua);
DECLARE_CMD(mainwindow_cmd_luafile);
DECLARE_CMD(mainwindow_cmd_quit);

#undef DECLARE_CMD

#endif
