int open_file_chooser_dialog(HWND owner, TCHAR *buf, int buflen);
int save_file_chooser_dialog(HWND owner, TCHAR *buf, int buflen);
void msgboxf(HWND, const TCHAR *, ...);
void format_error_code(TCHAR *, size_t, DWORD);
TCHAR *lstrdup(const TCHAR *s);
