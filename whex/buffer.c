#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>

#include "types.h"
#include "region.h"
#include "util.h"
#include "list.h"
#include "buffer.h"
#include "buf.h"
#include "printf.h"

#define VALID 1

void format_error_code(TCHAR *, size_t, DWORD);

enum {
	SEG_FILE,
	SEG_MEM,
};

struct segment {
	struct segment *next;
	uchar kind;
	uint64 start;
	uint64 end;
	union {
		struct {
			uint64 file_offset;
			uchar *filedata;
		};
		uchar data[8];
	};
};

static int
seek(HANDLE file, uint64 offset)
{
	LONG lo = (LONG) offset;
	LONG hi = offset >> 32;
	if (SetFilePointer(file, lo, &hi, FILE_BEGIN) != lo) {
		_printf("seek(%llx) failed\n", offset);
		return -1;
	}
	return 0;
}

static int
find_cache(Buffer *b, uint64 addr)
{
	assert(addr >= 0 && addr < b->file_size);

	uint64 base = addr & -CACHE_BLOCK_SIZE;
	for (int i=0; i<N_CACHE_BLOCK; i++) {
		struct cache_entry *c = &b->cache[i];
		if ((c->flags & VALID) && base == c->addr) return i;
	}

	seek(b->file, base);
	DWORD nread;
	int ret = b->next_cache;
	ReadFile(b->file, b->cache[ret].data, CACHE_BLOCK_SIZE, &nread, 0);
	b->cache[ret].addr = base;
	b->cache[ret].flags = VALID;
	b->next_cache = (ret+1)&(N_CACHE_BLOCK-1);

	return ret;
}

static uchar *
get_file_data(Buffer *b, uint64 addr)
{
	int block = find_cache(b, addr);
	return &b->cache[block].data[addr & (CACHE_BLOCK_SIZE-1)];
}

static uchar
get_file_byte(Buffer *b, uint64 addr)
{
	return *get_file_data(b, addr);
}

uchar
buf_getbyte(Buffer *b, uint64 addr)
{
	assert(addr >= 0 && addr < b->buffer_size);
	Segment *s = b->firstseg;
	while (addr >= s->end)
		s = s->next;
	uint64 off = addr - s->start;
	assert(off >= 0 && off < s->end - s->start);
	switch (s->kind) {
	case SEG_FILE:
		return get_file_byte(b, s->file_offset+off);
	case SEG_MEM:
		return s->data[off];
	default:
		assert(0);
	}
	return 0; // placate compiler
}

static void
kmp_table(int *T, const uchar *pat, int len)
{
	int pos = 2;
	int cnd = 0;
	// forall i:nat, 0 < i < len ->
	// T[i] < i /\ ...
	// pat[i-T[i]:i] = pat[0:T[i]] /\ ...
	// forall j:nat, T[i] < j < len -> pat[i-j:i] <> pat[0:j]
	T[1] = 0; // T[0] is undefined
	while (pos < len) {
		if (pat[pos-1] == pat[cnd]) {
			T[pos++] = cnd+1;
			cnd++;
		} else {
			// pat[pos-1] != pat[cnd]
			if (cnd > 0) {
				cnd = T[cnd];
			} else {
				T[pos++] = 0;
			}
		}
	}
}

uint64
buf_kmp_search(Buffer *b, const uchar *pat, int len, uint64 start)
{
	int *T;
	uint64 m;
	int i;

	assert(len);
	Region *r = &b->tmp_rgn;
	void *top = r->cur;
	T = ralloc(r, len * sizeof *T);
	kmp_table(T, pat, len);
	m = start; // start of potential match
	i = 0;
	while (m+i < b->buffer_size) {
		if (pat[i] == buf_getbyte(b, m+i)) {
			if (i == len - 1) goto end; /* match found */
			i++;
		} else {
			// current character does not match
			if (i) {
				m += i-T[i];
				i = T[i];
			} else {
				m++;
			}
		}
	}
	/* no match */
	m = b->buffer_size;
end:
	rfree(r, top);
	return m;
}

/*
 * ...|xx|x..
 * position of first mark = start
 * number of bytes after the second mark = N-(start+len-1)
 */
uint64
buf_kmp_search_backward(Buffer *b, const uchar *pat, int len, uint64 start)
{
	int *T;
	uchar *revpat;
	uint64 m;
	int i;

	assert(len);
	Region *r = &b->tmp_rgn;
	void *top = r->cur;
	T = ralloc(r, len * sizeof *T);
	revpat = ralloc(r, len);

	for (i=0; i<len; i++) {
		revpat[i] = pat[len-1-i];
	}
	pat = revpat;
	kmp_table(T, pat, len);
	m = start;
	i = 0;
	while (m+i < b->buffer_size) {
		if (pat[i] == buf_getbyte(b, b->buffer_size-1-(m+i))) {
			if (i == len - 1) {
				m = b->buffer_size - (m + len);
				goto end;
			}
			i++;
		} else {
			if (i) {
				m += i-T[i];
				i = T[i];
			} else {
				m++;
			}
		}
	}
	/* no match */
	m = b->buffer_size;
end:
	rfree(r, top);
	return m;
}

int
buf_init(Buffer *b, HANDLE file)
{
	uchar *cache_data;
	struct cache_entry *cache;
	DWORD lo, hi;
	uint64 size;

	/* get file size */
	lo = GetFileSize(file, &hi);
	if (lo == 0xffffffff) {
		DWORD err = GetLastError();
		if (err) {
			TCHAR errmsg[512];
			format_error_code(errmsg, NELEM(errmsg), err);
			_tprintf(TEXT("%s\n"), errmsg);
			return -1;
		}
	}
	size = (uint64) lo | (uint64) hi << 32;
	if (size < 0) {
		puts("negative file size");
		return -1;
	}

	cache_data = malloc(N_CACHE_BLOCK << LOG2_CACHE_BLOCK_SIZE);
	if (!cache_data) {
nomem:
		puts("out of memory");
		return -1;
	}

	cache = malloc(N_CACHE_BLOCK * sizeof *b->cache);
	if (!cache) {
		free(cache_data);
		goto nomem;
	}

	for (int i=0; i<N_CACHE_BLOCK; i++) {
		cache[i].addr = 0;
		cache[i].flags = 0;
		cache[i].data = cache_data + (i << LOG2_CACHE_BLOCK_SIZE);
	}

	Segment *seg = calloc(1, sizeof *seg);
	seg->kind = SEG_FILE;
	seg->end = size;

	b->file = file;
	b->file_size = size;
	b->buffer_size = size;
	b->firstseg = seg;
	b->cache = cache;
	b->cache_data = cache_data;
	b->tree = 0;
	memset(&b->tree_rgn, 0, sizeof b->tree_rgn);
	b->next_cache = 0;

	return 0;
}

void
buf_finalize(Buffer *b)
{
	CloseHandle(b->file);
	b->file = INVALID_HANDLE_VALUE;
	Segment *s = b->firstseg;
	while (s) {
		Segment *next = s->next;
		free(s);
		s = next;
	}
	b->firstseg = 0;
	b->file_size = 0;
	b->buffer_size = 0;
	free(b->cache);
	b->cache = 0;
	free(b->cache_data);
	b->cache_data = 0;
	rfreeall(&b->tree_rgn);
	b->tree = 0;
}

int
buf_save(Buffer *b, HANDLE dstfile)
{
	uchar inplace = b->file == dstfile;
	Segment *s = b->firstseg;
	Region *r = &b->tmp_rgn;
	void *top = r->cur;
	while (s) {
		_printf("%llx--%llx ", s->start, s->end);
		switch (s->kind) {
		case SEG_MEM:
			_printf("MEM\n");
			break;
		case SEG_FILE:
			_printf("FILE offset=%llx\n", s->file_offset);
			if (inplace && s->start == s->file_offset) {
				s->filedata = 0;
			} else {
				uint64 full_len = s->end - s->start;
				DWORD len = (DWORD) full_len;
				if (len != full_len) {
fail:
					rfree(r, top);
					return -1;
				}
				s->filedata = ralloc(r, len);
				if (!s->filedata) {
					_printf("out of memory\n", len);
					goto fail;
				}
				seek(b->file, s->file_offset);
				DWORD nread;
				ReadFile(b->file, s->filedata, len, &nread, 0);
				if (nread != len) {
					_printf("short read (%lu/%lu)\n",
						nread, len);
					goto fail;
				}
			}
			break;
		default:
			assert(0);
		}
		s = s->next;
	}
	s = b->firstseg;
	while (s) {
		uint64 full_seglen = s->end - s->start;
		DWORD seglen = (DWORD) full_seglen;
		if (seglen != full_seglen) {
			return -1;
		}
		DWORD nwritten;
		switch (s->kind) {
		case SEG_MEM:
			seek(dstfile, s->start);
			WriteFile(dstfile, s->data, seglen,
				  &nwritten, 0);
			break;
		case SEG_FILE:
			if (!s->filedata) break;
			seek(dstfile, s->start);
			WriteFile(dstfile, s->filedata, seglen,
				  &nwritten, 0);
			free(s->filedata);
			s->filedata = 0;
			break;
		default:
			assert(0);
		}
		s = s->next;
	}
	if (inplace) {
		Segment *next;
		s = b->firstseg;
		while (s) {
			next = s->next;
			free(s);
			s = next;
		}
		s = calloc(1, sizeof *s);
		s->kind = SEG_FILE;
		s->end = b->buffer_size;
		b->file_size = b->buffer_size;
		b->firstseg = s;
	}
	return 0;
}

static Segment *
newmemseg(uint64 start, size_t len)
{
	size_t xsize = len > 8 ? len-8 : 8;
	Segment *newseg = calloc(1, sizeof *newseg + xsize);
	newseg->kind = SEG_MEM;
	newseg->start = start;
	newseg->end = start+len;
	return newseg;
}

void
buf_replace(Buffer *b, uint64 addr, const uchar *data, size_t len)
{
	assert(addr >= 0 && addr + len < b->buffer_size);
	assert(len > 0);
	Segment *before, *after;
	uint64 end = addr + len;
	before = b->firstseg;
	while (before->end < addr)
		before = before->next;
	after = before;
	while (after && after->end <= end)
		after = after->next;
	if (before == after) {
		switch (before->kind) {
		case SEG_MEM:
			{
				/* in-place modification */
				size_t off = (size_t)(addr - before->start);
				memcpy(before->data + off, data, len);
			}
			break;
		case SEG_FILE:
			{
				Segment *newseg = newmemseg(addr, len);
				memcpy(newseg->data, data, len);
				if (addr == 0) {
					after = before;
					b->firstseg = newseg;
				} else {
					after = malloc(sizeof *after);
					*after = *before;
					before->next = newseg;
					before->end = addr;
				}
				after->file_offset += end - before->start;
				after->start = end;
				newseg->next = after;
			}
			break;
		default:
			assert(0);
		}
	} else {
		/* before != after */
		Segment *tobefreed;
		Segment *newseg;
		if (after && after->start != end) {
			size_t newseglen;
			uint64 full_delta = end - after->start;
			size_t delta = (size_t) full_delta;
			switch (after->kind) {
			case SEG_MEM:
				/* coalesce */
				newseglen = (size_t)(after->end - addr);
				newseg = newmemseg(addr, newseglen);
				memcpy(newseg->data, data, len);
				memcpy(newseg->data+len,
					after->data + delta,
					newseglen - len);
				newseg->next = after->next;
				break;
			case SEG_FILE:
				newseg = newmemseg(addr, len);
				memcpy(newseg->data, data, len);
				newseg->next = after;
				after->file_offset += full_delta;
				break;
			default:
				assert(0);
				newseg = 0;
			}
			after->start = end;
		} else {
			newseg = newmemseg(addr, len);
			memcpy(newseg->data, data, len);
			newseg->next = after;
		}
		if (addr == 0) {
			tobefreed = before;
			b->firstseg = newseg;
		} else {
			tobefreed = before->next;
			before->end = addr;
			before->next = newseg;
		}
		/* free any segment in between */
		Segment *stop = newseg->next;
		while (tobefreed != stop) {
			Segment *next = tobefreed->next;
			free(tobefreed);
			tobefreed = next;
		}
	}
}

void
buf_insert(Buffer *b, uint64 addr, const uchar *data, size_t len)
{
	assert(addr >= 0 && addr <= b->buffer_size);
	assert(len > 0);
	Segment *after;
	Segment *newseg = newmemseg(addr, len);
	memcpy(newseg->data, data, len);
	if (addr == 0) {
		after = b->firstseg;
		b->firstseg = newseg;
	} else {
		Segment *before = b->firstseg;
		while (before->end < addr) before = before->next;
		before->next = newseg;
		if (before->end == addr) {
			after = before->next;
		} else {
			/* split 'before' at 'addr' */
			uint64 full_delta = addr - before->start;
			size_t delta = (size_t) full_delta;
			size_t rest;
			switch (before->kind) {
			case SEG_FILE:
				after = malloc(sizeof *after);
				*after = *before;
				/* start and end will be adjusted later */
				after->start = addr;
				after->file_offset += full_delta;
				break;
			case SEG_MEM:
				rest = len - delta;
				after = newmemseg(addr, rest);
				memcpy(after->data, before->data + delta, rest);
				break;
			default:
				assert(0);
				after = 0;
			}
			before->end = addr;
		}
	}
	newseg->next = after;
	for (Segment *s = after; s; s = s->next) {
		s->start += len;
		s->end += len;
	}
}

static void
buf_read_file(Buffer *b, uchar *dst, uint64 fileoff, size_t n)
{
	do {
		uchar *src = get_file_data(b, fileoff);
		size_t n1 = CACHE_BLOCK_SIZE - (fileoff & (CACHE_BLOCK_SIZE-1));
		if (n1 > n) n1 = n;
		memcpy(dst, src, n1);
		dst += n1;
		n -= n1;
		fileoff += n1;
	} while (n);
}

void
buf_read(Buffer *b, uchar *dst, uint64 addr, size_t n)
{
	assert(addr >= 0 && addr < b->buffer_size);
	Segment *s = b->firstseg;
	while (addr >= s->end)
		s = s->next;
	uint64 segoff = addr - s->start;
	assert(segoff >= 0 && segoff < s->end - s->start);
	for (;;) {
		size_t n1 = (size_t)(s->end - addr);
		if (n1 > n) n1 = n;
		switch (s->kind) {
		case SEG_FILE:
			buf_read_file(b, dst, s->file_offset + segoff, n1);
			break;
		case SEG_MEM:
			memcpy(dst, s->data + segoff, n1);
			break;
		default:
			assert(0);
		}
		dst += n1;
		n -= n1;
		if (n == 0) break;
		s = s->next;
		if (!s) break;
		addr = s->start;
		segoff = 0;
	}
}
