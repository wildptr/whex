#include "u.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "winutil.h"
#include "unicode.h"

#include "luatk.h"

#if 0
LRESULT CALLBACK
wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}
#endif

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE _prev_instance, LPSTR _cmdline, int show)
{
    if (luatk_init() < 0) return 1;

#if 0
    WNDCLASS wc = {0};

    wc.lpfnWndProc = wndproc;
    wc.cbWndExtra = 0;
    wc.hInstance = instance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName = TEXT("LuaTkTest");

    ATOM wcatom = RegisterClass(&wc);
    if (!wcatom) return 1;
#endif

    /* initialize Lua */
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    luatk_api_open(L);

    if (luaL_dofile(L, "luatk_test.lua")) {
        luaerrorbox(0, L);
        lua_pop(L, 1);
    }

#if 0
    HWND hwnd = CreateWindow
        (CLASSNAME(wcatom), TEXT("LuaTk Test"), WS_OVERLAPPEDWINDOW,
         CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
         0, 0/*menu*/, instance, 0);

    ShowWindow(hwnd, show);
#endif

    MSG msg;

    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}
