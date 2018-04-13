typedef struct {
	Whex *whex;
	HWND hwnd;
	HWND monoedit;
	HWND cmdedit;
	TCHAR *med_buffer;
	/* capacity of buffer, in lines (80 bytes per line) */
	int med_buffer_nrow;
	HANDLE file;
	char *filepath;
	/* number of lines displayed */
	int nrow;
	WNDPROC med_wndproc;
	WNDPROC cmdedit_wndproc;
	long long current_line;
	/* current position in file */
	int cursor_x;
	int cursor_y;
	uint8_t *last_search_pattern;
	int last_search_pattern_len;
	int charwidth;
	int charheight;
	HFONT mono_font;
	lua_State *lua;
	long long hl_start;
	long long hl_len;
	HWND status_bar;
	Region *rgn;
} UI;
