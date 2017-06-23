#ifndef _MAINWINDOW_H_
#define _MAINWINDOW_H_

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#define N_CACHE_BLOCK 16

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
	uint8_t *last_search_pattern;
	uint32_t last_search_pattern_len;
	int charwidth;
	int charheight;
	HFONT mono_font;
	struct cache_entry cache[N_CACHE_BLOCK];
	lua_State *lua_state;
};

typedef const char *(*cmdproc_t)(struct mainwindow *, char *);

bool mainwindow_cache_valid(struct mainwindow *w, int block);
int mainwindow_find_cache(struct mainwindow *w, uint32_t address);
uint8_t mainwindow_getbyte(struct mainwindow *w, uint32_t address);
uint32_t mainwindow_kmp_search(struct mainwindow *w, const uint8_t *pat, uint32_t len, uint32_t start);
uint32_t mainwindow_kmp_search_backward(struct mainwindow *w, const uint8_t *pat, uint32_t len, uint32_t start);
void mainwindow_update_monoedit_buffer(struct mainwindow *w, uint32_t buffer_line, uint32_t num_lines);
void mainwindow_goto_line(struct mainwindow *w, uint32_t line);
void mainwindow_update_cursor_pos(struct mainwindow *w);
void mainwindow_goto_address(struct mainwindow *w, uint32_t address);
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
int mainwindow_char_handler_normal(struct mainwindow *w, int c);
int mainwindow_char_handler_command(struct mainwindow *w, int c);
void mainwindow_add_char_to_command(struct mainwindow *w, char c);
void mainwindow_init_font(struct mainwindow *w);
void mainwindow_handle_wm_create(struct mainwindow *w, LPCREATESTRUCT create);
void mainwindow_resize_monoedit(struct mainwindow *w, uint32_t width, uint32_t height);
int mainwindow_open_file(struct mainwindow *w, const char *path);
int mainwindow_init_cache(struct mainwindow *w);
const char *mainwindow_parse_and_execute_command(struct mainwindow *w, char *cmd);
void mainwindow_init_lua(struct mainwindow *w);

#define DECLARE_CMD(x) const char *x(struct mainwindow *, char *)

DECLARE_CMD(mainwindow_cmd_find_text);
DECLARE_CMD(mainwindow_cmd_find_hex);
DECLARE_CMD(mainwindow_cmd_goto);
DECLARE_CMD(mainwindow_cmd_findprev);
DECLARE_CMD(mainwindow_cmd_findnext);

#undef DECLARE_CMD

#endif
