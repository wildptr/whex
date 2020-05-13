#include "u.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <math.h>

#include "winutil.h"
#include "unicode.h"

/* window kind */
enum {
    FRAME,
    BUTTON,
    TEXTBOX,
    INVALID_KIND
};

enum {
    RC_WEIGHT   = 1,
    RC_MINSIZE  = 2,
};

typedef struct window Window;

typedef struct row_col_config {
    struct row_col_config *next;
    int index;
    uint flags;
    int weight;
    int minsize;
} RowColConfig;

/* also used as Lua userdata */
struct window {
    HWND hwnd;
    lua_State *lua;
    uchar kind;
    uchar sticky;
    WNDPROC super_wndproc; // for controls
    Window *first_child;
    Window **ptl;
    Window *next;
    int row, col, rowspan, colspan;
    int width, height; // for controls; these specify minimum size
    RowColConfig *row_config, *col_config;
    int padx0, padx1, pady0, pady1;
};

enum {
    C_X         = 1,
    C_Y         = 2,
    C_WIDTH     = 4,
    C_HEIGHT    = 8,
    C_ROW       = 0x10,
    C_COLUMN    = 0x20,
    C_ROWSPAN   = 0x40,
    C_COLUMNSPAN= 0x80,
    C_STICKY    = 0x100,
    C_PARENT    = 0x200,
    C_TEXT      = 0x400,

    C_PADX0     = 0x1000,
    C_PADX1     = 0x2000,
    C_PADY0     = 0x4000,
    C_PADY1     = 0x8000,
};

enum {
    STICKY_N = 1,
    STICKY_S = 2,
    STICKY_W = 4,
    STICKY_E = 8,
};

typedef struct {
    uint flags;
    int x, y, width, height;
    int row, col;
    int rowspan, colspan;
    uchar sticky;
    int padx0, padx1, pady0, pady1;
    Window *parent;
    TCHAR *text;
} Config;

static ATOM the_luatk_class;

static void parse_config(lua_State *L, int index, Config *c);
void luaerrorbox(HWND hwnd, lua_State *L);

int luatk_api_create_window(lua_State *L);
int luatk_api_show_window(lua_State *L);
int luatk_api_rowconfigure(lua_State *L);
int luatk_api_columnconfigure(lua_State *L);

int
luatk_api_open(lua_State *L)
{
    luaL_newmetatable(L, "luatk_window");

    lua_pushcfunction(L, luatk_api_create_window);
    lua_setglobal(L, "luatk_create_window");
    lua_pushcfunction(L, luatk_api_show_window);
    lua_setglobal(L, "luatk_show_window");
    lua_pushcfunction(L, luatk_api_rowconfigure);
    lua_setglobal(L, "luatk_rowconfigure");
    lua_pushcfunction(L, luatk_api_columnconfigure);
    lua_setglobal(L, "luatk_columnconfigure");

    /* HWND -> window userdata */
    /* prevent window userdata from being GC'd during lifetime of window */
    lua_newtable(L);
    lua_setglobal(L, "luatk_window_map");

    int ret = luaL_dofile(L, "luatk.lua");
    if (ret) {
        // TODO: print message to log window
        const char *msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        eprintf("%s\n", msg);
    }

    return ret;
}

void
unregister_window(Window *w)
{
    HWND hwnd = w->hwnd;
    w->hwnd = 0;
    // remove from luatk_window_map
    lua_State *L = w->lua;
    lua_getglobal(L, "luatk_window_map");
    lua_pushnil(L);
    lua_rawsetp(L, -2, (void*)hwnd);
    lua_pop(L, 1);
    //eprintf("unregistered window %x\n", hwnd);
}

typedef struct {
    int minsize;
    int weight;
    int start;
    int size;
} RowColInfo;

static void
layout(Window *w, int total_width, int total_height)
{
    /* compute grid size */
    int nrow = 0, ncol = 0;
    for (Window *child = w->first_child; child; child = child->next) {
        int bottom = child->row + child->rowspan;
        int right = child->col + child->colspan;
        if (bottom > nrow) nrow = bottom;
        if (right > ncol) ncol = right;
    }

    /* compute minimum size for each row/column */
    RowColInfo *rowinfo = calloc(nrow, sizeof *rowinfo);
    RowColInfo *colinfo = calloc(ncol, sizeof *colinfo);

    for (RowColConfig *c = w->row_config; c; c = c->next) {
        int i = c->index;
        if ((uint)i < nrow) {
            rowinfo[i].weight = c->weight;
            rowinfo[i].minsize = c->minsize;
        }
    }

    for (RowColConfig *c = w->col_config; c; c = c->next) {
        int i = c->index;
        if ((uint)i < ncol) {
            colinfo[i].weight = c->weight;
            colinfo[i].minsize = c->minsize;
        }
    }

    for (Window *child = w->first_child; child; child = child->next) {
        int w = child->width + (child->padx0 + child->padx1);
        int h = child->height + (child->pady0 + child->pady1);
        if (child->rowspan == 1) {
            int i = child->row;
            if (h > rowinfo[i].minsize) rowinfo[i].minsize = h;
        }
        if (child->colspan == 1) {
            int i = child->col;
            if (w > colinfo[i].minsize) colinfo[i].minsize = w;
        }
    }

#if 0
    for (int i=0; i<nrow; i++) {
        eprintf("row %d minsize=%d\n", i, rowinfo[i].minsize);
    }
#endif

    /* distribute extra space */
    int minwidth = 0, minheight = 0;
    for (int i=0; i<nrow; i++) minheight += rowinfo[i].minsize;
    for (int i=0; i<ncol; i++) minwidth += colinfo[i].minsize;

    int total_row_weight = 0, total_col_weight = 0;
    for (int i=0; i<nrow; i++) total_row_weight += rowinfo[i].weight;
    for (int i=0; i<ncol; i++) total_col_weight += colinfo[i].weight;

    int far_right;
    int extra_x = total_width - minwidth;
    if (extra_x > 0 && total_col_weight) {
        float k = (float)extra_x / total_col_weight;
        float x = 0.0f;
        for (int i=0; i<ncol; i++) {
            float size = colinfo[i].minsize + k * colinfo[i].weight;
            colinfo[i].start = (int)roundf(x);
            x += size;
            colinfo[i].size = (int)roundf(x) - colinfo[i].start;
        }
        far_right = (int)roundf(x);
    } else {
        int x = 0;
        for (int i=0; i<ncol; i++) {
            colinfo[i].start = x;
            colinfo[i].size = colinfo[i].minsize;
            x += colinfo[i].minsize;
        }
        far_right = x;
    }

    int far_bottom;
    int extra_y = total_height - minheight;
    if (extra_y > 0 && total_row_weight) {
        float k = (float)extra_y / total_row_weight;
        float y = 0.0f;
        for (int i=0; i<nrow; i++) {
            float size = rowinfo[i].minsize + k * rowinfo[i].weight;
            rowinfo[i].start = (int)roundf(y);
            y += size;
            rowinfo[i].size = (int)roundf(y) - rowinfo[i].start;
        }
        far_bottom = (int)roundf(y);
    } else {
        int y = 0;
        for (int i=0; i<nrow; i++) {
            rowinfo[i].start = y;
            rowinfo[i].size = rowinfo[i].minsize;
            y += rowinfo[i].minsize;
        }
        far_bottom = y;
    }

#if 0
    for (int i=0; i<nrow; i++) {
        eprintf("row %d start=%d\n", i, rowinfo[i].start);
    }
#endif

    /**/

    for (Window *child = w->first_child; child; child = child->next) {
        int row = child->row;
        int row_end = row + child->rowspan;
        int col = child->col;
        int col_end = col + child->colspan;
        int top = rowinfo[row].start;
        int bottom = row_end >= nrow ? far_bottom : rowinfo[row_end].start;
        int left = colinfo[col].start;
        int right = col_end >= ncol ? far_right : colinfo[col_end].start;
        int padx0 = child->padx0;
        int padx1 = child->padx1;
        int pady0 = child->pady0;
        int pady1 = child->pady1;
        //eprintf("padding: %d %d %d %d\n", padx0, padx1, pady0, pady1);
        int xoff, yoff, width, height;
        int span_height = bottom - top;
        int span_width = right - left;
        int alloc_height = span_height - (pady0 + pady1);
        int alloc_width = span_width - (padx0 + padx1);
        uchar sticky = child->sticky;
        height = (sticky & (STICKY_N|STICKY_S)) == (STICKY_N|STICKY_S) ?
            alloc_height : child->height;
        if (sticky & STICKY_N) {
            yoff = pady0;
        } else {
            if (sticky & STICKY_S) {
                yoff = pady0 + alloc_height - height;
            } else {
                yoff = pady0 + (alloc_height - height >> 1);
            }
        }
        width = (sticky & (STICKY_W|STICKY_E)) == (STICKY_W|STICKY_E) ?
            alloc_width : child->width;
        if (sticky & STICKY_W) {
            xoff = padx0;
        } else {
            if (sticky & STICKY_E) {
                xoff = padx0 + alloc_width - width;
            } else {
                xoff = padx0 + (alloc_width - width >> 1);
            }
        }
        SetWindowPos(child->hwnd, 0, left+xoff, top+yoff, width, height, SWP_NOZORDER);
    }

    free(rowinfo);
    free(colinfo);
}

/* for container windows */
LRESULT CALLBACK
ltkframe_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_NCDESTROY:
        {
            Window *w = (Window *) GetWindowLongPtr(hwnd, 0);
            for (RowColConfig *c = w->row_config, *next; c; c = next) {
                next = c->next;
                free(c);
            }
            for (RowColConfig *c = w->col_config, *next; c; c = next) {
                next = c->next;
                free(c);
            }
            unregister_window(w);
            /* from now on, w may get GC'd */
        }
        return 0;
    case WM_COMMAND:
        {
            HWND ctl = (HWND)lparam;
            Window *ctlw = (Window *) GetWindowLongPtr(ctl, GWLP_USERDATA);
            lua_State *L = ctlw->lua;
            switch (ctlw->kind) {
            case BUTTON:
                lua_getglobal(L, "luatk_window_map");
                lua_rawgetp(L, -1, (void*)ctl);
                // -1 -> window userdata
                // -2 -> luatk_window_map
                lua_getuservalue(L, -1);
                // -1 -> window uservalue
                // -2 -> window userdata
                // -3 -> luatk_window_map
                lua_pushstring(L, "command");
                lua_gettable(L, -2);
                // -1 -> command handler
                // -2 -> window uservalue
                // -3 -> window userdata
                // -4 -> luatk_window_map
                if (!lua_isnil(L, -1)) {
                    lua_insert(L, -2);
                    // -1 -> window uservalue
                    // -2 -> command handler
                    if (lua_pcall(L, 1, 0, 0)) {
                        luaerrorbox(hwnd, L);
                        lua_pop(L, 1);
                    }
                } else {
                    lua_pop(L, 2);
                }
                // -1 -> window userdata
                // -2 -> luatk_window_map
                lua_pop(L, 2);
                break;
            }
        }
        return 0;
    case WM_SIZE:
        {
            Window *w = (Window *) GetWindowLongPtr(hwnd, 0);
            int width = LOWORD(lparam);
            int height = HIWORD(lparam);
            w->width = width;
            w->height = height;
            layout(w, width, height);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK
ctl_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    Window *w = (Window *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCDESTROY:
        unregister_window(w);
        return 0;
    }
    return CallWindowProc(w->super_wndproc, hwnd, msg, wparam, lparam);
}

int
luatk_init(void)
{
    WNDCLASS wc = {0};

    wc.lpfnWndProc = ltkframe_wndproc;
    wc.cbWndExtra = sizeof(void*);
    wc.hInstance = GetModuleHandle(0);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName = TEXT("LTKFRAME");

    ATOM atom = RegisterClass(&wc);
    if (!atom) return -1;
    the_luatk_class = atom;
    return 0;
}

Window *
create_window_userdata(HWND hwnd, lua_State *L, uchar kind, const Config *c,
                       int uservalue_index)
{
    Window *w = lua_newuserdata(L, sizeof *w);
    memset(w, 0, sizeof *w);
    w->hwnd = hwnd;
    w->lua = L;
    w->kind = kind;
    w->row = c->row;
    w->col = c->col;
    w->rowspan = c->rowspan > 0 ? c->rowspan : 1;
    w->colspan = c->colspan > 0 ? c->colspan : 1;
    w->ptl = &w->first_child;
    // set metatable
    luaL_getmetatable(L, "luatk_window");
    lua_setmetatable(L, -2);
    // set uservalue
    lua_pushvalue(L, uservalue_index);
    lua_setuservalue(L, -2);
    /* put window userdata into global map so it is not GC'd when the hwnd
       is still valid */
    lua_getglobal(L, "luatk_window_map");
    // -1 -> luatk_window_map
    // -2 -> w
    lua_pushvalue(L, -2); // push w
    // -1 -> w
    // -2 -> luatk_window_map
    // -3 -> w
    lua_rawsetp(L, -2, (void*)hwnd);
    // -1 -> luatk_window_map
    // -2 -> w
    lua_pop(L, 1);
    // -1 -> w
    return w;
}

typedef struct {
    const char *classname;
    const TCHAR *T_classname;
    uchar kind;
} ClassInfo;

ClassInfo classtable[] = {
    { "frame", TEXT("LTKFRAME"), FRAME },
    { "button", TEXT("BUTTON"), BUTTON },
    { "entry", TEXT("EDIT"), TEXTBOX },
};

static ClassInfo *
lookup_class(const char *classname)
{
    for (int i=0; i<NELEM(classtable); i++) {
        if (!strcmp(classname, classtable[i].classname)) {
            return &classtable[i];
        }
    }
    return 0;
}

/* luatk_create_window(classname, window, config) */
int
luatk_api_create_window(lua_State *L)
{
    const char *classname = luaL_checkstring(L, 1);
    ClassInfo *class = lookup_class(classname);
    if (!class) return 0;

    Config c = {0};
    parse_config(L, 3, &c);

    DWORD style;
    HWND parent_hwnd;
    if (c.parent) {
        style = WS_CHILD | WS_VISIBLE;
        parent_hwnd = c.parent->hwnd;
    } else {
        /* top-level window */
        style = WS_OVERLAPPEDWINDOW;
        parent_hwnd = 0;
        int w = c.width;
        int h = c.height;
        if (!(c.flags & C_X)) c.x = CW_USEDEFAULT;
        if (!(c.flags & C_Y)) c.y = CW_USEDEFAULT;
        if (!(c.flags & C_WIDTH)) w = 512;
        if (!(c.flags & C_HEIGHT)) h = 384;
        RECT r = { 0, 0, w, h };
        AdjustWindowRect(&r, style, FALSE);
        c.width = r.right - r.left;
        c.height = r.bottom - r.top;
    }

    const TCHAR *text = c.flags & C_TEXT ? c.text : TEXT("");

    HWND hwnd = CreateWindow
        (class->T_classname, text, style,
         c.x, c.y, c.width, c.height,
         parent_hwnd, 0, GetModuleHandle(0), 0);

    if (c.flags & C_TEXT) free(c.text);

    /* silently returns nil on failure */
    if (!hwnd) return 0;

    // places userdata on top of stack
    Window *w = create_window_userdata(hwnd, L, class->kind, &c, 2);
    w->width = c.width;
    w->height = c.height;
    w->sticky = c.sticky;
    w->padx0 = c.padx0;
    w->padx1 = c.padx1;
    w->pady0 = c.pady0;
    w->pady1 = c.pady1;
    if (c.parent) {
        *c.parent->ptl = w;
        c.parent->ptl = &w->next;
    }
    if (class->kind == FRAME) {
        SetWindowLongPtr(hwnd, 0, (LONG_PTR)w);
    } else {
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)w);
        WNDPROC super_wndproc = SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)ctl_wndproc);
        w->super_wndproc = super_wndproc;
        SendMessage(hwnd, WM_SETFONT,
                    (WPARAM) GetStockObject(DEFAULT_GUI_FONT), 0);
    }

    return 1;
}

#define CHECK_WINDOW(w) do if (!(w)->hwnd) return luaL_error(L, "invalid window"); while (0)

int
luatk_api_show_window(lua_State *L)
{
    Window *w = luaL_checkudata(L, 1, "luatk_window");
    CHECK_WINDOW(w);

    int show = luaL_checkinteger(L, 2);
    ShowWindow(w->hwnd, show);
    return 0;
}

static void
parse_row_col_config(lua_State *L, int index, RowColConfig *c)
{
    lua_pushstring(L, "weight");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        int weight = luaL_checkinteger(L, -1);
        if (weight >= 0) {
            c->weight = weight;
            c->flags |= RC_WEIGHT;
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "minsize");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        int minsize = luaL_checkinteger(L, -1);
        if (minsize >= 0) {
            c->minsize = minsize;
            c->flags |= RC_MINSIZE;
        }
    }
    lua_pop(L, 1);
}

static void
update_row_col_config(RowColConfig *c, RowColConfig *newc)
{
    if (newc->flags & RC_WEIGHT) {
        c->weight = newc->weight;
    }
    if (newc->flags & RC_MINSIZE) {
        c->minsize = newc->minsize;
    }
}

int
luatk_api_rowconfigure(lua_State *L)
{
    Window *w = luaL_checkudata(L, 1, "luatk_window");
    CHECK_WINDOW(w);

    int index = luaL_checkinteger(L, 2);
    // TODO: report?
    if (index < 0) return 0;

    RowColConfig newc = {0};
    parse_row_col_config(L, 3, &newc);
    for (RowColConfig *c = w->row_config; c; c = c->next) {
        if (c->index == index) {
            update_row_col_config(c, &newc);
            return 0;
        }
    }
    /* no existing config */
    newc.index = index;
    RowColConfig *c = xmalloc(sizeof *c);
    *c = newc;
    c->next = w->row_config;
    w->row_config = c;
    return 0;
}

int
luatk_api_columnconfigure(lua_State *L)
{
    Window *w = luaL_checkudata(L, 1, "luatk_window");
    CHECK_WINDOW(w);

    int index = luaL_checkinteger(L, 2);
    if (index < 0) return 0;

    RowColConfig newc = {0};
    parse_row_col_config(L, 3, &newc);
    for (RowColConfig *c = w->col_config; c; c = c->next) {
        if (c->index == index) {
            update_row_col_config(c, &newc);
            return 0;
        }
    }
    /* no existing config */
    newc.index = index;
    RowColConfig *c = xmalloc(sizeof *c);
    *c = newc;
    c->next = w->col_config;
    w->col_config = c;
    return 0;
}

/* 'c' must be zeroed out */
static void
parse_config(lua_State *L, int index, Config *c)
{
#if 0
    /* text */
    lua_pushstring(L, "text");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        const char *text = luaL_checkstring(L, -1);
        if (text) {
            c->text = strdup(text);
        }
    }
    lua_pop(L, 1);
#endif

    lua_pushstring(L, "x");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        c->x = luaL_checkinteger(L, -1);
        c->flags |= C_X;
    }
    lua_pop(L, 1);

    lua_pushstring(L, "y");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        c->y = luaL_checkinteger(L, -1);
        c->flags |= C_Y;
    }
    lua_pop(L, 1);

    lua_pushstring(L, "width");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        int width = luaL_checkinteger(L, -1);
        if (width >= 0){
            c->width = width;
            c->flags |= C_WIDTH;
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "height");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        int height = luaL_checkinteger(L, -1);
        if (height >= 0) {
            c->height = height;
            c->flags |= C_HEIGHT;
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "row");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        int row = luaL_checkinteger(L, -1);
        if (row >= 0) {
            c->row = row;
            c->flags |= C_ROW;
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "column");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        int col = luaL_checkinteger(L, -1);
        if (col >= 0) {
            c->col = col;
            c->flags |= C_COLUMN;
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "rowspan");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        int rowspan = luaL_checkinteger(L, -1);
        if (rowspan > 0) {
            c->rowspan = rowspan;
            c->flags |= C_ROWSPAN;
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "columnspan");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        int colspan = luaL_checkinteger(L, -1);
        if (colspan > 0) {
            c->colspan = colspan;
            c->flags |= C_COLUMNSPAN;
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "sticky");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        const char *s = luaL_checkstring(L, -1);
        uchar bits = 0;
        while (*s) {
            switch (*s) {
            case 'N': case 'n':
                bits |= STICKY_N; break;
            case 'S': case 's':
                bits |= STICKY_S; break;
            case 'W': case 'w':
                bits |= STICKY_W; break;
            case 'E': case 'e':
                bits |= STICKY_E; break;
            }
            s++;
        }
        c->sticky = bits;
        c->flags |= C_STICKY;
    }
    lua_pop(L, 1);

    lua_pushstring(L, "padx");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        if (lua_isinteger(L, -1)) {
            int pad = lua_tointeger(L, -1);
            if (pad >= 0) {
                c->padx0 = pad;
                c->padx1 = pad;
                c->flags |= C_PADX0 | C_PADX1;
            }
        } else {
            lua_pushinteger(L, 1);
            lua_gettable(L, index);
            lua_pushinteger(L, 2);
            lua_gettable(L, index);
            int pad0 = luaL_checkinteger(L, -2);
            int pad1 = luaL_checkinteger(L, -1);
            if (pad0 >= 0) {
                c->padx0 = pad0;
                c->flags |= C_PADX0;
            }
            if (pad1 >= 0) {
                c->padx1 = pad1;
                c->flags |= C_PADX1;
            }
            lua_pop(L, 2);
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "pady");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        if (lua_isinteger(L, -1)) {
            int pad = lua_tointeger(L, -1);
            if (pad >= 0) {
                c->pady0 = pad;
                c->pady1 = pad;
                c->flags |= C_PADY0 | C_PADY1;
            }
        } else {
            lua_pushinteger(L, 1);
            lua_gettable(L, index);
            lua_pushinteger(L, 2);
            lua_gettable(L, index);
            int pad0 = luaL_checkinteger(L, -2);
            int pad1 = luaL_checkinteger(L, -1);
            if (pad0 >= 0) {
                c->pady0 = pad0;
                c->flags |= C_PADY0;
            }
            if (pad1 >= 0) {
                c->pady1 = pad1;
                c->flags |= C_PADY1;
            }
            lua_pop(L, 2);
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "parent");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        lua_pushstring(L, "luatk_window"); // get the userdata
        lua_gettable(L, -2);
        // -1 -> userdata
        // -2 -> Lua window object (ordinary table)
        Window *w = luaL_checkudata(L, -1, "luatk_window");
        lua_pop(L, 1);
        c->flags |= C_PARENT;
        c->parent = w;
    }
    lua_pop(L, 1);

    lua_pushstring(L, "text");
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        c->text = UTF8_TO_TSTR(luaL_checkstring(L, -1));
        c->flags |= C_TEXT;
    }
    lua_pop(L, 1);
}
