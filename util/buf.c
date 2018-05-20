#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>

#include "types.h"
#include "buf.h"
#include "filebuf.h"

#define INIT_BUFSIZE 32

static int
heapbuf_grow(HeapBuf *hb, int newsize)
{
	TCHAR *newbuf = realloc(hb->start, newsize * sizeof *newbuf);
	if (!newbuf) return -1;
	int len = hb->cur - hb->start;
	hb->start = newbuf;
	hb->cur = newbuf + len;
	hb->end = newbuf + newsize;
	return 0;
}

static int
heapbuf_ensure_avail(HeapBuf *hb, int n)
{
	if (hb->end - hb->cur < n) {
		int oldsize = hb->end - hb->start;
		int newsize = oldsize*2+n;
		if (heapbuf_grow(hb, newsize)) return -1;
	}
	return 0;
}

static int
heapbuf_putc(Buf *b, TCHAR c)
{
	HeapBuf *hb = (HeapBuf *) b;
	if (heapbuf_ensure_avail(hb, 2)) return 0;
	*hb->cur++ = c;
	*hb->cur = 0;
	return 1;
}

static int
heapbuf_puts(Buf *b, const TCHAR *s, int n)
{
	HeapBuf *hb = (HeapBuf *) b;
	if (heapbuf_ensure_avail(hb, n+1)) return 0;
	memcpy(hb->cur, s, n*sizeof(TCHAR));
	hb->cur += n;
	*hb->cur = 0;
	return n;
}

int
init_heapbuf(HeapBuf *hb)
{
	TCHAR *buf = malloc(INIT_BUFSIZE * sizeof *hb);
	if (!buf) return -1;
	*buf = 0;
	hb->buf.putc = heapbuf_putc;
	hb->buf.puts = heapbuf_puts;
	hb->start = buf;
	hb->cur = buf;
	hb->end = buf + INIT_BUFSIZE;
	return 0;
}

static int
filebuf_putc(Buf *b, TCHAR c)
{
	return fputc(c, ((FileBuf *) b)->fp) == c;
}

static int
filebuf_puts(Buf *b, const TCHAR *s, int n)
{
	int ret = 0;
	while (n--) {
		if (!filebuf_putc(b, *s++)) break;
		ret++;
	}
	return ret;
}

void
init_filebuf(FileBuf *fb, FILE *fp)
{
	fb->buf.putc = filebuf_putc;
	fb->buf.puts = filebuf_puts;
	fb->fp = fp;
}

static int
fixedbuf_putc(Buf *b, TCHAR c)
{
	FixedBuf *fb = (FixedBuf *) b;
	*fb->cur++ = c;
	*fb->cur = 0;
	return 1;
}

static int
fixedbuf_puts(Buf *b, const TCHAR *s, int n)
{
	FixedBuf *fb = (FixedBuf *) b;
	memcpy(fb->cur, s, n*sizeof(TCHAR));
	fb->cur += n;
	*fb->cur = 0;
	return n;
}

void
init_fixedbuf(FixedBuf *fb, TCHAR *start)
{
	fb->buf.putc = fixedbuf_putc;
	fb->buf.puts = fixedbuf_puts;
	fb->cur = start;
}
