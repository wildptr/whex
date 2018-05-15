typedef struct ui {
	Buffer *buffer;
	HWND hwnd;
	HWND monoedit;
	TCHAR *med_buffer;
	/* capacity of buffer, in lines (80 bytes per line) */
	int med_buffer_nrow;
	TCHAR *filepath;
	long long total_lines;
	/* number of lines displayed */
	int nrow;
	WNDPROC med_wndproc;
	long long current_line;
	/* current position in file */
	int cursor_y;
	int cursor_x;
	int charwidth;
	int charheight;
	HFONT mono_font;
	lua_State *lua;
	long long hl_start;
	long long hl_len;
	HWND status_bar;
	HINSTANCE instance;
	int npluginfunc;
	TCHAR *plugin_name;
	TCHAR **plugin_funcname;
	uint8_t mode;
	bool cursor_at_low_nibble;
	bool readonly;
	HWND treeview;
	long long replace_start;
	uint8_t *replace_buf;
	int replace_buf_cap;
	int replace_buf_len;
} UI;
