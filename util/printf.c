/*
 * Based on Michal Ludvig's minimal snprintf() implementation
 *
 * Copyright (c) 2013,2014 Michal Ludvig <michal@logix.cz>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the author nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdarg.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "types.h"
#include "buf.h"
#include "printf.h"

typedef int (*Formatter)();

enum {
	F_SIGNED = 1,
	F_UPPERCASE = 2,
	F_ZEROPAD = 4,
	F_PRINTSIGN = 8,
};

union u {
	int i;
	long l;
	long long L;
};

#define PUTNUM10(NAME, FIELD, TYPE)\
	static TCHAR *\
	NAME(union u *value, TCHAR *buf, uint f)\
	{\
		TYPE v = value->FIELD;\
		bool neg = (f & F_SIGNED) && v < 0;\
		TCHAR *p = buf;\
		if (neg) v = -v;\
		do {\
			int d = (unsigned TYPE) v % 10;\
			*p++ = '0'+d;\
			v = (unsigned TYPE) v / 10;\
		} while (v);\
		if (neg) *p++ = '-';\
		else if (f & F_PRINTSIGN) *p++ = '+';\
		return p;\
	}

#define PUTNUM16(NAME, FIELD, TYPE)\
	static TCHAR *\
	NAME(union u *value, TCHAR *buf, uint f)\
	{\
		TYPE v = value->FIELD;\
		bool upcase = f & F_UPPERCASE;\
		TCHAR *p = buf;\
		do {\
			int d = v&15;\
			*p++ = d < 10 ? '0'+d : (upcase?'A':'a')+(d-10);\
			v = (unsigned TYPE) v >> 4;\
		} while (v);\
		return p;\
	}

PUTNUM10(puti10, i, int)
PUTNUM10(putl10, l, long)
PUTNUM10(putL10, L, long long)
PUTNUM16(puti16, i, int)
PUTNUM16(putl16, l, long)
PUTNUM16(putL16, L, long long)

typedef TCHAR *(*conv_fn)(union u *, TCHAR *, uint);

static int
putn(void *value, Buf *b, conv_fn conv, int width, uint flags)
{
	TCHAR buf[20]; /* NO TRAILING NUL */
	int ret = 0;
	int len;
	bool zeropad = flags & F_ZEROPAD;
	TCHAR pad = zeropad ? '0' : ' ';

	/* This builds the string back to front ... */
	len = conv(value, buf, flags) - buf;

	if (len < width) {
		int n = width-len;
		while (n--) ret += b->putc(b, pad);
	}

	/* ... now we reverse it (could do it recursively but will
	   conserve the stack space) */
	for (int i = 0; i < len/2; i++) {
		TCHAR tmp = buf[i];
		buf[i] = buf[len-i-1];
		buf[len-i-1] = tmp;
	}
	ret += b->puts(b, buf, len);

	return ret;
}

int
vbprintf(Buf *b, const TCHAR *fmt, va_list va)
{
	int ret = 0;
	TCHAR ch;

	while ((ch = *fmt++)) {
		if (ch!='%') {
			ret += b->putc(b, ch);
			continue;
		}

		uint flags = 0;
		TCHAR *s;
		int n;
		uint width = 0;
		int l = 0;
		conv_fn conv;
		union u val;
		int (*f)();
		void *d;

		ch = *fmt++;

		if (ch == '+') {
			ch = *fmt++;
			flags |= F_PRINTSIGN;
		}

		/* Zero padding requested */
		if (ch=='0') {
			ch = *fmt++;
			flags |= F_ZEROPAD;
		}

		/* width */
		while (ch >= '0' && ch <= '9') {
			width = width*10 + (ch - '0');
			ch = *fmt++;
		}

spec:
		switch (ch) {
		case 0:
			goto end;

		case 'l':
			l++;
			ch = *fmt++;
			goto spec;

		case 'd':
			flags |= F_SIGNED;
			/* fallthrough */
		case 'u':
			switch (l) {
			case 0:
				conv = puti10;
				val.i = va_arg(va, int);
				break;
			case 1:
				conv = putl10;
				val.l = va_arg(va, long);
				break;
			default:
				conv = putL10;
				val.L = va_arg(va, long long);
			}
			ret += putn(&val, b, conv, width, flags);
			break;

		case 'X':
			flags |= F_UPPERCASE;
			/* fallthrough */
		case 'x':
			switch (l) {
			case 0:
				conv = puti16;
				val.i = va_arg(va, int);
				break;
			case 1:
				conv = putl16;
				val.l = va_arg(va, long);
				break;
			default:
				conv = putL16;
				val.L = va_arg(va, long long);
			}
			ret += putn(&val, b, conv, width, flags);
			break;

		case 'c' :
			ret += b->putc(b, (TCHAR) va_arg(va, int));
			break;

		case 's' :
			s = va_arg(va, TCHAR *);
			n = lstrlen(s);
			ret += b->puts(b, s, n);
			break;

		case 'a':
			f = va_arg(va, Formatter);
			d = va_arg(va, void *);
			ret += f(b, d);
			break;

		default:
			ret += b->putc(b, ch);
		}
	}
end:
	return ret;
}

int
bprintf(Buf *b, const TCHAR *fmt, ...)
{
	int ret;
	va_list va;
	va_start(va, fmt);
	ret = vbprintf(b, fmt, va);
	va_end(va);
	return ret;
}

int
_wvsprintf(TCHAR *s, const TCHAR *fmt, va_list va)
{
	FixedBuf fb;
	init_fixedbuf(&fb, s);
	return vbprintf(&fb.buf, fmt, va);
}

int
_wsprintf(TCHAR *buf, const TCHAR *fmt, ...)
{
	int ret;
	va_list va;
	va_start(va, fmt);
	ret = _wvsprintf(buf, fmt, va);
	va_end(va);
	return ret;
}
