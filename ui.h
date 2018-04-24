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
	void (*handle_char)(struct ui *, int);
	bool cursor_at_low_nibble;
	bool readonly;
} UI;
