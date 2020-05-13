char *utf16_to_mbcs_r(Region *, const wchar_t *utf16);
char *utf16_to_mbcs(const wchar_t *utf16);
wchar_t *mbcs_to_utf16_r(Region *, const char *mbcs);
wchar_t *mbcs_to_utf16(const char *mbcs);
char *utf8_to_mbcs(const char *utf8);
char *mbcs_to_utf8(const char *mbcs);
char *utf16_to_utf8(const wchar_t *widestr);
wchar_t *utf8_to_utf16(const char *str);
#ifdef UNICODE
#define UTF8_TO_TSTR utf8_to_utf16
#define TSTR_TO_UTF8 utf16_to_utf8
#else
#define UTF8_TO_TSTR utf8_to_mbcs
#define TSTR_TO_UTF8 mbcs_to_utf8
#endif
