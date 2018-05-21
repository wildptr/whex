#include <windows.h>
#include "types.h"
#include "buf.h"
#include "printf.h"
#include "winutil.h"

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

void
msgboxf(HWND hwnd, const TCHAR *fmt, ...)
{
	HeapBuf hb;
	va_list ap;
	TCHAR classname[32];

	if (init_heapbuf(&hb)) return;
	if (!GetClassName(hwnd, classname, 32)) {
		classname[0] = 0;
	}
	va_start(ap, fmt);
	vbprintf(&hb.buf, fmt, ap);
	va_end(ap);
	MessageBox(hwnd, hb.start, classname, MB_OK);
	free(hb.start);
}
