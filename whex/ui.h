typedef struct ui {
	Buffer *buffer;
	HWND hwnd;
	HWND monoedit;
	TCHAR *filepath;
	long long total_lines;
	/* number of lines displayed */
	int nrow;
	WNDPROC med_wndproc;
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
	char *plugin_name;
	char **plugin_funcname;
	uint8_t mode;
	bool cursor_at_low_nibble;
	bool readonly;
	HWND treeview;
	long long replace_start;
	uint8_t *replace_buf;
	int replace_buf_cap;
	int replace_buf_len;
} UI;
