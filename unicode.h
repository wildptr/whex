#ifndef _UNICODE_H_
#define _UNICODE_H_

#include <stdint.h>

int utf8_length(uint32_t rune);
int utf16_length(uint32_t rune);
int to_utf8(uint32_t rune, char *p);
int to_utf16(uint32_t rune, wchar_t *p);
uint32_t decode_utf8(const char *p);
uint32_t decode_utf16(const wchar_t *wp);
// result is malloc()'d
char *utf16_to_utf8(const wchar_t *widestr);
// result is malloc()'d
wchar_t *utf8_to_utf16(const char *str);

#endif // _UNICODE_H_
