#include "u.h"

#include <windows.h>
#include <commctrl.h>

#include "treelistview.h"

static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);

static ATOM
register_wndclass(void)
{
    WNDCLASS wndclass = {0};

    wndclass.lpfnWndProc = wndproc;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = GetModuleHandle(0);
    wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    wndclass.lpszClassName = TEXT("TREEVIEWTEST");
    return RegisterClass(&wndclass);
}

HWND the_tlv;

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE _prev_instance, LPSTR _cmdline, int show)
{
    InitCommonControls();

    if (!tlv_register_class()) return 1;
    if (!register_wndclass()) return 1;

    HWND hwnd = CreateWindow
        (TEXT("TREEVIEWTEST"), TEXT("Tree View Test"), WS_OVERLAPPEDWINDOW,
         CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
         0, 0/*menu*/, instance, 0);

    the_tlv = CreateWindow
        (TEXT("TreeListView"),
         TEXT(""),
         WS_CHILD | WS_VISIBLE,
         0, 0, 0, 0,
         hwnd, 0, instance, 0);

    tlv_add_column(the_tlv, 128, TEXT("Name"));
    tlv_add_column(the_tlv, 128, TEXT("Type"));
    tlv_add_column(the_tlv, 128, TEXT("Value"));

    TCHAR *s[3] = { TEXT("DATA"), TEXT("uint32"), TEXT("0") };
    TLV_Item *root = tlv_add_item(the_tlv, 0, 3, s);
    tlv_add_item(the_tlv, root, 3, s);
    tlv_add_item(the_tlv, root, 3, s);
    tlv_add_item(the_tlv, root, 2, s);

    ShowWindow(hwnd, show);

    MSG msg;

    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}

static LRESULT CALLBACK
wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        {
            int w, h;
            //if (wparam == SIZE_MINIMIZED) return 0;
            w = LOWORD(lparam);
            h = HIWORD(lparam);
            SetWindowPos(the_tlv, 0, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}
