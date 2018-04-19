#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "util.h"
#include "whex.h"

#if 0
bool
whex_cache_valid(Whex *w, int block)
{
	return w->cache[block].tag & 1;
};
#endif

int
whex_find_cache(Whex *w, long long addr)
{
	assert(addr >= 0 && addr < w->file_size);

	static int next_cache = 0;

	for (int i=0; i<N_CACHE_BLOCK; i++) {
		long long tag = w->cache[i].tag;
		if ((tag & 1) && addr >> 12 == tag >> 12) return i;
	}

	long long base = addr & -CACHE_BLOCK_SIZE;
	long long tag = base|1;
	SetFilePointer(w->file, base, 0, FILE_BEGIN);
	DWORD nread;
	int ret = next_cache;
	ReadFile(w->file, w->cache[ret].data, CACHE_BLOCK_SIZE, &nread, 0);
	w->cache[ret].tag = tag;
	next_cache = (ret+1)&(N_CACHE_BLOCK-1);

	DEBUG_PRINTF("loaded %I64x into cache block %d\n", base, ret);

	return ret;
}

uint8_t
whex_getbyte(Whex *w, long long addr)
{
	int block = whex_find_cache(w, addr);
	return w->cache[block].data[addr & (CACHE_BLOCK_SIZE-1)];
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
whex_kmp_search(Whex *w, const uint8_t *pat, int len, long long start)
{
	assert(len);
	int T[len];
	kmp_table(T, pat, len);
	long long m = start; // start of potential match
	int i = 0;
	while (m+i < w->file_size) {
		if (pat[i] == whex_getbyte(w, m+i)) {
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
	return w->file_size;
}

/*
 * ...|xx|x..
 * position of first mark = start
 * number of bytes after the second mark = N-(start+len-1)
 */
long long
whex_kmp_search_backward(Whex *w, const uint8_t *pat, int len, long long start)
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
	while (m+i < w->file_size) {
		if (pat[i] == whex_getbyte(w, w->file_size-1-(m+i))) {
			if (i == len-1) return w->file_size-(m+len);
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
	return w->file_size;
}

int
whex_init(Whex *w, HANDLE file)
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

	cache = malloc(N_CACHE_BLOCK * sizeof *w->cache);
	if (!cache) {
		free(cache_data);
		return -1;
	}

	for (int i=0; i<N_CACHE_BLOCK; i++) {
		cache[i].tag = 0;
		cache[i].data = cache_data + (i << LOG2_CACHE_BLOCK_SIZE);
	}

	w->file = file;
	w->file_size = size;
	w->cache = cache;
	w->cache_data = cache_data;
	w->tree = 0;
	memset(&w->tree_rgn, 0, sizeof w->tree_rgn);

	return 0;
}

void
whex_finalize(Whex *w)
{
	CloseHandle(w->file);
	w->file = INVALID_HANDLE_VALUE;
	w->file_size = 0;
	free(w->cache);
	w->cache = 0;
	free(w->cache_data);
	w->cache_data = 0;
	rfreeall(&w->tree_rgn);
	w->tree = 0;
}
