#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define BUFSIZE 512

int init_luatk(lua_State *L);
int file_chooser_dialog(HWND owner, TCHAR *buf, int buflen);

void
luaerrorbox(HWND hwnd, lua_State *L)
{
	MessageBoxA(hwnd, lua_tostring(L, -1), "Error", MB_OK | MB_ICONERROR);
}

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE _p, LPSTR _c, int show)
{
	TCHAR path[BUFSIZE];

	lua_State *L = luaL_newstate();
	luaL_openlibs(L);

	init_luatk(L);

	if (file_chooser_dialog(0, path, BUFSIZE))
		return 1;
	if (luaL_dofile(L, path)) {
		luaerrorbox(0, L);
		return 1;
	}

	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return msg.wParam;
}
