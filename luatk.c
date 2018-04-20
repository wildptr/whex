#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#define BUFSIZE 512

enum {
	WINDOW,
	BUTTON,
	LABEL,
	LISTVIEW,
};

enum {
	F_WINDOW_ON_CLOSE,
	END_WINDOW
};

enum {
	F_BUTTON_ON_CLICK = END_WINDOW,
};

typedef unsigned char uchar;
typedef unsigned short ushort;

typedef struct {
	uchar kind;
	HWND hwnd;
} Window;

typedef struct {
	bool has_pos;
	bool has_size;
	char *text;
	int x, y, w, h;
	HWND parent;
} Config;

static const TCHAR luatk_class[] = TEXT("LUATK");
static lua_State *lua;

void luaerrorbox(HWND hwnd, lua_State *L);
static ATOM register_wndclass(void);
LRESULT CALLBACK luatk_wndproc(HWND, UINT, WPARAM, LPARAM);
void getluafield(lua_State *L, Window *w, int field);
void setluafield(lua_State *L, Window *w, int field, int value_index);
int api_window_move(lua_State *L);
int api_window_resize(lua_State *L);
int api_window_configure(lua_State *L);
int api_window_add_child(lua_State *L);
int api_label_new(lua_State *);
int api_listview_new(lua_State *);
int api_listview__index(lua_State *);
int api_listview__newindex(lua_State *);
int api_listview_insert_column(lua_State *L);
int api_listview_insert_item(lua_State *L);
int api_quit(lua_State *L);
void parse_config(lua_State *L, int index, Config *);

/* TODO: add type checks */

int
api_window_show(lua_State *L)
{
	Window *w = lua_touserdata(L, 1);
	ShowWindow(w->hwnd, SW_SHOW);
	return 0;
}

int
api_window_pack(lua_State *L)
{
	Window *w = lua_touserdata(L, 1);
	Window *child = lua_touserdata(L, 2);
	HWND childhwnd = child->hwnd;
	int x = luaL_checkinteger(L, 3);
	int y = luaL_checkinteger(L, 4);
	int wid = luaL_checkinteger(L, 5);
	int hei = luaL_checkinteger(L, 6);

	SetParent(childhwnd, w->hwnd);
	//LONG style = GetWindowLongPtr(childhwnd, GWL_STYLE);
	SetWindowLongPtr(childhwnd, GWL_STYLE, WS_CHILD);
	SetWindowPos(childhwnd, 0, x, y, wid, hei,
		     SWP_NOZORDER | SWP_SHOWWINDOW);
	return 0;
}

void
register_window(lua_State *L, Window *w)
{
	lua_createtable(L, END_WINDOW, 0);
	lua_setuservalue(L, -2);
	lua_pushlightuserdata(L, w->hwnd);
	lua_pushvalue(L, -2);
	lua_rawset(L, LUA_REGISTRYINDEX);
}

void
init_window(lua_State *L, Window *w, const TCHAR *wndclass, DWORD wndstyle, int cfgindex)
{
	Config c = {0};
	parse_config(L, cfgindex, &c);

	int x, y, wid, hei;
	if (c.has_pos) {
		x = c.x;
		y = c.y;
	} else {
		x = CW_USEDEFAULT;
		y = CW_USEDEFAULT;
	}
	if (c.has_size) {
		wid = c.w;
		hei = c.h;
	} else {
		wid = CW_USEDEFAULT;
		hei = CW_USEDEFAULT;
	}

	HWND hwnd;
	/* sets w->hwnd */
	hwnd = CreateWindow(wndclass,
			    c.text,
			    wndstyle,
			    x, y, wid, hei,
			    c.parent, /* might be 0 */
			    0, /* menu */
			    GetModuleHandle(0),
			    w);
	if (c.text) free(c.text);
	assert(hwnd);
	if (wndclass != luatk_class) {
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG) w);
		w->hwnd = hwnd;
	}
	register_window(L, w);
}

int
api_window_new(lua_State *L)
{
	Window *w = lua_newuserdata(L, sizeof *w);
	luaL_setmetatable(L, "Window");
	w->kind = WINDOW;
	init_window(L, w, luatk_class, WS_OVERLAPPEDWINDOW, 1);
	return 1;
}

int
api_button_new(lua_State *L)
{
	Window *w = lua_newuserdata(L, sizeof *w);
	luaL_setmetatable(L, "Button");
	w->kind = BUTTON;
	init_window(L, w, WC_BUTTON, WS_CHILD | WS_VISIBLE, 1);
	return 1;
}

static void
bindfunc(lua_State *L, const char *name, lua_CFunction f)
{
	lua_pushstring(L, name);
	lua_pushcfunction(L, f);
	lua_rawset(L, -3);
}

int
fielderr(lua_State *L, const char *cls, const char *field)
{
	char buf[512];
	snprintf(buf, sizeof buf, "class %s has no member named %s",
		 cls, field);
	lua_pushstring(L, buf);
	return lua_error(L);
}

int
window_get_text(lua_State *L, Window *w)
{
	HWND hwnd = w->hwnd;
	int n = GetWindowTextLength(hwnd)+1;
	char *buf = malloc(n);
	GetWindowText(hwnd, buf, n);
	lua_pushstring(L, buf);
	free(buf);
	return 1;
}

int
api_window__index(lua_State *L)
{
	const char *field = luaL_checkstring(L, 2);
	int len = strlen(field);
	switch (len) {
	case 4:
		if (!strcmp(field, "show")) {
			lua_pushcfunction(L, api_window_show);
			return 1;
		} else if (!strcmp(field, "pack")) {
			lua_pushcfunction(L, api_window_pack);
			return 1;
		} else if (!strcmp(field, "text")) {
			Window *w = lua_touserdata(L, 1);
			return window_get_text(L, w);
		} else if (!strcmp(field, "move")) {
			lua_pushcfunction(L, api_window_move);
			return 1;
		}
		break;
	case 6:
		if (!strcmp(field, "resize")) {
			lua_pushcfunction(L, api_window_resize);
			return 1;
		}
		break;
	case 8:
		if (!strcmp(field, "on_close")) {
			Window *w = lua_touserdata(L, 1);
			getluafield(L, w, F_WINDOW_ON_CLOSE);
			return 1;
		}
		break;
	case 9:
		if (!strcmp(field, "configure")) {
			lua_pushcfunction(L, api_window_configure);
			return 1;
		} else if (!strcmp(field, "add_child")) {
			lua_pushcfunction(L, api_window_add_child);
			return 1;
		}
		break;
	}
	return fielderr(L, "Window", field);
}

int
api_window__newindex(lua_State *L)
{
	const char *field = luaL_checkstring(L, 2);
	int len = strlen(field);
	switch (len) {
	case 4:
		if (!strcmp(field, "text")) {
			Window *w = lua_touserdata(L, 1);
			const char *text = luaL_checkstring(L, 3);
			SetWindowText(w->hwnd, text);
			return 0;
		}
		break;
	case 8:
		if (!strcmp(field, "on_close")) {
			Window *w = lua_touserdata(L, 1);
			setluafield(L, w, F_WINDOW_ON_CLOSE, 3);
			return 0;
		}
		break;
	}
	return fielderr(L, "Window", field);
}

int
api_button__index(lua_State *L)
{
	const char *field = luaL_checkstring(L, 2);
	int len = strlen(field);
	switch (len) {
	case 8:
		if (!strcmp(field, "on_click")) {
			Window *w = lua_touserdata(L, 1);
			getluafield(L, w, F_BUTTON_ON_CLICK);
			return 1;
		}
		break;
	}
	return api_window__index(L);
}

int
api_button__newindex(lua_State *L)
{
	const char *field = luaL_checkstring(L, 2);
	int len = strlen(field);
	switch (len) {
	case 8:
		if (!strcmp(field, "on_click")) {
			Window *w = lua_touserdata(L, 1);
			setluafield(L, w, F_BUTTON_ON_CLICK, 3);
			return 0;
		}
		break;
	}
	return api_window__newindex(L);
}

int
init_luatk(lua_State *L)
{
	if (!register_wndclass()) return -1;

	lua = L;

	/* Window */
	luaL_newmetatable(L, "Window");
	bindfunc(L, "__index", api_window__index);
	bindfunc(L, "__newindex", api_window__newindex);
	lua_register(L, "Window", api_window_new);

	/* Button */
	luaL_newmetatable(L, "Button");
	bindfunc(L, "__index", api_button__index);
	bindfunc(L, "__newindex", api_button__newindex);
	lua_register(L, "Button", api_button_new);

	/* Label */
	lua_register(L, "Label", api_label_new);

	/* ListView */
	luaL_newmetatable(L, "ListView");
	bindfunc(L, "__index", api_listview__index);
	bindfunc(L, "__newindex", api_listview__newindex);
	lua_register(L, "ListView", api_listview_new);

	/* Globals */
	lua_register(L, "quit", api_quit);

	return 0;
}

static ATOM
register_wndclass(void)
{
	WNDCLASS wndclass = {0};

	wndclass.lpfnWndProc = luatk_wndproc;
	wndclass.cbWndExtra = sizeof(LONG_PTR);
	wndclass.hInstance = GetModuleHandle(0);
	wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	wndclass.lpszClassName = luatk_class;
	return RegisterClass(&wndclass);
}

void
button_cmd(lua_State *L, Window *w)
{
	HWND hwnd = w->hwnd;
	lua_pushlightuserdata(L, w->hwnd);
	lua_rawget(L, LUA_REGISTRYINDEX);
	lua_getuservalue(L, -1);
	lua_rawgeti(L, -1, F_BUTTON_ON_CLICK);
	/*
	 * Stack layout:
	 * -3 userdata
	 * -2 uservalue
	 * -1 handler
	 */
	if (!lua_isnil(L, -1)) {
		lua_insert(L, -3);
		lua_pop(L, 1); /* remove uservalue */
		if (lua_pcall(L, 1, 0, 0)) {
			const char *err = lua_tostring(L, -1);
			MessageBoxA(hwnd, err, "luatk", MB_OK | MB_ICONERROR);
		}
	} else {
		lua_pop(L, 3);
	}
}

LRESULT CALLBACK
luatk_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	Window *w;

	switch (msg) {
	case WM_NCCREATE:
		w = ((LPCREATESTRUCT) lparam)->lpCreateParams;
		w->hwnd = hwnd;
		SetWindowLongPtr(hwnd, 0, (LONG_PTR) w);
		return TRUE;
	case WM_CLOSE:
		w = (Window *) GetWindowLongPtr(hwnd, 0);
		getluafield(lua, w, F_WINDOW_ON_CLOSE);
		if (!lua_isnil(lua, -1)) {
			lua_pcall(lua, 0, 0, 0);
		} else {
			lua_pop(lua, 1);
		}
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		lua_pushlightuserdata(lua, hwnd);
		lua_pushnil(lua);
		lua_rawset(lua, LUA_REGISTRYINDEX);
		return 0;
#if 0
	case WM_NOTIFY:
		MessageBoxA(hwnd, "WM_NOTIFY", "luatk", MB_OK);
		return 0;
#endif
	case WM_COMMAND:
		{
			HWND ctl = (HWND) lparam;
			Window *ctlw = (Window *)
				GetWindowLongPtr(ctl, GWLP_USERDATA);
			switch (ctlw->kind) {
			case BUTTON:
				button_cmd(lua, ctlw);
				break;
			}
		}
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void
getluafield(lua_State *L, Window *w, int field)
{
	lua_pushlightuserdata(L, w->hwnd);
	lua_rawget(L, LUA_REGISTRYINDEX);
	lua_getuservalue(L, -1);
	lua_rawgeti(L, -1, field);
	/*
	 * Stack layout:
	 * -3 userdata
	 * -2 uservalue
	 * -1 field
	 */
	lua_insert(L, -3);
	lua_pop(L, 2);
}

void
setluafield(lua_State *L, Window *w, int field, int value_index)
{
	lua_pushlightuserdata(L, w->hwnd);
	lua_rawget(L, LUA_REGISTRYINDEX);
	lua_getuservalue(L, -1);
	if (value_index < 0) value_index -= 2; /* because we just pushed two */
	lua_pushvalue(L, value_index);
	lua_rawseti(L, -2, field);
	lua_pop(L, 2);
}

int
api_window_move(lua_State *L)
{
	Window *w = lua_touserdata(L, 1);
	int x = luaL_checkinteger(L, 2);
	int y = luaL_checkinteger(L, 3);
	SetWindowPos(w->hwnd, 0, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	return 0;
}

int
api_window_resize(lua_State *L)
{
	Window *w = lua_touserdata(L, 1);
	int wid = luaL_checkinteger(L, 2);
	int hei = luaL_checkinteger(L, 3);
	SetWindowPos(w->hwnd, 0, 0, 0, wid, hei, SWP_NOMOVE | SWP_NOZORDER);
	return 0;
}

int
api_window_configure(lua_State *L)
{
	Window *w = lua_touserdata(L, 1);
	int x = luaL_checkinteger(L, 2);
	int y = luaL_checkinteger(L, 3);
	int wid = luaL_checkinteger(L, 4);
	int hei = luaL_checkinteger(L, 5);
	SetWindowPos(w->hwnd, 0, x, y, wid, hei, SWP_NOZORDER);
	return 0;
}

int
api_window_add_child(lua_State *L)
{
	Window *w = lua_touserdata(L, 1);
	Window *child = lua_touserdata(L, 2);
	SetParent(child->hwnd, w->hwnd);
	return 0;
}

int
api_label_new(lua_State *L)
{
	Window *w = lua_newuserdata(L, sizeof *w);
	luaL_setmetatable(L, "Window");
	w->kind = LABEL;
	init_window(L, w, WC_STATIC, WS_CHILD | WS_VISIBLE, 1);
	return 1;
}

int
api_listview_new(lua_State *L)
{
	Window *w = lua_newuserdata(L, sizeof *w);
	luaL_setmetatable(L, "ListView");
	w->kind = LISTVIEW;
	init_window(L, w, WC_LISTVIEW, WS_CHILD | WS_VISIBLE | LVS_REPORT, 1);
	return 1;
}

int
api_listview__index(lua_State *L)
{
	const char *field = luaL_checkstring(L, 2);
	int len = strlen(field);
	switch (len) {
	case 11:
		if (!strcmp(field, "insert_item")) {
			lua_pushcfunction(L, api_listview_insert_item);
			return 1;
		}
		break;
	case 13:
		if (!strcmp(field, "insert_column")) {
			lua_pushcfunction(L, api_listview_insert_column);
			return 1;
		}
		break;
	}
	return api_window__index(L);
}

int
api_listview__newindex(lua_State *L)
{
	const char *field = luaL_checkstring(L, 2);
	int len = strlen(field);
	switch (len) {
	}
	return api_window__newindex(L);
}

int
api_listview_insert_column(lua_State *L)
{
	Window *w = lua_touserdata(L, 1);
	int col = luaL_checkinteger(L, 2);
	char *colname = strdup(luaL_checkstring(L, 3));

	LVCOLUMN lvc;
	lvc.mask = LVCF_TEXT;
	lvc.pszText = colname;

	int ret = SendMessage(w->hwnd, LVM_INSERTCOLUMN, col, (LPARAM) &lvc);
	free(colname);
	assert(ret == col);
	return 0;
}

int
api_listview_insert_item(lua_State *L)
{
	Window *w = lua_touserdata(L, 1);
	int row = luaL_checkinteger(L, 2);
	lua_len(L, 3);
	int n = luaL_checkinteger(L, -1);
	lua_pop(L, 1);
	lua_geti(L, 3, 1);
	char *itemname = strdup(luaL_checkstring(L, -1));
	assert(itemname);

	lua_pop(L, 1);

	LV_ITEM lvi;
	lvi.iItem = row;
	lvi.iSubItem = 0;
	lvi.mask = LVIF_TEXT;
	lvi.pszText = itemname;

	int ret = SendMessage(w->hwnd, LVM_INSERTITEM, 0, (LPARAM) &lvi);
	free(itemname);
	assert(ret == row);
	for (int i=1; i<n; i++) {
		lua_geti(L, 3, 1+i);
		itemname = strdup(luaL_checkstring(L, -1));
		lua_pop(L, 1);
		lvi.iSubItem = i;
		lvi.pszText = itemname;
		ret = SendMessage(w->hwnd, LVM_SETITEM, 0, (LPARAM) &lvi);
		free(itemname);
		assert(ret == TRUE);
	}

	return 0;
}

int
api_quit(lua_State *L)
{
	PostQuitMessage(0);
	return 0;
}

void
parse_config(lua_State *L, int index, Config *c)
{
	/* text */
	lua_pushstring(L, "text");
	lua_gettable(L, index);
	if (!lua_isnil(L, -1)) {
		const char *text = luaL_checkstring(L, -1);
		if (text) {
			c->text = strdup(text);
		}
	}
	lua_pop(L, 1);

	/* pos */
	lua_pushstring(L, "pos");
	lua_gettable(L, index);
	if (!lua_isnil(L, -1)) {
		c->has_pos = true;
		lua_geti(L, -1, 1);
		c->x = luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		lua_geti(L, -1, 2);
		c->y = luaL_checkinteger(L, -1);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	/* size */
	lua_pushstring(L, "size");
	lua_gettable(L, index);
	if (!lua_isnil(L, -1)) {
		c->has_size = true;
		lua_geti(L, -1, 1);
		c->w = luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		lua_geti(L, -1, 2);
		c->h = luaL_checkinteger(L, -1);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	/* parent */
	lua_pushstring(L, "parent");
	lua_gettable(L, index);
	if (!lua_isnil(L, -1)) {
		Window *w = lua_touserdata(L, -1);
		c->parent = w->hwnd;
	}
	lua_pop(L, 1);
}
