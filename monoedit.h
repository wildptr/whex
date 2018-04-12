typedef struct {
	int line;
	int start;
	int len;
	unsigned attr;
} MedTag;

enum {
	MED_WM_SET_BUFFER = WM_USER,
	MED_WM_SET_CSIZE,
	MED_WM_SCROLL,
	MED_WM_SET_CURSOR_POS,
	MED_WM_CLEAR_TAGS,
	MED_WM_ADD_TAG,
};

ATOM med_register_class(void);
