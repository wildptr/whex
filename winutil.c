#include <windows.h>

int
file_chooser_dialog(HWND owner, TCHAR *buf, int buflen)
{
	buf[0] = 0;
	OPENFILENAME ofn = {0};
	ofn.lStructSize = sizeof ofn;
	ofn.hwndOwner = owner;
	ofn.hInstance = GetModuleHandle(0);
	ofn.lpstrFile = buf;
	ofn.nMaxFile = buflen;
	if (!GetOpenFileName(&ofn)) return -1;
	return 0;
}
