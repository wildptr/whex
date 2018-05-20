int _wsprintfA(char *, const char *, ...);
int _wsprintfW(wchar_t *, const wchar_t *, ...);
int _wvsprintfA(char *, const char *, va_list);
int _wvsprintfW(wchar_t *, const wchar_t *, va_list);
int bprintfA(BufA *, const char *, ...);
int bprintfW(BufW *, const wchar_t *, ...);
int vbprintfA(BufA *, const char *, va_list);
int vbprintfW(BufW *, const wchar_t *, va_list);
char *asprintfA(const char *, ...);
wchar_t *asprintfW(const wchar_t *, ...);

#ifdef UNICODE
#define _wsprintf _wsprintfW
#define _wvsprintf _wvsprintfW
#define bprintf bprintfW
#define vbprintf vbprintfW
#define asprintf asprintfW
#else
#define _wsprintf _wsprintfA
#define _wvsprintf _wvsprintfA
#define bprintf bprintfA
#define vbprintf vbprintfA
#define asprintf asprintfA
#endif
