#include "u.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>

#include "buffer.h"
#include "winutil.h"

#define N_CACHE_BLOCK 16
#define LOG2_CACHE_BLOCK_SIZE 16
#define CACHE_BLOCK_SIZE (1 << LOG2_CACHE_BLOCK_SIZE)
#define VALID 1

enum {
    SENTINEL, // must be 0
    BRANCH,
    SEG_ZERO,
    SEG_FILE,
    SEG_MEM,
};

typedef struct segment {
    struct segment *next, *prev;
    uchar kind;
    uint64 len;
    union {
        /* SEG_FILE */
        struct {
            uint64 offset;
            uchar *data;
        } file;
        /* SEG_MEM */
        struct {
            size_t offset;
            size_t cap;
            uchar data[];
        } mem;
    };
} Segment;

typedef struct rope {
    struct rope *left, *right; // both non-null
    uchar kind; // = BRANCH
    uint64 len;
} Rope;

#define ROPE(x) ((Rope*)(x))
#define SEGMENT(x) ((Segment*)(x))

struct cache_entry {
    uint64 addr;
    uchar *data;
    uchar flags;
};

struct buffer {
    HANDLE file;
    uint64 file_size;
    uint64 buffer_size;
    Rope *rope; // non-null unless buffer is empty
    struct cache_entry *cache;
    uchar *cache_data;
    Region tmp;
    struct {
        struct segment *first, *last; // both non-null
        uchar kind; // = SENTINEL
    } sentinel;
    int next_cache;
};

const int sizeof_Buffer = sizeof(Buffer);

static int
seek(HANDLE file, uint64 offset)
{
    LONG lo = (LONG) offset;
    LONG hi = (LONG)(offset >> 32);
    if (SetFilePointer(file, lo, &hi, FILE_BEGIN) != (DWORD) lo) {
        eprintf("seek(%llx) failed\n", offset);
        return -1;
    }
    return 0;
}

static int
find_cache_opt(Buffer *b, uint64 addr)
{
    assert(addr >= 0 && addr < b->file_size);

    uint64 base = addr & -CACHE_BLOCK_SIZE;
    for (int i=0; i<N_CACHE_BLOCK; i++) {
        struct cache_entry *c = &b->cache[i];
        if ((c->flags & VALID) && base == c->addr) return i;
    }

    return -1;
}

static int
find_cache(Buffer *b, uint64 addr)
{
    int ret = find_cache_opt(b, addr);
    if (ret >= 0) return ret;

    uint64 base = addr & -CACHE_BLOCK_SIZE;
    DWORD nread;

    seek(b->file, base);
    ret = b->next_cache;
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

static Segment *
find_segment(Rope *r, uint64 offset, uint64 *psegoff)
{
start:
    assert(r);
    if (r->kind == BRANCH) {
        if (offset < r->left->len) {
            r = r->left;
            goto start;
        }
        // offset >= r->left->len
        offset -= r->left->len;
        r = r->right;
        goto start;
    }
    // r->kind != BRANCH
    Segment *s = (Segment *) r;
    assert(s->kind);
#if 0
    if (offset >= s->len) {
        eprintf("%llu %llu\n",offset,s->len);
        abort();
    }
#endif
    assert(offset < s->len);
    *psegoff = offset;
    return s;
}

uchar
buf_getbyte(Buffer *b, uint64 addr)
{
    if (addr >= b->buffer_size) {
        eprintf("buf_getbyte: address %llu out of range (%llu)\n",
                addr, b->buffer_size);
        return 0;
    }

    uint64 segoff; // offset within segment
    Segment *s = find_segment(b->rope, addr, &segoff);
    switch (s->kind) {
    case SEG_ZERO:
        return 0;
    case SEG_FILE:
        return get_file_byte(b, s->file.offset + segoff);
    case SEG_MEM:
        return s->mem.data[s->mem.offset + segoff];
    }
    // should not reach here
    return 0;
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

int
buf_kmp_search(Buffer *b, const uchar *pat, int len, uint64 start, uint64 *pos)
{
    int *T;
    uint64 m;
    int i;
    int ret;

    assert(len);
    T = xmalloc(len * sizeof *T);
    kmp_table(T, pat, len);
    m = start; // start of potential match
    i = 0;
    while (m+i < b->buffer_size) {
        if (pat[i] == buf_getbyte(b, m+i)) {
            if (i == len - 1) {
                *pos = m;
                ret = 0;
                goto end; /* match found */
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
    ret = -1;
end:
    free(T);
    return ret;
}

static void
link(Segment *s, Segment *prev, Segment *next)
{
    s->prev = prev;
    prev->next = s;
    s->next = next;
    next->prev = s;
}

static Segment *new_file_seg(uint64 len, uint64 offset);
static Segment *new_mem_seg(uint64 len);

int
buf_load_file(Buffer *b, HANDLE file, uint slurp_thresh)
{
    /* get file size */
    uint64 size;
    DWORD err = get_file_size(file, &size);
    if (err) {
        TCHAR errmsg[512];
        format_error_code(errmsg, NELEM(errmsg), err);
        T(fputs)(errmsg, stderr);
        return -1;
    }

    Segment *s;
    if (size <= slurp_thresh) {
        if (size) {
            DWORD nread;
            uint sizelo = size;
            s = new_mem_seg(size);
            ReadFile(file, s->mem.data, sizelo, &nread, 0);
            if (nread < sizelo) {
                if (nread) {
                    s->len = nread;
                } else {
                    free(s);
                    s = 0;
                }
                size = nread;
                eprintf("short read (%lu/%lu)\n", nread, sizelo);
            }
        } else {
            s = 0;
        }
        CloseHandle(file);
    } else {
        s = new_file_seg(size, 0);
        b->file = file;
        b->file_size = size;
    }

    if (s) link(s, SEGMENT(&b->sentinel), SEGMENT(&b->sentinel));
    b->rope = ROPE(s);
    b->buffer_size = size;

    return 0;
}

int
buf_init(Buffer *b)
{
    uchar *cache_data;
    struct cache_entry *cache;

    cache_data = malloc(N_CACHE_BLOCK << LOG2_CACHE_BLOCK_SIZE);
    if (!cache_data) {
nomem:
        fputs("out of memory\n", stderr);
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

    b->sentinel.first = SEGMENT(&b->sentinel);
    b->sentinel.last = SEGMENT(&b->sentinel);
    b->rope = 0;

    b->file = INVALID_HANDLE_VALUE;
    b->file_size = 0;
    b->buffer_size = 0;
    b->sentinel.kind = 0;
    b->cache = cache;
    b->cache_data = cache_data;
    b->next_cache = 0;
    rinit(&b->tmp);

    return 0;
}

static void
free_node(Rope *r)
{
    if (r) {
        if (r->kind == BRANCH) {
            free_node(r->left);
            free_node(r->right);
        }
        free(r);
    }
}

struct link {
    Segment *prev, *next;
};

static struct link
delete_node(Rope *r)
{
    assert(r);
    struct link ret;
    if (r->kind == BRANCH) {
        struct link l1 = delete_node(r->left);
        struct link l2 = delete_node(r->right);
        ret.prev = l1.prev;
        ret.next = l2.next;
    } else {
        Segment *s = (Segment *) r;
        s->next->prev = s->prev;
        s->prev->next = s->next;
        ret.prev = s->prev;
        ret.next = s->next;
    }
    free(r);
    return ret;
}

void
buf_finalize(Buffer *b)
{
    CloseHandle(b->file);
    b->file = INVALID_HANDLE_VALUE;
    free_node(b->rope);
    b->sentinel.first = SEGMENT(&b->sentinel);
    b->sentinel.last = SEGMENT(&b->sentinel);
    b->rope = 0;
    b->file_size = 0;
    b->buffer_size = 0;
    free(b->cache);
    b->cache = 0;
    free(b->cache_data);
    b->cache_data = 0;
    rfreeall(&b->tmp);
}

int
buf_save(Buffer *b, HANDLE dstfile)
{
    static uchar zero[4096];

    uchar inplace;
    Segment *s;
    Region *r;
    void *top;

    assert(dstfile);

    inplace = b->file == dstfile;
    s = b->sentinel.first;
    r = &b->tmp;
    top = r->cur;
    uint64 segstart = 0;
    while (s->kind) {
        //_printf("%llx--%llx ", s->start, s->end);
        switch (s->kind) {
        case SEG_ZERO:
            //_printf("ZERO\n");
            break;
        case SEG_MEM:
            //_printf("MEM\n");
            break;
        case SEG_FILE:
            eprintf("FILE offset=%llx\n", s->file.offset);
            if (inplace && segstart == s->file.offset) {
                s->file.data = 0;
            } else {
                uint64 full_len = s->len;
                DWORD len = (DWORD) full_len;
                DWORD nread;
                if (len != full_len) {
fail:
                    rfree(r, top);
                    fputs("segment too large\n", stderr);
                    return -1;
                }
                s->file.data = ralloc(r, len);
                if (!s->file.data) {
                    eprintf("out of memory\n", len);
                    goto fail;
                }
                seek(b->file, s->file.offset);
                ReadFile(b->file, s->file.data, len, &nread, 0);
                if (nread != len) {
                    eprintf("short read (%lu/%lu)\n",
                            nread, len);
                    goto fail;
                }
            }
            break;
        default:
            assert(0);
        }
        segstart += s->len;
        s = s->next;
    }
    s = b->sentinel.first;
    segstart = 0;
    while (s->kind) {
        uint64 full_seglen = s->len;
        DWORD seglen = (DWORD) full_seglen;
        DWORD nwritten;
        DWORD remain;
        if (seglen != full_seglen) {
            fputs("segment too large\n", stderr);
            return -1;
        }
        switch (s->kind) {
        case SEG_ZERO:
            seek(dstfile, segstart);
            remain = seglen;
            while (remain) {
                DWORD n = remain;
                if (n > sizeof zero) n = sizeof zero;
                WriteFile(dstfile, zero, n, &nwritten, 0);
                remain -= n;
            }
            break;
        case SEG_MEM:
            seek(dstfile, segstart);
            WriteFile(dstfile, s->mem.data + s->mem.offset, seglen, &nwritten, 0);
            break;
        case SEG_FILE:
            if (!s->file.data) break;
            seek(dstfile, segstart);
            WriteFile(dstfile, s->file.data, seglen, &nwritten, 0);
            s->file.data = 0;
            break;
        default:
            assert(0);
        }
        segstart += s->len;
        s = s->next;
    }
    if (inplace) {
        free_node(b->rope);
        s = calloc(1, sizeof *s);
        s->kind = SEG_FILE;
        s->len = b->buffer_size;
        b->file_size = b->buffer_size;
        b->sentinel.first = s;
        b->sentinel.last = s;
        b->rope = ROPE(s);
    }
    rfree(r, top);
    return 0;
}

int
buf_save_in_place(Buffer *b)
{
    return buf_save(b, b->file);
}

static Segment *
new_mem_seg(uint64 len)
{
    assert(len);
    size_t cap = len+15&-16;
    Segment *s = calloc(1, offsetof(Segment, mem.data[cap]));
    s->kind = SEG_MEM;
    s->len = len;
    s->mem.cap = cap;
    return s;
}

static Segment *
new_zero_seg(uint64 len)
{
    assert(len);
    Segment *s = calloc(1, sizeof *s);
    s->kind = SEG_ZERO;
    s->len = len;
    return s;
}

static Segment *
new_file_seg(uint64 len, uint64 offset)
{
    assert(len);
    Segment *s = calloc(1, sizeof *s);
    s->kind = SEG_FILE;
    s->len = len;
    s->file.offset = offset;
    return s;
}

static Rope *
make_branch(Rope *a, Rope *b)
{
    assert(a);
    assert(b);
    Rope *r = xmalloc(sizeof *r);
    r->kind = BRANCH;
    r->left = a;
    r->right = b;
    r->len = a->len + b->len;
    return r;
}

// len <= r->len
static Rope *
rope_replace(Rope *r, uint64 offset, const uchar *data, uint64 len)
{
    assert(r);
    assert(len);

    switch (r->kind) {
    case SEG_ZERO:
        if (!data) return r;
        goto generic;
    case SEG_FILE:
generic:
        {
            Segment *s = (Segment *) r;
            Segment *newseg;
            if (data) {
                newseg = new_mem_seg(len);
                memcpy(newseg->mem.data, data, len);
            } else {
                newseg = new_zero_seg(len);
            }
            uint64 seglen = s->len;
            if (offset == 0) {
                // prefix of s gets replaced
                if (len < seglen) {
                    s->len = seglen - len;
                    switch (r->kind) {
                    case SEG_ZERO:
                        break;
                    case SEG_FILE:
                        s->file.offset += len;
                        break;
                    case SEG_MEM:
                        s->mem.offset += len;
                        break;
                    default:
                        assert(0);
                    }
                    link(newseg, s->prev, s);
                    r = make_branch(ROPE(newseg), r);
                } else {
                    link(newseg, s->prev, s->next);
                    free(s);
                    r = ROPE(newseg);
                }
            } else if (offset + len == seglen) {
                // suffix of s gets replaced
                s->len = offset;
                link(newseg, s, s->next);
                r = make_branch(r, ROPE(newseg));
            } else {
                Segment *right;
                s->len = offset;
                uint64 right_len = seglen - (offset+len);
                switch (r->kind) {
                case SEG_ZERO:
                    right = new_zero_seg(right_len);
                    break;
                case SEG_FILE:
                    right = new_file_seg(right_len, s->file.offset + (offset+len));
                    break;
                case SEG_MEM:
                    right = new_mem_seg(right_len);
                    memcpy(right->mem.data, s->mem.data + s->mem.offset + (offset+len), right_len);
                    break;
                default:
                    assert(0);
                }
                right->next = s->next;
                s->next->prev = right;
                link(newseg, s, right);
                r = make_branch(ROPE(s), make_branch(ROPE(newseg), ROPE(right)));
            }
        }
        return r;
    case SEG_MEM:
        {
            Segment *s = (Segment *) r;
            assert(len <= s->len);
            if (data) {
                memcpy(s->mem.data + s->mem.offset + offset, data, len);
            } else {
                memset(s->mem.data + s->mem.offset + offset, 0, len);
            }
        }
        return r;
    case BRANCH:
        if (offset < r->left->len) {
            // left child affected
            if (offset + len > r->left->len) {
                // right child affected
                if (offset == 0 && len == r->len) {
                    // entire rope is being replaced
                    struct link link = delete_node(r);
                    Segment *s;
                    if (data) {
                        s = new_mem_seg(len);
                        memcpy(s->mem.data, data, len);
                    } else {
                        s = new_zero_seg(len);
                    }
                    s->prev = link.prev;
                    link.prev->next = s;
                    s->next = link.next;
                    link.next->prev = s;
                    r = ROPE(s);
                } else {
                    uintptr_t l = r->left->len - offset;
                    r->left = rope_replace(r->left, offset, data, l);
                    r->right = rope_replace(r->right, 0, data+l, len-l);
                }
            } else {
                // right child unaffected
                r->left = rope_replace(r->left, offset, data, len);
            }
        } else {
            // left child unaffected, right child affected
            r->right = rope_replace
                (r->right, offset - r->left->len, data, len);
        }
        return r;
    }
    assert(0);
}

// len <= r->len
static Rope *
rope_delete(Rope *r, uint64 offset, uint64 len)
{
    assert(r);
    assert(len);

    switch (r->kind) {
    case SEG_ZERO:
        if (r->len > len) {
            r->len -= len;
        } else {
            assert(r->len == len);
            Segment *s = (Segment *) r;
            s->prev->next = s->next;
            s->next->prev = s->prev;
            r = 0;
        }
        return r;
    case SEG_FILE:
    case SEG_MEM:
        {
            Segment *s = (Segment *) r;
            uint64 seglen = s->len;
            if (offset == 0) {
                // prefix of s gets deleted
                if (len < seglen) {
                    s->len = seglen - len;
                    switch (r->kind) {
                    case SEG_ZERO:
                        break;
                    case SEG_FILE:
                        s->file.offset += len;
                        break;
                    case SEG_MEM:
                        s->mem.offset += len;
                        break;
                    default:
                        assert(0);
                    }
                } else {
                    s->prev->next = s->next;
                    s->next->prev = s->prev;
                    r = 0;
                }
            } else if (offset + len == seglen) {
                // suffix of s gets replaced
                s->len = offset;
            } else {
                Segment *right;
                s->len = offset;
                uint64 right_len = seglen - (offset+len);
                switch (r->kind) {
                case SEG_ZERO:
                    right = new_zero_seg(right_len);
                    break;
                case SEG_FILE:
                    right = new_file_seg(right_len, s->file.offset + (offset+len));
                    break;
                case SEG_MEM:
                    right = new_mem_seg(right_len);
                    memcpy(right->mem.data, s->mem.data + s->mem.offset + (offset+len), right_len);
                    break;
                default:
                    assert(0);
                }
                link(right, s, s->next);
                r = make_branch(ROPE(s), ROPE(right));
            }
        }
        return r;
    case BRANCH:
        if (offset < r->left->len) {
            // left child affected
            if (offset + len > r->left->len) {
                // right child affected
                if (offset == 0 && len == r->len) {
                    // entire rope is being deleted
                    delete_node(r);
                    r = 0;
                } else {
                    uintptr_t l = r->left->len - offset;
                    r->left = rope_delete(r->left, offset, l);
                    r->right = rope_delete(r->right, 0, len-l);
                    Rope *oldr = r;
                    if (r->left) {
                        if (r->right) {
                            r->len -= len;
                        } else {
                            r = r->left;
                            free(oldr);
                        }
                    } else {
                        // !r->left
                        if (r->right) {
                            r = r->right;
                        } else {
                            // !r->right
                            r = 0;
                        }
                        free(oldr);
                    }
                }
            } else {
                // right child unaffected
                r->left = rope_delete(r->left, offset, len);
                if (r->left) {
                    r->len -= len;
                } else {
                    Rope *oldr = r;
                    r = r->right;
                    free(oldr);
                }
            }
        } else {
            // left child unaffected, right child affected
            r->right = rope_delete(r->right, offset - r->left->len, len);
            if (r->right) {
                r->len -= len;
            } else {
                Rope *oldr = r;
                r = r->left;
                free(oldr);
            }
        }
        return r;
    }
    assert(0);
}

static Rope *
rope_insert(Rope *r, uint64 offset, const uchar *data, uint64 len)
{
    assert(r);
    assert(len);

    switch (r->kind) {
    case SEG_ZERO:
        if (!data) {
            r->len += len;
            return r;
        }
        goto generic;
    case SEG_FILE:
generic:
        {
            Segment *s = (Segment *) r;
            Segment *newseg;
            if (data) {
                newseg = new_mem_seg(len);
                memcpy(newseg->mem.data, data, len);
            } else {
                newseg = new_zero_seg(len);
            }
            uint64 seglen = s->len;
            if (offset == 0) {
                // insert to the left of s
                link(newseg, s->prev, s);
                r = make_branch(ROPE(newseg), r);
            } else if (offset == seglen) {
                // insert to the right of s
                link(newseg, s, s->next);
                r = make_branch(r, ROPE(newseg));
            } else {
                // insert in the middle of s
                Segment *right;
                s->len = offset;
                uint64 right_len = seglen - offset;
                switch (r->kind) {
                case SEG_ZERO:
                    right = new_zero_seg(right_len);
                    break;
                case SEG_FILE:
                    right = new_file_seg(right_len, s->file.offset + offset);
                    break;
                case SEG_MEM:
                    right = new_mem_seg(right_len);
                    memcpy(right->mem.data, s->mem.data + s->mem.offset + offset, right_len);
                    break;
                default:
                    assert(0);
                }
                right->next = s->next;
                s->next->prev = right;
                link(newseg, s, right);
                r = make_branch(ROPE(s), make_branch(ROPE(newseg), ROPE(right)));
            }
        }
        return r;
    case SEG_MEM:
        {
            Segment *s = (Segment *) r;
            if (offset == r->len && s->len + len <= s->mem.cap) {
                if (s->mem.offset + s->len + len > s->mem.cap) {
                    memmove(s->mem.data, s->mem.data + s->mem.offset, s->len);
                    s->mem.offset = 0;
                }
                uchar *dst = s->mem.data + s->mem.offset + s->len;
                if (data) {
                    memcpy(dst, data, len);
                } else {
                    memset(dst, 0, len);
                }
                r->len += len;
                return r;
            }
        }
        goto generic;
    case BRANCH:
        if (offset <= r->left->len) {
            r->left = rope_insert(r->left, offset, data, len);
        } else {
            r->right = rope_insert(r->right, offset - r->left->len, data, len);
        }
        r->len += len;
        return r;
    }
    assert(0);
}

static void
lock_cache(Buffer *b, uint64 addr, uint64 len)
{
    uint64 segoff;
    Segment *s = find_segment(b->rope, addr, &segoff);
    uint64 segstart = addr - segoff;
    uint64 end = addr + len;
    do {
        uint64 segend = segstart + s->len;
        Segment *next = s->next;
        if (s->kind == SEG_FILE) {
            uint64 a = max(addr&-CACHE_BLOCK_SIZE, segstart);
            do {
                int c = find_cache_opt(b, a);
                if (c >= 0) {
                    uint blkoff = a&CACHE_BLOCK_SIZE-1;
                    uchar *data = b->cache[c].data + blkoff;
                    uint len1 = CACHE_BLOCK_SIZE - blkoff;
                    if (a + len1 > segend) len1 = segend - a;
                    b->rope = rope_replace(b->rope, a, data, len1);
                    a += len1;
                }
                if (a >= end) return;
            } while (a < segend);
        }
        s = next;
        segstart = segend;
    } while (segstart < end && s->kind);
}

static void
dump_rope(Buffer *b, const char *header)
{
    uint64 segstart = 0;
    eprintf("%s\n", header);
    for (Segment *s = b->sentinel.first; s->kind; s = s->next) {
        eprintf("kind=%d start=%llu len=%llu\n", s->kind, segstart, s->len);
        segstart += s->len;
    }
}

void
buf_replace(Buffer *b, uint64 addr, const uchar *data, uint64 len)
{
    if (addr + len > b->buffer_size || addr + len < addr) {
        eprintf("buf_replace: out of range (%llu + %llu > %llu)\n",
                addr, len, b->buffer_size);
        if (addr >= b->buffer_size) return;
        len = b->buffer_size - addr;
    }

    if (!len) return;

    lock_cache(b, addr, len);
    b->rope = rope_replace(b->rope, addr, data, len);
}

void
buf_insert(Buffer *b, uint64 addr, const uchar *data, uint64 len)
{
    if (addr > b->buffer_size) {
        eprintf("buf_insert: out of range (%llu > %llu)\n",
                addr, b->buffer_size);
        return;
    }

    uint64 newsize = b->buffer_size + len;
    if (newsize < b->buffer_size) {
        eprintf("buf_insert: size overflow\n");
        return;
    }

    if (!len) return;

    if (b->rope) {
        b->rope = rope_insert(b->rope, addr, data, len);
    } else {
        Segment *newseg;
        if (data) {
            newseg = new_mem_seg(len);
            memcpy(newseg->mem.data, data, len);
        } else {
            newseg = new_zero_seg(len);
        }
        link(newseg, SEGMENT(&b->sentinel), SEGMENT(&b->sentinel));
        b->rope = ROPE(newseg);
    }
    b->buffer_size = newsize;
    //dump_rope(b, "after buf_insert");
}

void
buf_delete(Buffer *b, uint64 addr, uint64 len)
{
    if (addr + len > b->buffer_size || addr + len < addr) {
        eprintf("buf_delete: out of range (%llu + %llu > %llu)\n",
                addr, len, b->buffer_size);
        if (addr >= b->buffer_size) return;
        len = b->buffer_size - addr;
    }

    if (!len) return;

    b->rope = rope_delete(b->rope, addr, len);
    b->buffer_size -= len;
}

static void
read_file(Buffer *b, uchar *dst, uint64 fileoff, size_t n)
{
    do {
        uchar *src = get_file_data(b, fileoff);
        size_t n1 = CACHE_BLOCK_SIZE -
            ((size_t) fileoff & (CACHE_BLOCK_SIZE-1));
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
    Segment *s;
    uint64 segoff;
    if (addr >= b->buffer_size) {
        eprintf("buf_read: address %llu out of range (%llu)\n",
                addr, b->buffer_size);
        return;
    }

    s = find_segment(b->rope, addr, &segoff);
    assert(segoff < s->len);
    uint64 segstart = addr - segoff;
    size_t rem = n;
    for (;;) {
        size_t n1 = min(rem, segstart + s->len - addr);
        switch (s->kind) {
        case SEG_ZERO:
            memset(dst, 0, n1);
            break;
        case SEG_FILE:
            read_file(b, dst, s->file.offset + segoff, n1);
            break;
        case SEG_MEM:
            memcpy(dst, s->mem.data + s->mem.offset + segoff, n1);
            break;
        default:
            assert(0);
        }
        dst += n1;
        rem -= n1;
        if (rem == 0) return;// n;
        s = s->next;
        if (!s->kind) return;// n-rem;
        addr = segstart;
        segoff = 0;
    }
}

uint64
buf_size(Buffer *b)
{
    return b->buffer_size;
}
