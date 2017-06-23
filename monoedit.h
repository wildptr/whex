#ifndef _MONOEDIT_H_
#define _MONOEDIT_H_

#include <windows.h>

enum {
	MONOEDIT_WM_SET_BUFFER = WM_USER,
	MONOEDIT_WM_SET_CSIZE,
	MONOEDIT_WM_SCROLL,
	MONOEDIT_WM_SET_CURSOR_POS,
};

ATOM monoedit_register_class(void);
BOOL monoedit_unregister_class(void);

#endif
