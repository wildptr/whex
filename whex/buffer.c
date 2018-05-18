#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "util.h"
#include "buffer.h"

#define VALID 1

void format_error_code(TCHAR *, size_t, DWORD);

enum {
	SEG_FILE,
	SEG_MEM,
};

struct segment {
	struct segment *next;
	uint8_t kind;
	long long start;
	long long end;
	union {
		long long file_offset;
		uint8_t *filedata;
		uint8_t data[8];
	};
};

static int
seek(HANDLE file, long long offset)
{
	long lo = (long) offset;
	long hi = offset >> 32;
	if (SetFilePointer(file, lo, &hi, FILE_BEGIN) != lo) {
		printf("seek(%I64x) failed\n", offset);
		return -1;
	}
	return 0;
}

static int
find_cache(Buffer *b, long long addr)
{
	assert(addr >= 0 && addr < b->file_size);

	long long base = addr & -CACHE_BLOCK_SIZE;
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

	DEBUG_PRINTF("loaded %I64x into cache block %d\n", base, ret);

	return ret;
}

static uint8_t *
get_file_data(Buffer *b, long long addr)
{
	int block = find_cache(b, addr);
	return &b->cache[block].data[addr & (CACHE_BLOCK_SIZE-1)];
}

static uint8_t
get_file_byte(Buffer *b, long long addr)
{
	return *get_file_data(b, addr);
}

uint8_t
buf_getbyte(Buffer *b, long long addr)
{
	assert(addr >= 0 && addr < b->buffer_size);
	Segment *s = b->firstseg;
	while (addr >= s->end)
		s = s->next;
	long long off = addr - s->start;
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

#if 0
void
buf_setbyte(Buffer *b, long long addr, uint8_t val)
{
	int block = find_cache(b, addr);
	struct cache_entry *c = &b->cache[block];
	c->data[addr & (CACHE_BLOCK_SIZE-1)] = val;
	c->flags |= DIRTY;
}
#endif

static void
kmp_table(int *T, const uint8_t *pat, int len)
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

long long
buf_kmp_search(Buffer *b, const uint8_t *pat, int len, long long start)
{
	int *T;
	long long m;
	int i;

	assert(len);
	T = malloc(len * sizeof *T);
	kmp_table(T, pat, len);
	m = start; // start of potential match
	i = 0;
	while (m+i < b->buffer_size) {
		if (pat[i] == buf_getbyte(b, m+i)) {
			if (i == len - 1) {
				free(T);
				return m; // match found
			}
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
	free(T);
	return b->buffer_size;
}

/*
 * ...|xx|x..
 * position of first mark = start
 * number of bytes after the second mark = N-(start+len-1)
 */
long long
buf_kmp_search_backward(Buffer *b, const uint8_t *pat, int len, long long start)
{
	int *T;
	uint8_t *revpat;
	long long m;
	int i;

	assert(len);
	T = malloc(len * sizeof *T);
	revpat = malloc(len);
	
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
				free(T);
				free(revpat);
				return b->buffer_size - (m + len);
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
	free(T);
	free(revpat);
	return b->buffer_size;
}

int
buf_init(Buffer *b, HANDLE file)
{
	uint8_t *cache_data;
	struct cache_entry *cache;
	DWORD lo, hi;
	long long size;

	/* get file size */
	lo = GetFileSize(file, &hi);
	if (lo == 0xffffffff) {
		DWORD err = GetLastError();
		if (err) {
			TCHAR errmsg[512];
			format_error_code(errmsg, NELEM(errmsg), err);
			puts(errmsg);
			return -1;
		}
	}
	size = (long long) lo | (long long) hi << 32;
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
buf_save(Buffer *b)
{
	Segment *s = b->firstseg;
	while (s) {
		printf("%I64x--%I64x ", s->start, s->end);
		switch (s->kind) {
			long long file_offset;
		case SEG_MEM:
			printf("MEM\n");
			break;
		case SEG_FILE:
			file_offset = s->file_offset;
			printf("FILE offset=%I64x\n", file_offset);
			if (s->start == file_offset) {
				s->filedata = 0;
			} else {
				long len = (long)(s->end - s->start);
				s->filedata = malloc(len);
				/* TODO: cleanup on failure */
				if (!s->filedata) {
					printf("malloc(%lu) failed\n", len);
					return -1;
				}
				seek(b->file, file_offset);
				DWORD nread;
				ReadFile(b->file, s->filedata, len, &nread, 0);
				if (nread != len) {
					printf("short read (%lu/%lu)\n",
					       nread, len);
					return -1;
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
		long seglen = (long)(s->end - s->start);
		switch (s->kind) {
			DWORD nwritten;
		case SEG_MEM:
			seek(b->file, s->start);
			WriteFile(b->file, s->data, seglen,
				  &nwritten, 0);
			break;
		case SEG_FILE:
			if (!s->filedata) break;
			seek(b->file, s->start);
			WriteFile(b->file, s->filedata, seglen,
				  &nwritten, 0);
			free(s->filedata);
			break;
		default:
			assert(0);
		}
		Segment *next = s->next;
		free(s);
		s = next;
	}
	s = calloc(1, sizeof *s);
	s->kind = SEG_FILE;
	s->end = b->buffer_size;
	b->file_size = b->buffer_size;
	b->firstseg = s;
	return 0;
}

static Segment *
newmemseg(long long start, long len)
{
	int xsize = len > 8 ? len-8 : 8;
	Segment *newseg = calloc(1, sizeof *newseg + xsize);
	newseg->kind = SEG_MEM;
	newseg->start = start;
	newseg->end = start+len;
	return newseg;
}

void
buf_replace(Buffer *b, long long addr, const uint8_t *data, long len)
{
	assert(addr >= 0 && addr < b->buffer_size);
	assert(len > 0);
	Segment *before, *after;
	long long end = addr + len;
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
				long off = (long)(addr - before->start);
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
				}
				after->start = end;
				after->file_offset += end - before->start;
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
			long newseglen;
			long delta;
			switch (after->kind) {
			case SEG_MEM:
				/* coalesce */
				newseglen = (long)(after->end - addr);
				newseg = newmemseg(addr, newseglen);
				delta = (long)(addr - after->start) + len;
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
				break;
			default:
				assert(0);
				newseg = 0;
			}
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
buf_insert(Buffer *b, long long addr, const uint8_t *data, long len)
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
			long delta = (long)(addr - before->start);
			switch (before->kind) {
				long rest;
			case SEG_FILE:
				after = malloc(sizeof *after);
				*after = *before;
				/* start and end will be adjusted later */
				after->start = addr;
				after->file_offset += delta;
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
buf_read_file(Buffer *b, uint8_t *dst, long long fileoff, long n)
{
	do {
		uint8_t *src = get_file_data(b, fileoff);
		long n1 = CACHE_BLOCK_SIZE - (fileoff & (CACHE_BLOCK_SIZE-1));
		if (n1 > n) n1 = n;
		memcpy(dst, src, n1);
		dst += n1;
		n -= n1;
		fileoff += n1;
	} while (n);
}

void
buf_read(Buffer *b, uint8_t *dst, long long addr, long n)
{
	assert(addr >= 0 && addr < b->buffer_size);
	Segment *s = b->firstseg;
	while (addr >= s->end)
		s = s->next;
	long long segoff = addr - s->start;
	assert(segoff >= 0 && segoff < s->end - s->start);
	for (;;) {
		long n1 = (long)(s->end - addr);
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
