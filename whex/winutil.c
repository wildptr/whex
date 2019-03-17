#include <windows.h>
#include "u.h"
#include "winutil.h"

int
file_chooser_dialog(HWND owner, TCHAR *buf, int buflen)
{
	OPENFILENAME ofn = {0};
	buf[0] = 0;
	ofn.lStructSize = sizeof ofn;
	ofn.hwndOwner = owner;
	ofn.hInstance = GetModuleHandle(0);
	ofn.lpstrFile = buf;
	ofn.nMaxFile = buflen;
	if (!GetOpenFileName(&ofn)) return -1;
	return 0;
}

void
msgboxf(HWND hwnd, const TCHAR *fmt, ...)
{
	T(HeapBuf) hb;
	va_list ap;
	TCHAR classname[32];
	TCHAR *s;

	T(init_heapbuf)(&hb);
	if (!GetClassName(hwnd, classname, 32)) {
		classname[0] = 0;
	}
	va_start(ap, fmt);
	T(vbprintf)(&hb.buf, fmt, ap);
	va_end(ap);
	s = T(finish_heapbuf)(&hb);
	MessageBox(hwnd, s, classname, MB_OK);
	free(hb.start);
}
