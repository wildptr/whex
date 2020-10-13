#include "u.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "winutil.h"

#define LINE_HEIGHT 16

/****************************************************************************
 * Type Definitions                                                         *
 ****************************************************************************/
enum {
    NEED_REPAINT = 1,
};

typedef struct line {
    struct line *next, *prev;
    TCHAR *text; // NOT terminated
    int len, cap;
    uint flags;
} Line;

typedef struct editor_state {
    Line *first_line, *last_line;
    /* cursor */
    Line *cur_line;
    int cur_line_offset;
    /* font information */
    //HFONT font;
    int *charwidth; // starting at 0x20 (' ')
    TEXTMETRIC *textmetric;
    /* GUI-related stuff */
    HWND hwnd;
    HBRUSH bg_brush;
    Line *first_visible_line; // may be NULL
    int first_visible_line_y;
    bool caret_visible;
    /**/
    bool need_full_repaint;
    bool caret_moved;
} Editor_State;

typedef struct paint_params {
    HDC dc;
    int top;
    int window_width;
    HBRUSH bg_brush;
    int *charwidth;
} Paint_Params;

/****************************************************************************
 * Function prototypes                                                      *
 ****************************************************************************/
Line *new_line(int cap);
int grow_line(Line *l, int newcap);
void append_char(Line *l, TCHAR c);
void insert_char(Line *l, int offset, TCHAR c);
void append_string(Line *l, TCHAR *s, int len);
void delete_char(Line *l, int pos);
Line *insert_line_after(Editor_State *s, Line *l, int cap);
void delete_line(Editor_State *s, Line *l);

bool is_printable_char(TCHAR c);
int char_width_raw(int *width_table, TCHAR c);
int rep_char(TCHAR buf[static 6], wchar_t c);
int char_width(int *width_table, wchar_t c);
int text_width(int *width_table, TCHAR *text, int len);

void paint_line(Paint_Params *p, Line *l);
LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
static ATOM register_wndclass(void);
void update_ui(Editor_State *);

/****************************************************************************
 * Implementation                                                           *
 ****************************************************************************/

Line *
new_line(int cap)
{
    Line *l = xmalloc0(sizeof *l);
    l->text = xmalloc(cap);
    l->cap = cap;
    return l;
}

int
grow_line(Line *l, int newcap)
{
    TCHAR *newtext = realloc(l->text, newcap * sizeof(TCHAR));
    if (!newtext) return -1;
    l->text = newtext;
    l->cap = newcap;
    return 0;
}

void
append_char(Line *l, TCHAR c)
{
    if (l->len == l->cap) {
        if (grow_line(l, l->cap + (l->len+1)) < 0) return;
    }
    l->text[l->len++] = c;
}

void
insert_char(Line *l, int offset, TCHAR c)
{
    if (offset >= l->len) {
        append_char(l, c);
    } else {
        if (l->len == l->cap) {
            if (grow_line(l, l->cap + (l->len+1)) < 0) return;
        }
        memmove(l->text+offset+1, l->text+offset, (l->len-offset)*sizeof(TCHAR));
        l->text[offset] = c;
        l->len++;
    }
}

void
append_string(Line *l, TCHAR *s, int len)
{
    int newlen = l->len + len;
    if (newlen > l->cap) {
        if (grow_line(l, l->cap + newlen) < 0) return;
    }
    memcpy(l->text + l->len, s, len * sizeof(TCHAR));
    l->len = newlen;
}

void
delete_char(Line *l, int pos)
{
    assert(pos >= 0);
    if (pos >= l->len) return;
    memmove(l->text+pos, l->text+pos+1, (l->len-(pos+1))*sizeof(TCHAR));
    l->len--;
}

Line *
insert_line_after(Editor_State *s, Line *l, int cap)
{
    Line *newl = new_line(cap);
    newl->prev = l;
    if (l->next) {
        newl->next = l->next;
        l->next->prev = newl;
    } else {
        // newl->next remains NULL
        s->last_line = newl;
    }
    l->next = newl;
    return newl;
}

void
delete_line(Editor_State *s, Line *l)
{
    Line *prev = l->prev;
    Line *next = l->next;

    if (prev) prev->next = next;
    else s->first_line = next;

    if (next) next->prev = prev;
    else s->last_line = prev;

    // fix references
    if (s->first_visible_line == l) {
        s->first_visible_line = next;
    }

    free(l->text);
    free(l);
}

bool
is_printable_char(TCHAR c)
{
    return c>=0x20 && c<=0x7e;
}

int
char_width_raw(int *width_table, TCHAR c)
{
    unsigned index = c-0x20;
    if (index >= 95) return 0;
    return width_table[index];
}

int
rep_char(TCHAR buf[static 6], wchar_t c)
{
    int len;
    if (c<0x100) {
        T(_sprintf)(buf, TEXT("$%.2X"), c);
        len = 3;
    } else {
        T(_sprintf)(buf, TEXT("$%.4X"), c);
        len = 5;
    }
    return len;
}

int
char_width(int *width_table, wchar_t c)
{
    if (is_printable_char(c)) return char_width_raw(width_table, c);

    TCHAR rep[6];
    int len = rep_char(rep, c);
    int sum = 0;
    for (int i=0; i<len; i++) sum += char_width_raw(width_table, rep[i]);
    return sum;
}

int
text_width(int *width_table, TCHAR *text, int len)
{
    int sum = 0;
    while (len--) sum += char_width(width_table, *text++);
    return sum;
#if 0
    RECT rect = {0};
    DrawText(dc, text, len, &rect, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
    return rect.right;
#endif
}

void
paint_line(Paint_Params *p, Line *l)
{
    int len = l->len;
    TCHAR *text = l->text;
    int y = p->top;
    HDC dc = p->dc;

    int i=0, x=0;
    TCHAR c;
    for (;;) {
        while (i<len && !is_printable_char(c=text[i])) {
            TCHAR buf[6];
            int replen = rep_char(buf, c);
            TextOut(dc, x, y, buf, replen);
            for (int k=0; k<replen; k++) {
                x += char_width_raw(p->charwidth, buf[k]);
            }
            i++;
        }
        if (i==len) break;
        // text[i] is printable
        int j = i;
        int newx = x;
        while (j<len && is_printable_char(text[j])) {
            newx += char_width_raw(p->charwidth, text[j]);
            j++;
        }
        // either j==len, or text[j] is non-printable
        TextOut(dc, x, y, text+i, j-i);
        i = j;
        x = newx;
    }

    RECT rect = {
        .top = y, .bottom = y+LINE_HEIGHT,
        .left = x, .right = p->window_width
    };
    FillRect(dc, &rect, p->bg_brush);

    l->flags &= ~NEED_REPAINT;
}

LRESULT CALLBACK
wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    Editor_State *s;
    Line *l;
    int offset;

    switch (msg) {
    case WM_NCCREATE:
        s = ((LPCREATESTRUCT)lparam)->lpCreateParams;
        s->hwnd = hwnd;
        s->bg_brush = (HBRUSH) GetClassLongPtr(hwnd, GCLP_HBRBACKGROUND);
        SetWindowLongPtr(hwnd, 0, (LONG_PTR) s);

        {
            HDC dc = GetDC(hwnd);
            s->charwidth = xmalloc(95 * sizeof *s->charwidth);
#if 0
            float *charwidthf = xmalloc(95 * sizeof *charwidthf);
            GetCharWidthFloat(dc, 0x20, 0x7e, charwidthf);
            for (int i=0; i<95; i++) {
                s->charwidth[i] = charwidthf[i]*16.0f;
            }
            free(charwidthf);
#endif
            GetCharWidth(dc, 0x20, 0x7e, s->charwidth);

            s->textmetric = xmalloc(sizeof *s->textmetric);
            GetTextMetrics(dc, s->textmetric);

            ReleaseDC(hwnd, dc);
        }

        break; // hand over to DefWindowProc
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_LBUTTONDOWN:
        s = (Editor_State *) GetWindowLongPtr(hwnd, 0);
        {
            int x = LOWORD(lparam);
            int y = HIWORD(lparam);

            // find the line
            int vis_rel_y = (y - s->first_visible_line_y)/LINE_HEIGHT;
            if (vis_rel_y < 0) return 0;
            l = s->first_visible_line;
            if (!l) return 0;
            int tmp = vis_rel_y;
            while (tmp) {
                if (!l->next) break;
                l = l->next;
                tmp--;
            }

            // find the character
            int i = x/s->textmetric->tmAveCharWidth; // initial guess
            if (i > l->len) i = l->len;
            int left = text_width(s->charwidth, l->text, i);
            int right;
            if (i < l->len && (right = left + char_width(s->charwidth, l->text[i])) <= x) {
                // search to the right
                do {
                    left = right;
                    i++;
                    if (i >= l->len) break;
                    int cw = char_width(s->charwidth, l->text[i]);
                    right = left+cw;
                } while (right <= x);
            } else if (left > x) {
                // search to the left
                do {
                    right = left;
                    if (i <= 0) break;
                    i--;
                    int cw = char_width(s->charwidth, l->text[i]);
                    left = right-cw;
                } while (left > x);
            } // otherwise initial guess is correct

            if (x-left > right-x) i++;

            s->cur_line = l;
            s->cur_line_offset = i;
            s->caret_moved = true;
        }
        return 0;
    case WM_KEYDOWN:
        s = (Editor_State *) GetWindowLongPtr(hwnd, 0);
        switch (wparam) {
        case VK_LEFT:
            if (s->cur_line_offset > 0) {
                s->cur_line_offset--;
                s->caret_moved = true;
            } else {
                l = s->cur_line;
                if (l->prev) {
                    s->cur_line = l = l->prev;
                    s->cur_line_offset = l->len;
                    s->caret_moved = true;
                }
            }
            break;
        case VK_RIGHT:
            if (s->cur_line_offset < s->cur_line->len) {
                s->cur_line_offset++;
                s->caret_moved = true;
            } else {
                l = s->cur_line;
                if (l->next) {
                    s->cur_line = l->next;
                    s->cur_line_offset = 0;
                    s->caret_moved = true;
                }
            }
            break;
        case VK_UP:
            if (s->cur_line->prev) {
                s->cur_line = s->cur_line->prev;
                s->caret_moved = true;
            }
            break;
        case VK_DOWN:
            if (s->cur_line->next) {
                s->cur_line = s->cur_line->next;
                s->caret_moved = true;
            }
            break;
        case VK_HOME:
            if (s->cur_line_offset) {
                s->cur_line_offset = 0;
                s->caret_moved = true;
            }
            break;
        case VK_END:
            {
                int line_len = s->cur_line->len;
                if (s->cur_line_offset != line_len) {
                    s->cur_line_offset = line_len;
                    s->caret_moved = true;
                }
            }
            break;
        case VK_DELETE:
            l = s->cur_line;
            offset = s->cur_line_offset;
            if (offset >= l->len) offset = l->len;
            if (offset < l->len) {
                delete_char(l, offset);
                l->flags |= NEED_REPAINT;
            } else {
                Line *next = l->next;
                // don't delete the last line
                if (next) {
                    append_string(l, next->text, next->len);
                    delete_line(s, next);
                    s->need_full_repaint = true;
                }
            }
            break;
        }
        return 0;
    case WM_CHAR:
        s = (Editor_State *) GetWindowLongPtr(hwnd, 0);
        l = s->cur_line;
        TCHAR c = wparam;
        switch (c) {
        case '\r'/*ENTER*/:
            {
                Line *newl = insert_line_after(s, l, 256);
                offset = s->cur_line_offset;
                if (offset < l->len) {
                    // need to break the line
                    append_string(newl, l->text + offset, l->len - offset);
                    l->len = offset;
                }
                s->cur_line = newl;
                s->cur_line_offset = 0;
                s->need_full_repaint = true;
                s->caret_moved = true;
            }
            break;
        case '\b':
            offset = s->cur_line_offset;
            if (offset >= l->len) offset = l->len;
            if (offset) {
                delete_char(l, offset-1);
                l->flags |= NEED_REPAINT;
                s->cur_line_offset = offset-1;
                s->caret_moved = true;
            } else {
                Line *prev = l->prev;
                // don't delete the first line
                if (prev) {
                    append_string(prev, l->text, l->len);
                    delete_line(s, l);
                    s->cur_line = prev;
                    s->cur_line_offset = prev->len;
                    s->need_full_repaint = true;
                    s->caret_moved = true;
                }
            }
            break;
        case 0x1b: // ESCAPE
            // debug
            printf("%d/%d\n", s->cur_line_offset, s->cur_line->len);
            break;
        default:
            {
                offset = s->cur_line_offset;
                if (offset > l->len) {
                    offset = l->len;
                }
                insert_char(l, offset, c);
                s->cur_line_offset = offset+1;
                l->flags |= NEED_REPAINT;
                s->caret_moved = true;
            }
        }
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            s = (Editor_State *) GetWindowLongPtr(hwnd, 0);
            RECT rect;
            GetClientRect(hwnd, &rect);
            Paint_Params p = {0};
            p.dc = dc;
            p.window_width = rect.right;
            p.bg_brush = s->bg_brush;
            p.top = s->first_visible_line_y;
            p.charwidth = s->charwidth;
            for (l = s->first_visible_line;
                 l && p.top < rect.bottom; l = l->next)
            {
                paint_line(&p, l);
                p.top += LINE_HEIGHT;
            }
            rect.top = p.top;
            FillRect(dc, &rect, s->bg_brush);
            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

static ATOM
register_wndclass(void)
{
    WNDCLASS wndclass = {0};

    wndclass.lpfnWndProc = wndproc;
    wndclass.cbWndExtra = sizeof(LONG_PTR);
    wndclass.hInstance = GetModuleHandle(0);
    wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    wndclass.lpszClassName = TEXT("NEWEDIT");
    return RegisterClass(&wndclass);
}

void
update_ui(Editor_State *s)
{
    HWND hwnd = s->hwnd;
    HDC dc = GetDC(hwnd);
    RECT rect;
    GetClientRect(hwnd, &rect);
    Paint_Params p = {0};
    p.dc = dc;
    p.window_width = rect.right;
    p.bg_brush = s->bg_brush;
    p.top = s->first_visible_line_y;
    p.charwidth = s->charwidth;

    // hide caret during painting to prevent ghost carets
    HideCaret(hwnd);

    if (s->need_full_repaint) {
        for (Line *l = s->first_visible_line;
             l && p.top < rect.bottom; l = l->next)
        {
            paint_line(&p, l);
            p.top += LINE_HEIGHT;
        }
        rect.top = p.top;
        FillRect(dc, &rect, s->bg_brush);
        s->need_full_repaint = 0;
    } else {
        for (Line *l = s->first_line; l; l = l->next) {
            if (l->flags & NEED_REPAINT) {
                paint_line(&p, l);
            }
            p.top += LINE_HEIGHT;
        }
    }

    ShowCaret(hwnd);

    /* handle caret position */
    if (s->caret_moved) {
        Line *curline = s->cur_line;
        int offset = s->cur_line_offset;
        assert(offset >= 0);
        if (offset > curline->len) offset = curline->len;
        int x = text_width(s->charwidth, curline->text, offset);
        int y = s->first_visible_line_y;
        Line *l = s->first_visible_line;
        bool found=0;
        while (l && y < rect.bottom) {
            if (l == curline) {
                found=1;
                break;
            }
            y += LINE_HEIGHT;
            l = l->next;
        }
        if (found) {
            //printf("SetCaretPos(%d, %d)\n", x, y);
            SetCaretPos(x, y);
            if (!s->caret_visible) {
                ShowCaret(hwnd);
                s->caret_visible = true;
            }
        } else {
#if 0 // not necessary
            if (s->caret_visible) {
                HideCaret(hwnd);
                s->caret_visible = false;
            }
#endif
        }
        s->caret_moved = false;
    }

    ReleaseDC(hwnd, dc);
}

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE _prev_instance, LPSTR _cmdline, int show)
{
    ATOM wndclass;
    HWND hwnd;
    MSG msg;

    wndclass = register_wndclass();
    if (!wndclass) {
        TCHAR msg[512];
        format_error_code(msg, NELEM(msg), GetLastError());
        fputs("Failed to register window class:\n", stderr);
        T(fputs)(msg, stderr);
        return 1;
    }

    Editor_State state = {0};
    Line *line = new_line(256);
    state.first_line = line;
    state.last_line = line;
    state.cur_line = line;
    state.first_visible_line = line;

    hwnd = CreateWindowEx
        (0,//WS_EX_CLIENTEDGE,
         CLASSNAME(wndclass), TEXT("Editor"), WS_OVERLAPPEDWINDOW,
         CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
         0, 0, instance, &state);

    ShowWindow(hwnd, show);

    CreateCaret(hwnd, NULL, 1, LINE_HEIGHT);
    ShowCaret(hwnd);
    state.caret_visible = true;

    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (!PeekMessage(&msg, 0, 0, 0, 0)) update_ui(&state);
    }
    return msg.wParam;
}
