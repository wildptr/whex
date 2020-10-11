#include "u.h"
#include <windows.h>
#include "unicode.h"

/*
 *  0xxxxxxx
 *  110xxxxx +6
 *  1110xxxx +12
 *  11110xxx +18
 *  111110xx +24
 *  1111110x +30
 *  ...
 */

int
utf8_length(uint32_t rune)
{
    if (rune < 0x80) return 1;
    if (rune < 0x800) return 2;
    if (rune < 0x10000) return 3;
#if 0
    if (rune < 0x200000) return 4;
    if (rune < 0x4000000) return 5;
    if (rune < 0x80000000) return 6;
    return 7;
#endif
    return 4;
}

int
to_utf8(uint32_t rune, char *p)
{
    int len;
    if (rune < 0x80) {
        *p = (char) rune;
        len = 1;
    }
    else if (rune < 0x800) {
        // 11-6
        p[0] = (char)(0xc0 | rune >> 6);
        // 6-0
        p[1] = (char)(0x80 | (rune & 0x3f));
        len = 2;
    }
    else if (rune < 0x10000) {
        // 16-12
        p[0] = (char)(0xe0 | rune >> 12);
        // 12-6
        p[1] = (char)(0x80 | (rune >> 6 & 0x3f));
        // 6-0
        p[2] = (char)(0x80 | (rune & 0x3f));
        len = 3;
    }
    else /*if (rune < 0x200000)*/ {
        // 21-18
        p[0] = (char)(0xf0 | rune >> 18);
        // 18-12
        p[1] = (char)(0x80 | (rune >> 12 & 0x3f));
        // 12-6
        p[2] = (char)(0x80 | (rune >> 6 & 0x3f));
        // 6-0
        p[3] = (char)(0x80 | (rune & 0x3f));
        len = 4;
    }
    return len;
#if 0
    if (rune < 0x4000000) {
        p[0] = (char)(0xf8 |  rune >> 24);
        p[1] = (char)(0x80 | (rune >> 18 & 0x3f));
        p[2] = (char)(0x80 | (rune >> 12 & 0x3f));
        p[3] = (char)(0x80 | (rune >>  6 & 0x3f));
        p[4] = (char)(0x80 | (rune       & 0x3f));
        return 5;
    }
    if (rune < 0x80000000) {
        p[0] = (char)(0xfc |  rune >> 30);
        p[1] = (char)(0x80 | (rune >> 24 & 0x3f));
        p[2] = (char)(0x80 | (rune >> 18 & 0x3f));
        p[3] = (char)(0x80 | (rune >> 12 & 0x3f));
        p[4] = (char)(0x80 | (rune >>  6 & 0x3f));
        p[5] = (char)(0x80 | (rune       & 0x3f));
        return 6;
    }
    p[0] = (char)(0xfe);
    p[1] = (char)(0x80 | (rune >> 30 & 0x3f));
    p[2] = (char)(0x80 | (rune >> 24 & 0x3f));
    p[3] = (char)(0x80 | (rune >> 18 & 0x3f));
    p[4] = (char)(0x80 | (rune >> 12 & 0x3f));
    p[5] = (char)(0x80 | (rune >>  6 & 0x3f));
    p[6] = (char)(0x80 | (rune       & 0x3f));
    return 7;
#endif
}

int
decode_utf16(const wchar_t *wp, int len, uint32_t *rune)
{
    int ret;
    if ((wp[0] & 0xfc00) == 0xd800 && len > 1 && (wp[1] & 0xfc00) == 0xdc00) {
        // surrogate pair
        *rune = 0x10000 +
            ((wp[0] & 0x3ff) << 10 | (wp[1] & 0x3ff));
        ret = 2;
    } else {
        *rune = *wp;
        ret = 1;
    }
    return ret;
}

// return value in wchar_t's
int
utf16_length(uint32_t rune)
{
    if (rune < 0x10000) return 1;
    if (rune < 0x110000) return 2;
    return 1; /* as if rune were 0xfffd */
}

char *
utf16_to_utf8(const wchar_t *widestr)
{
    // first calculate amount of memory needed to hold result UTF-8 string
    const wchar_t *wp = widestr;
    int len = wcslen(wp);
    int rem = len;
    int n_char = 0;
    while (rem > 0) {
        uint32_t rune;
        int n = decode_utf16(wp, rem, &rune);
        n_char += utf8_length(rune);
        wp += n;
        rem -= n;
    }
    char *buf = xmalloc(n_char+1);
    char *p = buf;
    wp = widestr;
    rem = len;
    while (rem > 0) {
        uint32_t rune;
        int n = decode_utf16(wp, rem, &rune);
        p += to_utf8(rune, p);
        wp += n;
        rem -= n;
    }
    *p = 0;
    return buf;
}

// return value in wchar_t's
int
to_utf16(uint32_t rune, wchar_t *p)
{
    if (rune < 0x10000) {
        *p = (wchar_t) rune;
        return 1;
    }
    if (rune < 0x110000) {
        uint32_t r = rune - 0x10000;
        p[0] = 0xd800 | ((r >> 10) & 0x3ff);
        p[1] = 0xdc00 | (r & 0x3ff);
        return 2;
    }
    *p = 0xfffd;
    return 1;
}

int
decode_utf8(const char *p, int len, uint32_t *rune)
{
    int ret;
    uint8_t *u = (uint8_t *) p;
    if ((*u & 0x80) == 0x00) {
        *rune = *u;
        ret = 1;
    }
    else if ((*u & 0xe0) == 0xc0) {
        if (len < 2) goto bad;
        *rune= (u[0] & 0x1f) << 6
             | (u[1] & 0x3f);
        ret = 2;
    }
    else if ((*u & 0xf0) == 0xe0) {
        if (len < 3) goto bad;
        *rune= (u[0] & 0x0f) << 12
             | (u[1] & 0x3f) << 6
             | (u[2] & 0x3f);
        ret = 3;
    }
    else /*if ((*u & 0xf8) == 0xf0)*/ {
        if (len < 4) goto bad;
        *rune= (u[0] & 0x07) << 18
             | (u[1] & 0x3f) << 12
             | (u[2] & 0x3f) << 6
             | (u[3] & 0x3f);
        ret = 4;
    }
    return ret;
bad:
    *rune = 0xfffd;
    return len;
}

wchar_t *
utf8_to_utf16(const char *str)
{
    const char *p = str;
    int len = strlen(str);
    int rem = len;
    int n_wchar = 0;
    while (rem > 0) {
        uint32_t rune;
        int n = decode_utf8(p, rem, &rune);
        n_wchar += utf16_length(rune);
        p += n;
        rem -= n;
    }
    wchar_t *buf = xmalloc((n_wchar+1)*2);
    wchar_t *wp = buf;
    p = str;
    rem = len;
    while (rem > 0) {
        uint32_t rune;
        int n = decode_utf8(p, rem, &rune);
        wp += to_utf16(rune, wp);
        p += n;
        rem -= n;
    }
    *wp = 0;
    return buf;
}

char *
utf8_to_mbcs(const char *utf8)
{
    // quick'n'dirty implementation, inefficient and
    // possibly incorrect
    wchar_t *utf16 = utf8_to_utf16(utf8);
    char *mbcs = utf16_to_mbcs(utf16);
    free(utf16);
    return mbcs;
}

char *
mbcs_to_utf8(const char *mbcs)
{
    // quick'n'dirty implementation, inefficient and
    // possibly incorrect
    wchar_t *utf16 = mbcs_to_utf16(mbcs);
    char *utf8 = utf16_to_utf8(utf16);
    free(utf16);
    return utf8;
}

char *
utf16_to_mbcs(const wchar_t *utf16)
{
    int utf16_len = lstrlenW(utf16);
    char *mbcs = xmalloc(utf16_len*2+1);
    WideCharToMultiByte(CP_ACP, 0, utf16, utf16_len+1,
                        mbcs, utf16_len*2+1, 0, 0);
    return mbcs;
}

char *
utf16_to_mbcs_r(Region *r, const wchar_t *utf16)
{
    int utf16_len = lstrlenW(utf16);
    char *mbcs = ralloc(r, utf16_len*2+1);
    WideCharToMultiByte(CP_ACP, 0, utf16, utf16_len+1,
                        mbcs, utf16_len*2+1, 0, 0);
    return mbcs;
}

wchar_t *
mbcs_to_utf16(const char *mbcs)
{
    int mbcs_len = strlen(mbcs)+1;
    wchar_t *utf16 = xmalloc((mbcs_len)*2);
    MultiByteToWideChar(CP_ACP, 0, mbcs, mbcs_len, utf16, mbcs_len);
    return utf16;
}

wchar_t *
mbcs_to_utf16_r(Region *r, const char *mbcs)
{
    int mbcs_len = strlen(mbcs)+1;
    wchar_t *utf16 = ralloc(r, (mbcs_len)*2);
    MultiByteToWideChar(CP_ACP, 0, mbcs, mbcs_len, utf16, mbcs_len);
    return utf16;
}
