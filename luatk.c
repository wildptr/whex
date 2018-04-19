#include <assert.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <windows.h>

#define BUFSIZE 512

enum {
	WINDOW,
	BUTTON,
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

static lua_State *lua;

void luaerrorbox(HWND hwnd, lua_State *L);
static ATOM register_wndclass(void);
LRESULT CALLBACK luatk_wndproc(HWND, UINT, WPARAM, LPARAM);
void getluafield(lua_State *L, Window *w, int field);
void setluafield(lua_State *L, Window *w, int field, int value_index);

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

int
api_window_new(lua_State *L)
{
	int narg = lua_gettop(L);
	const TCHAR *title = TEXT("LuaTk");
	if (narg >= 1) {
		title = luaL_checkstring(L, 1);
	}

	Window *w = lua_newuserdata(L, sizeof *w);
	luaL_setmetatable(L, "Window");
	w->kind = WINDOW;

	HWND hwnd;
	/* sets w->hwnd */
	hwnd = CreateWindow(TEXT("LUATK"), /* class name */
			    0,
			    WS_OVERLAPPEDWINDOW,
			    CW_USEDEFAULT,
			    CW_USEDEFAULT,
			    CW_USEDEFAULT,
			    CW_USEDEFAULT,
			    0,
			    0, /* menu */
			    GetModuleHandle(0),
			    w);
	SetWindowText(hwnd, title);

	register_window(L, w);

	return 1;
}

static void
format_error_code(TCHAR *buf, size_t buflen, DWORD err)
{
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
		      0,
		      err,
		      0,
		      buf,
		      buflen,
		      0);
}

int
api_button_new(lua_State *L)
{
	const TCHAR *caption = luaL_checkstring(L, 1);

	Window *w = lua_newuserdata(L, sizeof *w);
	luaL_setmetatable(L, "Button");
	w->kind = BUTTON;
	HWND hwnd = CreateWindow(TEXT("BUTTON"),
				 caption,
				 0,
				 CW_USEDEFAULT,
				 CW_USEDEFAULT,
				 CW_USEDEFAULT,
				 CW_USEDEFAULT,
				 0,
				 0,
				 GetModuleHandle(0),
				 w);
	w->hwnd = hwnd;
	register_window(L, w);
	if (!hwnd) {
		char errmsg[512];
		format_error_code(errmsg, sizeof errmsg, GetLastError());
		MessageBoxA(0, errmsg, "luatk", MB_OK|MB_ICONERROR);
		ExitProcess(1);
	}
	SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG) w);
	return 1;
}

#if 0
static void
bindfunc(lua_State *L, const char *name, int (*fn)(lua_State *))
{
	lua_pushstring(L, name);
	lua_pushcfunction(L, fn);
	lua_rawset(L, -3);
}
#endif

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
api_Window__index(lua_State *L)
{
	const char *field = luaL_checkstring(L, 2);
	int len = strlen(field);
	switch (len) {
	case 3:
		if (!strcmp(field, "new")) {
			lua_pushcfunction(L, api_window_new);
			return 1;
		}
		break;
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
		}
		break;
	case 8:
		if (!strcmp(field, "on_close")) {
			Window *w = lua_touserdata(L, 1);
			getluafield(L, w, F_WINDOW_ON_CLOSE);
			return 1;
		}
		break;
	}
	return fielderr(L, "Window", field);
}

int
api_Window__newindex(lua_State *L)
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
api_Button__index(lua_State *L)
{
	const char *field = luaL_checkstring(L, 2);
	int len = strlen(field);
	switch (len) {
	case 3:
		if (!strcmp(field, "new")) {
			lua_pushcfunction(L, api_button_new);
			return 1;
		}
		break;
	case 8:
		if (!strcmp(field, "on_click")) {
			Window *w = lua_touserdata(L, 1);
			getluafield(L, w, F_BUTTON_ON_CLICK);
			return 1;
		}
		break;
	}
	return api_Window__index(L);
}

int
api_Button__newindex(lua_State *L)
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
	return api_Window__newindex(L);
}

int
init_luatk(lua_State *L)
{
	if (!register_wndclass()) return -1;

	lua = L;

	/* Window */
	luaL_newmetatable(L, "Window");

	/* set up __index and __newindex */
	lua_pushstring(L, "__index");
	lua_pushcfunction(L, api_Window__index);
	lua_rawset(L, -3);
	lua_pushstring(L, "__newindex");
	lua_pushcfunction(L, api_Window__newindex);
	lua_rawset(L, -3);

	luaL_setmetatable(L, "Window");
	lua_setglobal(L, "Window");

	/* Button */
	luaL_newmetatable(L, "Button");

	lua_pushstring(L, "__index");
	lua_pushcfunction(L, api_Button__index);
	lua_rawset(L, -3);
	lua_pushstring(L, "__newindex");
	lua_pushcfunction(L, api_Button__newindex);
	lua_rawset(L, -3);

	luaL_setmetatable(L, "Button");
	lua_setglobal(L, "Button");

	/* Globals */
	/* none yet */

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
	wndclass.lpszClassName = TEXT("LUATK");
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
