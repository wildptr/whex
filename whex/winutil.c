#include "u.h"
#include <windows.h>
#include "winutil.h"

int
open_file_chooser_dialog(HWND owner, TCHAR *buf, int buflen)
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

int
save_file_chooser_dialog(HWND owner, TCHAR *buf, int buflen)
{
	OPENFILENAME ofn = {0};
	buf[0] = 0;
	ofn.lStructSize = sizeof ofn;
	ofn.hwndOwner = owner;
	ofn.hInstance = GetModuleHandle(0);
	ofn.lpstrFile = buf;
	ofn.nMaxFile = buflen;
	if (!GetSaveFileName(&ofn)) return -1;
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

void
format_error_code(TCHAR *buf, size_t buflen, DWORD error_code)
{
#if 0
	DWORD FormatMessage
	(
	 DWORD dwFlags,		/* source and processing options */
	 LPCVOID lpSource,	/* pointer to message source */
	 DWORD dwMessageId,	/* requested message identifier */
	 DWORD dwLanguageId,	/* language identifier for requested message */
	 LPTSTR lpBuffer,	/* pointer to message buffer */
	 DWORD nSize,		/* maximum size of message buffer */
	 va_list *Arguments 	/* address of array of message inserts */
	);
#endif
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
		      0,
		      error_code,
		      0,
		      buf,
		      buflen,
		      0);
}
