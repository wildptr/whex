#ifndef _UNICODE_H_
#define _UNICODE_H_

#include <stdint.h>

char *utf16_to_mbcs_r(Region *, const wchar_t *utf16);
char *utf16_to_mbcs(const wchar_t *utf16);
wchar_t *mbcs_to_utf16_r(Region *, const char *mbcs);
wchar_t *mbcs_to_utf16(const char *mbcs);

#endif // _UNICODE_H_
