typedef char TCHAR;
#undef UNICODE
#include "printf.c"

#include <stdio.h>
#include "filebuf.h"

int
_printf(const char *fmt, ...)
{
	FileBufA fb;
	va_list va;
	int ret;
	init_filebufA(&fb, stdout);
	va_start(va, fmt);
	ret = vbprintfA(&fb.buf, fmt, va);
	va_end(va);
	return ret;
}
