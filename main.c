#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "arena.h"
#include "mainwindow.h"
#include "monoedit.h"
#include "tree.h"
#include "unicode.h"

#include <commctrl.h>

#ifdef UNICODE
#define MBCS_TO_TSTR(x) mbcs_to_utf16(x)
#define TSTR_TO_MBCS(x) utf16_to_mbcs(x)
#else
#define MBCS_TO_TSTR(x) (x)
#define TSTR_TO_MBCS(x) (x)
#endif

#define INITIAL_N_ROW 32
#define LOG2_N_COL 4
#define N_COL (1<<LOG2_N_COL)
#define N_COL_CHAR (16+4*N_COL)

enum {
	CONTROL_STATUS_BAR = 0x100
};

char *strdup(const char *);
void register_lua_globals(lua_State *L);

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
	assert(address >= 0 && address < w->file_size);

	static int next_cache = 0;

	for (int i=0; i<N_CACHE_BLOCK; i++) {
		long long tag = w->cache[i].tag;
		if ((tag & 1) && address >> 12 == tag >> 12) return i;
	}

	long long base = address & -CACHE_BLOCK_SIZE;
	long long tag = base|1;
	SetFilePointer(w->file, base, 0, FILE_BEGIN);
	DWORD nread;
	int ret = next_cache;
	ReadFile(w->file, w->cache[ret].data, CACHE_BLOCK_SIZE, &nread, 0);
	w->cache[ret].tag = tag;
	next_cache = (ret+1)&(N_CACHE_BLOCK-1);

	DEBUG_PRINTF("loaded %I64x into cache block %d\n", base, ret);

	return ret;
}

uint8_t mainwindow_getbyte(struct mainwindow *w, long long address)
{
	int block = mainwindow_find_cache(w, address);
	return w->cache[block].data[address & (CACHE_BLOCK_SIZE-1)];
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
		TCHAR *line = &w->monoedit_buffer[buffer_line*N_COL_CHAR];
		TCHAR *p = line;
		long long address = abs_line << LOG2_N_COL;
		if (abs_line >= w->total_lines) {
			for (int i=0; i<N_COL_CHAR; i++) {
				p[i] = ' ';
			}
		} else {
			int block = mainwindow_find_cache(w, address);
			int base = address & (CACHE_BLOCK_SIZE-1);
			wsprintf(p, TEXT("%08I64x: "), address);
			p += 10;
			int end = 0;
		       	if (abs_line+1 >= w->total_lines) {
				end = w->file_size & (N_COL-1);
			}
			if (!end) {
				end = N_COL;
			}
			for (int j=0; j<end; j++) {
				wsprintf(p, TEXT("%02x "), w->cache[block].data[base|j]);
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

static uint8_t hexval(char c)
{
	return c > '9' ? 10+(c|32)-'a' : c-'0';
}

static uint8_t hextobyte(const uint8_t *p)
{
	return hexval(p[0]) << 4 | hexval(p[1]);
}

void mainwindow_set_current_line(struct mainwindow *w, long long line)
{
	assert(line <= w->total_lines);
	w->current_line = line;
	if (w->interactive) {
		mainwindow_update_monoedit_buffer(w, 0, w->nrows);
		mainwindow_update_monoedit_tags(w);
		mainwindow_update_cursor_pos(w);
		InvalidateRect(w->monoedit, 0, FALSE);
	}
}

void mainwindow_update_status_text(struct mainwindow *w, struct tree *leaf)
{
	const TCHAR *type_name = TEXT("unknown");
	TCHAR value_buf[80];
	value_buf[0] = 0;
	char *path = 0;
	TCHAR *path_tstr = 0;
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
				ival = mainwindow_getbyte(w, leaf->start);
				wsprintf(value_buf, TEXT("%u (%02x)"), ival, ival);
				break;
			case 2:
				type_name = TEXT("uint16");
				ival = mainwindow_getbyte(w, leaf->start) |
					mainwindow_getbyte(w, leaf->start + 1) << 8;
				wsprintf(value_buf, TEXT("%u (%04x)"), ival, ival);
				break;
			case 4:
				type_name = TEXT("uint32");
				ival = mainwindow_getbyte(w, leaf->start)
					| mainwindow_getbyte(w, leaf->start + 1) << 8
					| mainwindow_getbyte(w, leaf->start + 2) << 16
					| mainwindow_getbyte(w, leaf->start + 3) << 24;
				wsprintf(value_buf, TEXT("%u (%08x)"), ival, ival);
				break;
			case 8:
				type_name = TEXT("uint64");
				ival = mainwindow_getbyte(w, leaf->start)
					| mainwindow_getbyte(w, leaf->start + 1) << 8
					| mainwindow_getbyte(w, leaf->start + 2) << 16
					| mainwindow_getbyte(w, leaf->start + 3) << 24;
				ival_hi = mainwindow_getbyte(w, leaf->start + 4)
					| mainwindow_getbyte(w, leaf->start + 5) << 8
					| mainwindow_getbyte(w, leaf->start + 6) << 16
					| mainwindow_getbyte(w, leaf->start + 7) << 24;
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
				uint8_t b = mainwindow_getbyte(w, leaf->start + i);
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
		path = tree_path(leaf);
		path_tstr = MBCS_TO_TSTR(path);
	}
	TCHAR cursor_pos_buf[17];
	wsprintf(cursor_pos_buf, TEXT("%I64x"), mainwindow_cursor_pos(w));
	SendMessage(w->status_bar, SB_SETTEXT, 0, (LPARAM) cursor_pos_buf);
	SendMessage(w->status_bar, SB_SETTEXT, 1, (LPARAM) type_name);
	SendMessage(w->status_bar, SB_SETTEXT, 2, (LPARAM) value_buf);
	SendMessage(w->status_bar, SB_SETTEXT, 3, (LPARAM) path_tstr);
#ifdef UNICODE
	free(path_tstr);
#endif
	free(path);
}

// should be invoked when cursor_pos is changed in interactive mode
void mainwindow_update_field_info(struct mainwindow *w)
{
	if (w->tree) {
		struct tree *leaf = tree_lookup(w->tree, mainwindow_cursor_pos(w));
		if (leaf) {
			w->hl_start = leaf->start;
			w->hl_len = leaf->len;
		} else {
			w->hl_start = 0;
			w->hl_len = 0;
		}
		if (w->interactive) {
			mainwindow_update_monoedit_tags(w);
			InvalidateRect(w->monoedit, 0, FALSE);
			mainwindow_update_status_text(w, leaf);
		}
	} else {
		mainwindow_update_status_text(w, 0);
	}
}

void mainwindow_update_cursor_pos(struct mainwindow *w)
{
	SendMessage(w->monoedit,
		    MONOEDIT_WM_SET_CURSOR_POS,
		    10+w->cursor_x*3,
		    w->cursor_y);
	mainwindow_update_field_info(w);
	SendMessage(w->monoedit, MONOEDIT_WM_SET_CURSOR_POS,
		    10+w->cursor_x*3, w->cursor_y);
}

long long mainwindow_cursor_pos(struct mainwindow *w)
{
	return ((w->current_line + w->cursor_y) << LOG2_N_COL) + w->cursor_x;
}

void mainwindow_goto_address(struct mainwindow *w, long long address)
{
	long long line = address >> LOG2_N_COL;
	int col = address & (N_COL-1);
	assert(address >= 0 && address < w->file_size);
	w->cursor_x = col;
	if (line >= w->current_line && line < w->current_line + w->nrows) {
		w->cursor_y = line - w->current_line;
		if (w->interactive) {
			mainwindow_update_cursor_pos(w);
		}
	} else {
		long long line1;
		if (line >= w->nrows >> 1) {
			line1 = line - (w->nrows >> 1);
		} else {
			line1 = 0;
		}
		w->cursor_y = line - line1;
		mainwindow_set_current_line(w, line1);
	}
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
	if (w->last_search_pattern) {
		free(w->last_search_pattern);
	}
	w->last_search_pattern = malloc(patlen);
	memcpy(w->last_search_pattern, pat, patlen);
	w->last_search_pattern_len = patlen;
	long long matchpos = mainwindow_kmp_search(w, pat, patlen, mainwindow_cursor_pos(w));
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
		long long tmp = mainwindow_cursor_pos(w) + w->last_search_pattern_len - 1;
		if (w->file_size >= tmp) {
			start = w->file_size - tmp;
		} else {
			return "pattern not found";
		}
		search_func = mainwindow_kmp_search_backward;
	} else {
		long long cursor_pos = mainwindow_cursor_pos(w);
		if (cursor_pos+1 < w->file_size) {
			start = cursor_pos+1;
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

void mainwindow_error_prompt(struct mainwindow *w, const char *errmsg)
{
	TCHAR *errmsg_tstr = MBCS_TO_TSTR(errmsg);
	MessageBox(w->hwnd, errmsg_tstr, TEXT("Error"), MB_ICONERROR);
#ifdef UNICODE
	free(errmsg_tstr);
#endif
}

void mainwindow_execute_command(struct mainwindow *w, cmdproc_t cmdproc, char *arg)
{
	const char *errmsg = cmdproc(w, arg);
	if (errmsg) {
		mainwindow_error_prompt(w, errmsg);
	}
}

void mainwindow_scroll_up_line(struct mainwindow *w)
{
	if (w->current_line) {
		w->current_line--;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, -1);
		memmove(w->monoedit_buffer+N_COL_CHAR, w->monoedit_buffer, N_COL_CHAR*(w->nrows-1)*sizeof(TCHAR));
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, 0, 1);
			mainwindow_update_monoedit_tags(w);
			mainwindow_update_cursor_pos(w);
		}
	}
}

void mainwindow_scroll_down_line(struct mainwindow *w)
{
	if (mainwindow_cursor_pos(w) + N_COL < w->file_size) {
		w->current_line++;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, 1);
		memmove(w->monoedit_buffer, w->monoedit_buffer+N_COL_CHAR, N_COL_CHAR*(w->nrows-1)*sizeof(TCHAR));
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, w->nrows-1, 1);
			mainwindow_update_monoedit_tags(w);
			mainwindow_update_cursor_pos(w);
		}
	}
}

void mainwindow_move_up(struct mainwindow *w)
{
	if (mainwindow_cursor_pos(w) >= N_COL) {
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
	if (w->file_size >= N_COL && mainwindow_cursor_pos(w) < w->file_size - N_COL) {
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
		mainwindow_update_cursor_pos(w);
	}
}

void mainwindow_move_right(struct mainwindow *w)
{
	if (w->cursor_x < (N_COL-1) && mainwindow_cursor_pos(w)+1 < w->file_size) {
		w->cursor_x++;
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
			mainwindow_update_cursor_pos(w);
			InvalidateRect(w->monoedit, 0, FALSE);
		}
	} else {
		long long delta = w->current_line;
		w->current_line = 0;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, -delta);
		memmove(w->monoedit_buffer+N_COL_CHAR*delta, w->monoedit_buffer, N_COL_CHAR*(w->nrows-delta)*sizeof(TCHAR));
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, 0, delta);
			mainwindow_update_monoedit_tags(w);
			mainwindow_update_cursor_pos(w);
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
			mainwindow_update_cursor_pos(w);
			InvalidateRect(w->monoedit, 0, FALSE);
		}
	} else {
		long long delta = w->total_lines - w->current_line;
		w->current_line = w->total_lines;
		SendMessage(w->monoedit, MONOEDIT_WM_SCROLL, 0, delta);
		memmove(w->monoedit_buffer, w->monoedit_buffer+N_COL_CHAR*delta, N_COL_CHAR*(w->nrows-delta)*sizeof(TCHAR));
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, w->nrows-delta, delta);
			mainwindow_update_monoedit_tags(w);
			mainwindow_update_cursor_pos(w);
		}
	}
}

void mainwindow_move_up_page(struct mainwindow *w)
{
	if (mainwindow_cursor_pos(w) >= N_COL*w->nrows) {
		mainwindow_scroll_up_page(w);
	}
}

void mainwindow_move_down_page(struct mainwindow *w)
{
	if (w->file_size >= N_COL*w->nrows && mainwindow_cursor_pos(w) < w->file_size - N_COL*w->nrows) {
		mainwindow_scroll_down_page(w);
	}
}

int mainwindow_handle_char(struct mainwindow *w, int c)
{
	switch (c) {
		TCHAR buf[2];
	case 8: // backspace
		mainwindow_move_backward(w);
		break;
	case ' ':
		mainwindow_move_forward(w);
		break;
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
	case 'b':
		mainwindow_move_prev_field(w);
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
	case 'w':
		mainwindow_move_next_field(w);
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
	struct mainwindow *w = (void *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

	switch (message) {
	case WM_LBUTTONDOWN:
		{
			int x = LOWORD(lparam);
			int y = HIWORD(lparam);
			SetFocus(hwnd);
			int cx = x / w->charwidth;
			int cy = y / w->charheight;
			if (cx >= 10 && cx < 10+N_COL*3 && cy < w->nrows) {
				cx = (cx-10)/3;
				long long pos = ((w->current_line + cy) << LOG2_N_COL) + cx;
				if (pos < w->file_size) {
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
			//mainwindow_goto_address(w, 0);
			mainwindow_goto_bol(w);
			break;
		case VK_END:
			/*if (w->file_size) {
				mainwindow_goto_address(w, w->file_size-1);
			}*/
			mainwindow_goto_eol(w);
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
	struct mainwindow *w = (void *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

	switch (message) {
	case WM_CHAR:
		switch (wparam) {
			char *cmd;
			TCHAR *raw_cmd;
			int buf_len;
			const char *errmsg;
		case '\r':
			buf_len = GetWindowTextLength(hwnd)+1;
			raw_cmd = malloc(buf_len * sizeof *raw_cmd);
			GetWindowText(hwnd, raw_cmd, buf_len);
			cmd = TSTR_TO_MBCS(raw_cmd);
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
			free(raw_cmd);
#ifdef UNICODE
			free(cmd);
#endif
			if (errmsg) {
				mainwindow_error_prompt(w, errmsg);
			}
			// fallthrough
		case 27: // escape
			SetWindowText(hwnd, TEXT(""));
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
	w->monoedit = monoedit;
	w->nrows = INITIAL_N_ROW;
	w->monoedit_buffer = malloc(N_COL_CHAR*INITIAL_N_ROW*sizeof(TCHAR));
	w->monoedit_buffer_cap_lines = INITIAL_N_ROW;
	for (int i=0; i<N_COL_CHAR*INITIAL_N_ROW; i++) {
		w->monoedit_buffer[i] = ' ';
	}
	SendMessage(monoedit, MONOEDIT_WM_SET_CSIZE, N_COL_CHAR, INITIAL_N_ROW);
	SendMessage(monoedit, MONOEDIT_WM_SET_BUFFER, 0, (LPARAM) w->monoedit_buffer);
	SendMessage(monoedit, WM_SETFONT, (WPARAM) w->mono_font, 0);
	mainwindow_update_monoedit_buffer(w, 0, INITIAL_N_ROW);
	/* subclass monoedit window */
	SetWindowLongPtr(monoedit, GWLP_USERDATA, (LONG_PTR) w);
	w->monoedit_wndproc = (WNDPROC) SetWindowLongPtr(monoedit, GWLP_WNDPROC, (LONG_PTR) monoedit_wndproc);
	SetFocus(monoedit);
	mainwindow_update_cursor_pos(w);

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
	SendMessage(cmdedit, WM_SETFONT, (WPARAM) w->mono_font, 0);
	w->cmdedit = cmdedit;
	// subclass command window
	SetWindowLongPtr(cmdedit, GWLP_USERDATA, (LONG_PTR) w);
	w->cmdedit_wndproc = (WNDPROC) SetWindowLongPtr(cmdedit, GWLP_WNDPROC, (LONG_PTR) cmdedit_wndproc);

	// create status bar
	status_bar = CreateStatusWindow(WS_CHILD | WS_VISIBLE,
					NULL,
					hwnd,
					CONTROL_STATUS_BAR);
	int parts[] = { 64, 128, 256, -1 };
	SendMessage(status_bar, SB_SETPARTS, 4, (LPARAM) parts);
	w->status_bar = status_bar;

	// get height of status bar
	RECT rect;
	GetWindowRect(status_bar, &rect);
	int status_bar_height = rect.bottom - rect.top;

	// adjust size of main window
	GetWindowRect(hwnd, &rect);
	rect.right = rect.left + w->charwidth * N_COL_CHAR;
	rect.bottom = rect.top +
		w->charheight * INITIAL_N_ROW + // MonoEdit
		w->charheight + // command window
		status_bar_height; // status bar
	AdjustWindowRect(&rect, GetWindowLongPtr(hwnd, GWL_STYLE), FALSE);
	SetWindowPos(hwnd, 0, 0, 0,
		     rect.right - rect.left,
		     rect.bottom - rect.top,
		     SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);

	SetWindowText(hwnd, TEXT("WHEX"));
}

void mainwindow_resize_monoedit(struct mainwindow *w, int width, int height)
{
	int new_nrows = height/w->charheight;
	if (new_nrows > w->nrows) {
		if (new_nrows > w->monoedit_buffer_cap_lines) {
			w->monoedit_buffer = realloc(w->monoedit_buffer, N_COL_CHAR*new_nrows*sizeof(TCHAR));
			w->monoedit_buffer_cap_lines = new_nrows;
			SendMessage(w->monoedit, MONOEDIT_WM_SET_BUFFER, 0, (LPARAM) w->monoedit_buffer);
		}
		if (w->interactive) {
			mainwindow_update_monoedit_buffer(w, w->nrows, new_nrows - w->nrows);
			mainwindow_update_monoedit_tags(w);
		}
	}
	w->nrows = new_nrows;
	SendMessage(w->monoedit, MONOEDIT_WM_SET_CSIZE, -1, height/w->charheight);
	SetWindowPos(w->monoedit,
		     0,
		     0,
		     0,
		     width,
		     height,
		     SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);
	if (w->interactive) {
		InvalidateRect(w->monoedit, 0, FALSE);
	}
}

LRESULT CALLBACK
wndproc(HWND hwnd,
	UINT message,
	WPARAM wparam,
	LPARAM lparam)
{
	struct mainwindow *w = (void *) GetWindowLongPtr(hwnd, 0);
	switch (message) {
	case WM_NCCREATE:
		w = ((LPCREATESTRUCT)lparam)->lpCreateParams;
		w->hwnd = hwnd;
		SetWindowLongPtr(hwnd, 0, (LONG_PTR) w);
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
			// adjust status bar geometry automatically
			SendMessage(w->status_bar, WM_SIZE, 0, 0);
			RECT status_bar_rect;
			GetWindowRect(w->status_bar, &status_bar_rect);
			// translate top-left from screen to client
			ScreenToClient(w->hwnd, (LPPOINT) &status_bar_rect);
			int width  = LOWORD(lparam);
			//int height = HIWORD(lparam);
			int cmd_y = status_bar_rect.top - w->charheight;
			if (cmd_y < 0) cmd_y = 0;
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
	wndclass.cbWndExtra = sizeof(LONG_PTR);
	wndclass.hInstance = GetModuleHandle(0);
	wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	wndclass.lpszClassName = TEXT("WHEX");
	return RegisterClass(&wndclass);
}

static void format_error_code(TCHAR *buf, size_t buflen, DWORD error_code)
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

// usually you want to update UI after this
int mainwindow_open_file(struct mainwindow *w, const TCHAR *path)
{
	static char errfmt_open[] = "Failed to open %s: %s\n";
	static char errfmt_size[] = "Failed to retrieve size of %s: %s\n";
	TCHAR errtext[512];

	if (w->file != INVALID_HANDLE_VALUE) {
		CloseHandle(w->file);
	}
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
	if (w->filepath) {
		free(w->filepath);
	}
#ifdef UNICODE
	w->filepath = utf16_to_mbcs(path);
#else
	w->filepath = strdup(path);
#endif
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
	DEBUG_PRINTF("file size: %I64u (0x%I64x)\n", w->file_size, w->file_size);
	w->total_lines = w->file_size >> LOG2_N_COL;
	if (w->file_size&(N_COL-1)) {
		w->total_lines += 1;
	}
	// invalidate cache
	for (int i=0; i<N_CACHE_BLOCK; i++) {
		w->cache[i].tag = 0;
	}
	return 0;
}

int mainwindow_init_cache(struct mainwindow *w)
{
	uint8_t *cache_data = malloc(N_CACHE_BLOCK << LOG2_CACHE_BLOCK_SIZE);
	if (!cache_data) {
		return -1;
	}
	for (int i=0; i<N_CACHE_BLOCK; i++) {
		w->cache[i].tag = 0;
		w->cache[i].data = cache_data + (i << LOG2_CACHE_BLOCK_SIZE);
	}
	return 0;
}

static int file_chooser_dialog(TCHAR *buf, int buflen)
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

static void usage(void)
{
	fprintf(stderr, "incorrect usage\n");
	exit(1);
}

static int
start_gui(HINSTANCE instance,
	  int show,
	  struct mainwindow *w,
	  const TCHAR *filepath)
{
	InitCommonControls();
	if (!monoedit_register_class()) {
		return 1;
	}
	if (!register_wndclass()) {
		return 1;
	}
	if (mainwindow_open_file(w, filepath) < 0) {
		return 1;
	}
	if (mainwindow_init_cache(w) < 0) {
		return 1;
	}
	mainwindow_init_font(w);
	//RECT rect = { 0, 0, w->charwidth*N_COL_CHAR, w->charheight*(INITIAL_N_ROW+1) };
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
				 w); // window-creation data
	ShowWindow(hwnd, show);
	mainwindow_update_ui(w);
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	// window closed now
	return msg.wParam;
}

int APIENTRY
WinMain(HINSTANCE instance,
	HINSTANCE prev_instance,
	LPSTR cmdline,
	int show)
{
	int argc;
	TCHAR **argv;
	TCHAR openfilename[512];
	const TCHAR *filepath;
	const TCHAR *script_path = 0;

#ifdef UNICODE
	argv = CommandLineToArgvW(GetCommandLine(), &argc);
#else
	// TODO test this
	wchar_t *cmdline_utf16 = mbcs_to_utf16(cmdline);
	wchar_t **argv_utf16 = CommandLineToArgvW(cmdline_utf16, &argc);
	argv = malloc((argc+1) * sizeof *argv);
	for (int i=0; i<argc; i++) {
		argv[i] = utf16_to_mbcs(argv_utf16[i]);
	}
	argv[argc] = 0;
#endif
	// parse command line arguments
	if (argc == 1) {
		if (file_chooser_dialog(openfilename, sizeof openfilename /
					sizeof *openfilename)) return 1;
		filepath = openfilename;
	} else {
		if (!lstrcmp(argv[1], TEXT("-c"))) {
			if (argc < 3) usage();
			script_path = argv[2];
		} else {
			// GUI mode
			filepath = argv[1];
		}
	}

	// initialize mainwindow struct
	struct mainwindow *w = calloc(1, sizeof *w);
	w->file = INVALID_HANDLE_VALUE;
	w->interactive = !script_path;
	mainwindow_init_lua(w);

	if (script_path) {
		char *script_path_mbcs;
#ifdef UNICODE
		script_path_mbcs = utf16_to_mbcs(script_path);
#else
		script_path_mbcs = script_path;
#endif
		DEBUG_PRINTF("execute Lua script %s\n", script_path_mbcs);
		lua_State *L = w->lua;
		int err = luaL_loadfile(L, script_path_mbcs) ||
			lua_pcall(L, 0, 0, 0);
#ifdef UNICODE
		free(script_path_mbcs);
#endif
		if (err) {
			const char *err = lua_tostring(L, -1);
			fputs(err, stderr);
			return 1;
		}
		return 0;
	}
	DEBUG_PRINTF("starting GUI\n");
	return start_gui(instance, show, w, filepath);
}

// commands

const char *mainwindow_cmd_goto(struct mainwindow *w, char *arg)
{
	long long address;
	if (sscanf(arg, "%I64x", &address) == 1) {
		if (address < 0 || address >= w->file_size) {
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
	lua_State *L = w->lua;

	bool saved_interactive = w->interactive;
	w->interactive = false;
	error = luaL_loadbuffer(L, arg, strlen(arg), "line") ||
		lua_pcall(L, 0, 0, 0);
	w->interactive = saved_interactive;
	if (error) {
		const char *err = lua_tostring(L, -1);
		return err;
	}
	if (w->interactive) {
		mainwindow_update_ui(w);
	}
	return 0;
}

const char *mainwindow_cmd_luafile(struct mainwindow *w, char *arg)
{
	while (*arg == ' ') arg++;
	char *filepath_mbcs = arg;
	while (*arg && *arg != ' ') arg++;
	if (*arg) {
		return "trailing character";
	}

	if (!*filepath_mbcs) {
		return "no file specified";
	}

	int error;
	lua_State *L = w->lua;

	bool saved_interactive = w->interactive;
	w->interactive = false;
	// luaL_loadfile uses fopen() which accepts MBCS string
	error = luaL_loadfile(L, filepath_mbcs) || lua_pcall(L, 0, 0, 0);
	w->interactive = saved_interactive;
	if (error) {
		const char *err = lua_tostring(L, -1);
		return err;
	}
	if (w->interactive) {
		mainwindow_update_ui(w);
	}
	return 0;
}

const char *mainwindow_cmd_hl(struct mainwindow *w, char *arg)
{
	long long start = 0;
	long long len = 0;
	sscanf(arg, "%I64d%I64d", &start, &len);
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
	case 1:
		if (!memcmp(start, "e", 1)) {
			cmdproc = mainwindow_cmd_edit;
		} else if (!memcmp(start, "q", 1)) {
			cmdproc = mainwindow_cmd_quit;
		}
		break;
	case 2:
		if (!memcmp(start, "hl", 2)) {
			cmdproc = mainwindow_cmd_hl;
		}
		break;
	case 3:
		if (!memcmp(start, "lua", 3)) {
			cmdproc = mainwindow_cmd_lua;
		}
		break;
	case 7:
		if (!memcmp(start, "luafile", 7)) {
			cmdproc = mainwindow_cmd_luafile;
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
	assert(!w->lua);
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	// store `w` at REGISTRY[0]
	lua_pushinteger(L, 0);
	lua_pushlightuserdata(L, w);
	lua_settable(L, LUA_REGISTRYINDEX);
	// register C functions
	register_lua_globals(L);
	w->lua = L;
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
	long long len = w->hl_len;
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

void mainwindow_update_ui(struct mainwindow *w)
{
	mainwindow_update_monoedit_buffer(w, 0, w->nrows);
	mainwindow_update_monoedit_tags(w);
	mainwindow_update_cursor_pos(w);
	InvalidateRect(w->monoedit, 0, FALSE);
}

const char *mainwindow_cmd_edit(struct mainwindow *w, char *arg)
{
	while (*arg == ' ') arg++;
	char *start = arg;
	while (*arg && *arg != ' ') arg++;
	if (*arg) {
		return "trailing character";
	}

	TCHAR *filepath = MBCS_TO_TSTR(start);
	int err = mainwindow_open_file(w, filepath);
#ifdef UNICODE
	free(filepath);
#endif
	if (err) {
		return "failed to open file";
	}
	w->current_line = 0;
	w->cursor_y = 0;
	w->cursor_x = 0;
	if (w->interactive) {
		mainwindow_update_ui(w);
	}
	return 0;
}

const char *mainwindow_cmd_quit(struct mainwindow *w, char *arg)
{
	SendMessage(w->hwnd, WM_CLOSE, 0, 0);
	return 0;
}

void mainwindow_move_forward(struct mainwindow *w)
{
	if (mainwindow_cursor_pos(w) < w->file_size) {
		if (w->cursor_x < N_COL-1) {
			mainwindow_move_right(w);
		} else {
			w->cursor_x = 0;
			mainwindow_move_down(w);
		}
	}
}

void mainwindow_move_backward(struct mainwindow *w)
{
	if (mainwindow_cursor_pos(w) > 0) {
		if (w->cursor_x > 0) {
			mainwindow_move_left(w);
		} else {
			w->cursor_x = N_COL-1;
			mainwindow_move_up(w);
		}
	}
}

void mainwindow_move_next_field(struct mainwindow *w)
{
	if (w->tree) {
		long long cursor_pos = mainwindow_cursor_pos(w);
		struct tree *leaf = tree_lookup(w->tree, cursor_pos);
		if (leaf) {
			mainwindow_goto_address(w, leaf->start + leaf->len);
		}
	}
}

void mainwindow_move_prev_field(struct mainwindow *w)
{
	if (w->tree) {
		long long cursor_pos = mainwindow_cursor_pos(w);
		struct tree *leaf = tree_lookup(w->tree, cursor_pos);
		if (leaf) {
			struct tree *prev_leaf = tree_lookup(w->tree, cursor_pos-1);
			if (prev_leaf) {
				mainwindow_goto_address(w, prev_leaf->start);
			}
		}
	}
}

void mainwindow_goto_bol(struct mainwindow *w)
{
	if (w->file_size > 0) {
		mainwindow_goto_address(w, (w->current_line + w->cursor_y) * N_COL);
	}
}

void mainwindow_goto_eol(struct mainwindow *w)
{
	if (w->file_size > 0) {
		long long addr = (w->current_line + w->cursor_y + 1) * N_COL - 1;
		if (addr >= w->file_size) {
			addr = w->file_size-1;
		}
		mainwindow_goto_address(w, addr);
	}
}
