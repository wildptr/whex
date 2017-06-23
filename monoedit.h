#ifndef _MONOEDIT_H_
#define _MONOEDIT_H_

#include <windows.h>

struct tag {
	int line;
	int start;
	int len;
	unsigned attr;
};

enum {
	MONOEDIT_WM_SET_BUFFER = WM_USER,
	MONOEDIT_WM_SET_CSIZE,
	MONOEDIT_WM_SCROLL,
	MONOEDIT_WM_SET_CURSOR_POS,
	MONOEDIT_WM_CLEAR_TAGS,
	MONOEDIT_WM_ADD_TAG,
};

ATOM monoedit_register_class(void);

#endif
