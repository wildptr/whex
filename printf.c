/* THIS FILE IS MEANT TO BE INCLUDED FROM u.c; DO NOT USE DIRECTLY */

/* buffer */

static void
T_(heapbuf_grow)(T_(HeapBuf) *hb, int newsize)
{
    _TCHAR *newbuf = xrealloc(hb->start, newsize * sizeof *newbuf);
    int len;
    len = hb->cur - hb->start;
    hb->start = newbuf;
    hb->cur = newbuf + len;
    hb->end = newbuf + newsize;
}

static void
T_(heapbuf_ensure_avail)(T_(HeapBuf) *hb, int n)
{
    if (hb->end - hb->cur < n) {
        int oldsize = hb->end - hb->start;
        int newsize = oldsize*2+n;
        T_(heapbuf_grow)(hb, newsize);
    }
}

static void
T_(heapbuf_putc)(T_(Buf) *b, _TCHAR c)
{
    T_(HeapBuf) *hb = (T_(HeapBuf) *) b;
    T_(heapbuf_ensure_avail)(hb, 1);
    *hb->cur++ = c;
}

static void
T_(heapbuf_puts)(T_(Buf) *b, const _TCHAR *s, int n)
{
    T_(HeapBuf) *hb = (T_(HeapBuf) *) b;
    T_(heapbuf_ensure_avail)(hb, n);
    memcpy(hb->cur, s, n * sizeof *s);
    hb->cur += n;
}

void
T_(init_heapbuf_size)(T_(HeapBuf) *hb, int size)
{
    _TCHAR *buf;
    assert(size >= 0);
    buf = xmalloc(INIT_BUFSIZE * sizeof *buf);
    hb->buf.putc = T_(heapbuf_putc);
    hb->buf.puts = T_(heapbuf_puts);
    hb->start = buf;
    hb->cur = buf;
    hb->end = buf + size;
}

void
T_(init_heapbuf)(T_(HeapBuf) *hb)
{
    T_(init_heapbuf_size)(hb, INIT_BUFSIZE);
}

_TCHAR *
T_(finish_heapbuf)(T_(HeapBuf) *hb)
{
    if (hb->cur == hb->end) {
        T_(heapbuf_grow)(hb, hb->cur - hb->start + 1);
    }
    *hb->cur = 0;
    return hb->start;
}

static void
T_(filebuf_putc)(T_(Buf) *b, _TCHAR c)
{
    T_(FileBuf) *fb = (T_(FileBuf) *) b;
    T_(fputc)(c, fb->fp);
}

static void
T_(filebuf_puts)(T_(Buf) *b, const _TCHAR *s, int n)
{
    while (n--) T_(filebuf_putc)(b, *s++);
}

void
T_(init_filebuf)(T_(FileBuf) *fb, FILE *fp)
{
    fb->buf.putc = T_(filebuf_putc);
    fb->buf.puts = T_(filebuf_puts);
    fb->fp = fp;
}

static void
T_(fixedbuf_putc)(T_(Buf) *b, _TCHAR c)
{
    T_(FixedBuf) *fb = (T_(FixedBuf) *) b;
    *fb->cur++ = c;
    *fb->cur = 0;
}

static void
T_(fixedbuf_puts)(T_(Buf) *b, const _TCHAR *s, int n)
{
    T_(FixedBuf) *fb = (T_(FixedBuf) *) b;
    memcpy(fb->cur, s, n * sizeof *s);
    fb->cur += n;
    *fb->cur = 0;
}

void
T_(init_fixedbuf)(T_(FixedBuf) *fb, _TCHAR *start)
{
    fb->buf.putc = T_(fixedbuf_putc);
    fb->buf.puts = T_(fixedbuf_puts);
    fb->cur = start;
}

static void
T_(regionbuf_grow)(T_(RegionBuf) *rb, int newsize)
{
    Region *r = rb->r;
    char *oldcur = r->cur;
    int oldlen = oldcur - (char *) rb->start; // this many bytes need to be copied
    new_chunk(r, newsize * sizeof(_TCHAR));
    /* move data in (rb->start -- r->cur) to new area */
    memcpy(r->cur, rb->start, oldlen);
    rb->start = (_TCHAR *) r->cur;
    r->cur += oldlen;
}

static void
T_(regionbuf_ensure_avail)(T_(RegionBuf) *rb, int n)
{
    Region *r = rb->r;
    if ((_TCHAR *) r->limit - (_TCHAR *) r->cur < n) {
        int oldsize = (_TCHAR *) r->limit - rb->start;
        int newsize = oldsize*2+n;
        T_(regionbuf_grow)(rb, newsize);
    }
}

static void
T_(regionbuf_putc)(T_(Buf) *b, _TCHAR c)
{
    T_(RegionBuf) *rb = (T_(RegionBuf) *) b;
    Region *r = rb->r;
    T_(regionbuf_ensure_avail)(rb, 1);
    *(_TCHAR*)r->cur = c;
    r->cur += sizeof(_TCHAR);
}

static void
T_(regionbuf_puts)(T_(Buf) *b, const _TCHAR *s, int n)
{
    T_(RegionBuf) *rb = (T_(RegionBuf) *) b;
    Region *r = rb->r;
    T_(regionbuf_ensure_avail)(rb, n);
    memcpy(r->cur, s, n * sizeof *s);
    r->cur += n * sizeof *s;
}

void
T_(init_regionbuf)(T_(RegionBuf) *rb, Region *r)
{
    rb->buf.putc = T_(regionbuf_putc);
    rb->buf.puts = T_(regionbuf_puts);
    rb->r = r;
    r->cur = (char *) ALIGN((uintptr_t) r->cur, sizeof(_TCHAR));
    rb->start = (_TCHAR *) r->cur;
}

_TCHAR *
T_(finish_regionbuf)(T_(RegionBuf) *rb)
{
    Region *r = rb->r;
    if (r->cur == r->limit) {
        T_(regionbuf_grow)(rb, (_TCHAR *) r->cur - rb->start + 1);
    }
    *(_TCHAR*)r->cur = 0;
    r->cur += sizeof(_TCHAR);
    return rb->start;
}

/* printf */

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

#define PUTNUM10(NAME, FIELD, TYPE)\
static _TCHAR *\
T_(NAME)(union u *value, _TCHAR *buf, uint f)\
{\
    TYPE v = value->FIELD;\
    uchar neg = (f & F_SIGNED) && v < 0;\
    _TCHAR *p = buf;\
    if (neg) v = -v;\
    if (v || !(f & F_HAVE_PREC)) do {\
        int d = (int)((unsigned TYPE) v % 10);\
        *p++ = '0'+d;\
        v = (unsigned TYPE) v / 10;\
    } while (v);\
    if (neg) *p++ = '-';\
    else if (f & F_PRINTSIGN) *p++ = f & F_PLUS_SIGN ? '+' : ' ';\
    return p;\
}

#define PUTNUM16(NAME, FIELD, TYPE)\
static _TCHAR *\
T_(NAME)(union u *value, _TCHAR *buf, uint f)\
{\
    TYPE v = value->FIELD;\
    uchar upcase = (f & F_UPPERCASE) != 0;\
    _TCHAR *p = buf;\
    if (v || !(f & F_HAVE_PREC)) do {\
        int d = (int) v & 15;\
        *p++ = d < 10 ? '0'+d : (upcase?'A':'a')+(d-10);\
        v = (unsigned TYPE) v >> 4;\
    } while (v);\
    return p;\
}

PUTNUM10(puti10, i, int)
PUTNUM10(putl10, l, long)
PUTNUM10(putL10, L, INT64_)
PUTNUM16(puti16, i, int)
PUTNUM16(putl16, l, long)
PUTNUM16(putL16, L, INT64_)

typedef _TCHAR *(*T_(conv_fn))(union u *, _TCHAR *, uint);

static void
T_(putn)(void *value, T_(Buf) *b, T_(conv_fn) conv, int prec, uint flags)
{
    _TCHAR buf[20]; /* NO TRAILING NUL */

    /* This builds the string back to front ... */
    int len = conv(value, buf, flags) - buf;
    int i;
    /* ... now we reverse it (could do it recursively but will
       conserve the stack space) */
    for (i = 0; i < len/2; i++) {
        _TCHAR tmp = buf[i];
        buf[i] = buf[len-i-1];
        buf[len-i-1] = tmp;
    }

    if (flags & F_HAVE_PREC) {
        for (i=len; i<prec; i++) b->putc(b, '0');
    }
    b->puts(b, buf, len);
}

void
T_(vbprintf)(T_(Buf) *b, const _TCHAR *fmt, va_list va)
{
    _TCHAR ch;

    while ((ch = *fmt++)) {
        uint flags;
        _TCHAR *s;
        int n;
        int prec;
        int l;
        T_(conv_fn) conv;
        union u val;
        Formatter f;
        void *d;

        if (ch!='%') {
            b->putc(b, ch);
            continue;
        }

        flags = 0;
        prec = 0;
        l = 0;

        ch = *fmt++;
flags:
        switch (ch) {
        case ' ':
            ch = *fmt++;
            flags |= F_PRINTSIGN;
            goto flags;
        case '+':
            ch = *fmt++;
            flags |= F_PRINTSIGN | F_PLUS_SIGN;
            goto flags;
        }

        /* precision */
        if (ch == '.') {
            ch = *fmt++;
            flags |= F_HAVE_PREC;
            if (ch == '*') {
                ch = *fmt++;
                prec = va_arg(va, int);
                if (prec < 0) prec = 0;
            } else {
                prec = 0;
                while (ch >= '0' && ch <= '9') {
                    prec = prec*10 + (ch - '0');
                    ch = *fmt++;
                }
            }
        }

spec:
        switch (ch) {
        case 0:
            return;

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
                conv = T_(puti10);
                val.i = va_arg(va, int);
                break;
            case 1:
                conv = T_(putl10);
                val.l = va_arg(va, long);
                break;
            default:
                conv = T_(putL10);
                val.L = va_arg(va, INT64_);
            }
            T_(putn)(&val, b, conv, prec, flags);
            break;

        case 'X':
            flags |= F_UPPERCASE;
            /* fallthrough */
        case 'x':
            switch (l) {
            case 0:
                conv = T_(puti16);
                val.i = va_arg(va, int);
                break;
            case 1:
                conv = T_(putl16);
                val.l = va_arg(va, long);
                break;
            default:
                conv = T_(putL16);
                val.L = va_arg(va, INT64_);
            }
            T_(putn)(&val, b, conv, prec, flags);
            break;

        case 'c':
            val.i = va_arg(va, int);
            b->putc(b, val.i);
            break;

        case 's':
            s = va_arg(va, _TCHAR *);
            if (flags & F_HAVE_PREC) {
                /* be careful not to read past (s+prec) */
                n = 0;
                while (n < prec && s[n]) n++;
            } else {
                n = T_(strlen)(s);
            }
            b->puts(b, s, n);
            break;

        case 'a':
            f = va_arg(va, Formatter);
            d = va_arg(va, void *);
            f(b, d);
            break;

        default:
            b->putc(b, ch);
        }
    }
}

void
T_(bprintf)(T_(Buf) *b, const _TCHAR *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    T_(vbprintf)(b, fmt, va);
    va_end(va);
}

void
T_(_vsprintf)(_TCHAR *s, const _TCHAR *fmt, va_list va)
{
    T_(FixedBuf) fb;
    T_(init_fixedbuf)(&fb, s);
    T_(vbprintf)(&fb.buf, fmt, va);
}

void
T_(_sprintf)(_TCHAR *buf, const _TCHAR *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    T_(_vsprintf)(buf, fmt, va);
    va_end(va);
}

_TCHAR *
T_(asprintf)(const _TCHAR *fmt, ...)
{
    T_(HeapBuf) hb;
    va_list va;
    T_(init_heapbuf)(&hb);
    va_start(va, fmt);
    T_(vbprintf)(&hb.buf, fmt, va);
    va_end(va);
    return T_(finish_heapbuf)(&hb);
}

_TCHAR *
T_(rsprintf)(Region *r, const _TCHAR *fmt, ...)
{
    T_(RegionBuf) rb;
    va_list va;
    T_(init_regionbuf)(&rb, r);
    va_start(va, fmt);
    T_(vbprintf)(&rb.buf, fmt, va);
    va_end(va);
    return T_(finish_regionbuf)(&rb);
}

void
T_(_vfprintf)(FILE *fp, const _TCHAR *fmt, va_list va)
{
    T_(FileBuf) fb;
    T_(init_filebuf)(&fb, fp);
    T_(vbprintf)(&fb.buf, fmt, va);
}

void
T_(_fprintf)(FILE *fp, const _TCHAR *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    T_(_vfprintf)(fp, fmt, va);
    va_end(va);
}

void
T_(_printf)(const _TCHAR *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    T_(_vfprintf)(stdout, fmt, va);
    va_end(va);
}

void
T_(eprintf)(const _TCHAR *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    T_(_vfprintf)(stderr, fmt, va);
    va_end(va);
}
