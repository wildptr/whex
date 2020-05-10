#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <malloc.h>

char *strdup(const char *);

#define unreachable() assert(0)
/* For "unsigned INT64_" to be valid, INT64_ must not be a typedef name. */
#define INT64_ __int64
#define NEW(p, r) p = ralloc(r, sizeof *p)
#define NEWARRAY(p, n, r) p = ralloc(r, (n) * sizeof *p)
#define bputc(b, c) (b)->putc(b, c)
#define bputs(b, s) (b)->puts(b, s, strlen(s))
#define NELEM(x) (sizeof (x) / sizeof *(x))

typedef unsigned int uint;
typedef unsigned char uchar;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);

/* region */

typedef struct {
    char *cur, *limit;
    struct chunk *head;
} Region;

void *ralloc(Region *r, int size);
void rinit(Region *r);
void rfreeall(Region *r);
void rfree(Region *r, void *p);

#define T_(x) x
#define _TCHAR char
#include "printf.h"
#undef T_
#undef _TCHAR

#ifdef UNICODE
#define T_(x) x##_w
#define _TCHAR wchar_t
#include "printf.h"
#undef T_
#undef _TCHAR
#endif

#ifdef UNICODE
#define T(x) x##_w
#else
#define T(x) x
#endif
