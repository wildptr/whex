#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "util.h"
#include "buffer.h"

#define VALID 1
#define DIRTY 2

static int
find_cache(Buffer *b, long long addr)
{
	assert(addr >= 0 && addr < b->file_size);

	long long base = addr & -CACHE_BLOCK_SIZE;
	for (int i=0; i<N_CACHE_BLOCK; i++) {
		struct cache_entry *c = &b->cache[i];
		if ((c->flags & VALID) && base == c->addr) return i;
	}

	SetFilePointer(b->file, base, 0, FILE_BEGIN);
	DWORD nread;
	int ret = b->next_cache;
	if (b->cache[ret].flags & DIRTY) {
		for (ret = 0; ret < N_CACHE_BLOCK; ret++) {
			if (!(b->cache[ret].flags & DIRTY)) break;
		}
		if (ret == N_CACHE_BLOCK) {
			/* TODO */
			abort();
		}
	}
	ReadFile(b->file, b->cache[ret].data, CACHE_BLOCK_SIZE, &nread, 0);
	b->cache[ret].addr = base;
	b->cache[ret].flags = VALID;
	b->next_cache = (ret+1)&(N_CACHE_BLOCK-1);

	DEBUG_PRINTF("loaded %I64x into cache block %d\n", base, ret);

	return ret;
}

uint8_t
buf_getbyte(Buffer *b, long long addr)
{
	return *buf_get_data(b, addr);
}

uint8_t *
buf_get_data(Buffer *b, long long addr)
{
	int block = find_cache(b, addr);
	return &b->cache[block].data[addr & (CACHE_BLOCK_SIZE-1)];
}

void
buf_setbyte(Buffer *b, long long addr, uint8_t val)
{
	int block = find_cache(b, addr);
	struct cache_entry *c = &b->cache[block];
	c->data[addr & (CACHE_BLOCK_SIZE-1)] = val;
	c->flags |= DIRTY;
}

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
	assert(len);
	int T[len];
	kmp_table(T, pat, len);
	long long m = start; // start of potential match
	int i = 0;
	while (m+i < b->file_size) {
		if (pat[i] == buf_getbyte(b, m+i)) {
			if (i == len-1) return m; // match found
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
	return b->file_size;
}

/*
 * ...|xx|x..
 * position of first mark = start
 * number of bytes after the second mark = N-(start+len-1)
 */
long long
buf_kmp_search_backward(Buffer *b, const uint8_t *pat, int len, long long start)
{
	assert(len);
	int T[len];
	uint8_t revpat[len];
	for (int i=0; i<len; i++) {
		revpat[i] = pat[len-1-i];
	}
	pat = revpat;
	kmp_table(T, pat, len);
	long long m = start;
	int i = 0;
	while (m+i < b->file_size) {
		if (pat[i] == buf_getbyte(b, b->file_size-1-(m+i))) {
			if (i == len-1) return b->file_size-(m+len);
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
	return b->file_size;
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
		if (err) return -1;
	}
	size = (long long) lo | (long long) hi << 32;
	if (size < 0) return -1;

	cache_data = malloc(N_CACHE_BLOCK << LOG2_CACHE_BLOCK_SIZE);
	if (!cache_data) {
		return -1;
	}

	cache = malloc(N_CACHE_BLOCK * sizeof *b->cache);
	if (!cache) {
		free(cache_data);
		return -1;
	}

	for (int i=0; i<N_CACHE_BLOCK; i++) {
		cache[i].addr = 0;
		cache[i].flags = 0;
		cache[i].data = cache_data + (i << LOG2_CACHE_BLOCK_SIZE);
	}

	b->file = file;
	b->file_size = size;
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
	b->file_size = 0;
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
	int ret = 0;
	for (int i=0; i<N_CACHE_BLOCK; i++) {
		struct cache_entry *c = &b->cache[i];
		if (c->flags & DIRTY) {
			DWORD nwritten;
			LONG addrlo = c->addr;
			LONG addrhi = c->addr >> 32;
			if (SetFilePointer(b->file, addrlo, &addrhi,
					   FILE_BEGIN) != addrlo)
				return -1;
			WriteFile(b->file, c->data, CACHE_BLOCK_SIZE,
				  &nwritten, 0);
			/* TODO: show message if failed */
			if (nwritten == CACHE_BLOCK_SIZE) {
				c->flags &= ~DIRTY;
			} else {
				ret = -1;
			}
		}
	}
	return ret;
}
