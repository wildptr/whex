#include "u.h"

#include <windows.h>
#include <commctrl.h>

#include "treelistview.h"

struct TLV_Item {
    HTREEITEM treeitem;
    TLV_Item *parent, *next, *first_child, *last_child;
    int ncol;
    TCHAR *cols[];
};

typedef struct {
    HWND treeview;
    HWND header;
    WNDPROC treeview_wndproc;
    int ncol;
    int max_ncol;
    int *colwidth;
    //int *colstart;
    //HTREEITEM sel_item;
    //int sel_col;
    TLV_Item *first_item, *last_item;
} TreeListView;

static TCHAR *
lstrdup(const TCHAR *s)
{
    int nb = (lstrlen(s)+1) * sizeof(TCHAR);
    TCHAR *ret = xmalloc(nb);
    memcpy(ret, s, nb);
    return ret;
}

static TLV_Item *
add_item(TreeListView *c, TLV_Item *parent, int ncol, const TCHAR **cols)
{
#if 0
    if (G.items_len == G.items_cap) {
        assert(G.items_cap > 0);
        int newcap = G.items_cap*2;
        G.items = xrealloc(G.items, newcap * sizeof *G.items);
        G.items_cap = newcap;
    }
#endif
    TLV_Item *item = xmalloc(offsetof(TLV_Item, cols[ncol]));
    item->parent = parent;
    item->next = 0;
    item->first_child = 0;
    item->last_child = 0;

    // add child to parent
    if (parent) {
        if (parent->last_child) {
            parent->last_child->next = item;
            parent->last_child = item;
        } else {
            parent->first_child = item;
            parent->last_child = item;
        }
    } else {
        if (c->last_item) {
            c->last_item->next = item;
            c->last_item = item;
        } else {
            c->first_item = item;
            c->last_item = item;
        }
    }

    HTREEITEM parent_treeitem;
    if (parent) {
        parent_treeitem = parent->treeitem;
    } else {
        parent_treeitem = 0;
    }

    TVINSERTSTRUCT tvins = {0};
    tvins.hParent = parent_treeitem;
    tvins.hInsertAfter = TVI_LAST;
    tvins.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvins.item.pszText = cols[0];
    tvins.item.lParam = (LPARAM)item;
    item->treeitem = (HTREEITEM)
        SendMessage(c->treeview, TVM_INSERTITEM, 0, (LPARAM)&tvins);

    item->parent = parent;
    item->ncol = ncol;
    for (int i=0; i<ncol; i++) {
        item->cols[i] = lstrdup(cols[i]);
    }
    return item;
}

static LRESULT CALLBACK my_treeview_wndproc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK tlv_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

ATOM
tlv_register_class(void)
{
    WNDCLASS wndclass = {0};

    wndclass.lpfnWndProc = tlv_wndproc;
    wndclass.cbWndExtra = sizeof(LONG_PTR);
    wndclass.hInstance = GetModuleHandle(0);
    wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wndclass.lpszClassName = TEXT("TreeListView");
    return RegisterClass(&wndclass);
}

static int
header_insert_item(HWND header, int insert_after,
    int width, const TCHAR *text)
{
    HD_ITEM hdi;
    int index;

    hdi.mask = HDI_TEXT | HDI_FORMAT | HDI_WIDTH;
    hdi.pszText = text;
    hdi.cxy = width;
    hdi.cchTextMax = lstrlen(text);
    hdi.fmt = HDF_LEFT | HDF_STRING;

    index = SendMessage(header, HDM_INSERTITEM,
        (WPARAM) insert_after, (LPARAM) &hdi);

    return index;
}

static void
free_item(TLV_Item *item)
{
    for (TLV_Item *child = item->first_child, *next; child; child = next) {
        next = child->next;
        free_item(child);
    }
    free(item);
}

static void handle_ITEMPOSTPAINT(TreeListView *, NMTVCUSTOMDRAW *);

static LRESULT CALLBACK
tlv_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TreeListView *c;

    switch (msg) {
    case WM_NCCREATE:
        c = xmalloc(sizeof *c);
        memset(c, 0, sizeof *c);
        SetWindowLongPtr(hwnd, 0, (LONG_PTR)c);
        break;
    case WM_NCDESTROY:
        c = (TreeListView *) GetWindowLongPtr(hwnd, 0);
        free(c->colwidth);
        for (TLV_Item *item = c->first_item, *next; item; item = next) {
            next = item->next;
            free_item(item);
        }
        return 0;
    case WM_CREATE:
        {
            c = (TreeListView *) GetWindowLongPtr(hwnd, 0);
            CREATESTRUCT *cs = (CREATESTRUCT *) lparam;
            c->treeview = CreateWindow
                (WC_TREEVIEW,
                 TEXT(""),
                 WS_CHILD | WS_VISIBLE |
                 TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS,
                 0, 0, 0, 0,
                 hwnd, (HMENU)1, cs->hInstance, 0);
            c->treeview_wndproc = (WNDPROC) SetWindowLongPtr
                (c->treeview, GWL_WNDPROC, (LONG_PTR)my_treeview_wndproc);
            c->header = CreateWindow
                (WC_HEADER,
                 TEXT(""),
                 WS_CHILD | WS_VISIBLE | HDS_BUTTONS | HDS_HORZ,
                 0, 0, 0, 0,
                 hwnd, (HMENU)2, cs->hInstance, 0);
            c->max_ncol = 4;
            c->colwidth = xmalloc(4 * sizeof *c->colwidth);
#if 0
            G.colstart[0] = 0;
            for (int i=1; i<G.ncol; i++) {
                G.colstart[i] = G.colstart[i-1] + G.colwidth[i];
            }
#endif
            return 0;
        }
    case WM_SIZE:
        {
            int w, h;
            //if (wparam == SIZE_MINIMIZED) return 0;
            w = LOWORD(lparam);
            h = HIWORD(lparam);
            c = (TreeListView *) GetWindowLongPtr(hwnd, 0);
            /* set header geometry */
            RECT rect = { 0, 0, w, h };
            WINDOWPOS wp; /* receives geometry computed by header control */
            HD_LAYOUT layout = { &rect, &wp };
            Header_Layout(c->header, &layout);
            SetWindowPos(c->header, wp.hwndInsertAfter,
                         wp.x, wp.y, wp.cx, wp.cy, wp.flags);
            int header_height = wp.cy;
            /* set treeview geometry */
            SetWindowPos(c->treeview, 0, 0, header_height, w, h-header_height,
                         SWP_NOZORDER);
            return 0;
        }
    case WM_NOTIFY:
        if (wparam == 1/*treeview*/) {
            if (((NMHDR*)lparam)->code == NM_CUSTOMDRAW) {
                NMTVCUSTOMDRAW *tvcd = (NMTVCUSTOMDRAW*)lparam;
                switch (tvcd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    //puts("CDDS_PREPAINT");
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT:
                    /* necessary for CDDS_ITEMPOSTPAINT notifications */
                    return CDRF_NOTIFYPOSTPAINT;
                case CDDS_ITEMPOSTPAINT:
                    c = (TreeListView *) GetWindowLongPtr(hwnd, 0);
                    handle_ITEMPOSTPAINT(c, tvcd);
                    return 0;
                }
            }
        } else if (wparam == 2/*header*/) {
            if (((NMHDR*)lparam)->code == HDN_ITEMCHANGED) {
                HD_NOTIFY *hdn = (HD_NOTIFY*)lparam;
                int item = hdn->iItem;
                HD_ITEM *hdi = hdn->pitem;
                int width = hdi->cxy;
                c = (TreeListView *) GetWindowLongPtr(hwnd, 0);
                if (item < c->ncol) {
                    c->colwidth[item] = width;
                    // set bErase to TRUE so old text gets erased
                    InvalidateRect(c->treeview, 0, TRUE);
                }
            }
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void
paint_cell(HDC dc, RECT *rect, const TCHAR *text)
{
#if 0
    if (selected) {
        FillRect(dc, rect, (HBRUSH)(COLOR_HIGHLIGHT+1));
        DrawFocusRect(dc, rect);
        // also need to set background color for TextOut
    }
#endif
    if (text) TextOut(dc, rect->left+1, rect->top+1, text, lstrlen(text));
}

static TLV_Item *
get_treeitem_lparam(HWND treeview, HTREEITEM treeitem)
{
    TV_ITEM tvitem;
    tvitem.hItem = treeitem;
    tvitem.mask = TVIF_PARAM;
    TreeView_GetItem(treeview, &tvitem);
    return (TLV_Item *) tvitem.lParam;
}

static void
handle_ITEMPOSTPAINT(TreeListView *c, NMTVCUSTOMDRAW *tvcd)
{
    HTREEITEM treeitem = (HTREEITEM) tvcd->nmcd.dwItemSpec;
    RECT rect;

    TLV_Item *item = get_treeitem_lparam(c->treeview, treeitem);
    assert(item);

    // FALSE means we want the entire line occupied by the item
    TreeView_GetItemRect(c->treeview, treeitem, &rect, FALSE);
    HDC dc = tvcd->nmcd.hdc;
    int l = c->colwidth[0];
    for (int i=1; i<item->ncol && i<c->ncol; i++) {
        int r = l+c->colwidth[i];
        rect.left = l;
        rect.right = r;
        paint_cell(dc, &rect, item->cols[i]);
        l = r;
    }
}

static LRESULT CALLBACK
my_treeview_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
#if 0
    switch (msg) {
    case WM_LBUTTONDOWN:
        {
            int x = LOWORD(lparam);
            int y;
            int l, colw;
            l = 0;
            int col;
            for (int i=0; i<G.ncol; i++) {
                colw = G.colwidth[i];
                if ((uint)(x-l) < colw) {
                    col = i;
                    goto found;
                }
                l += colw;
            }
            G.sel_col = -1;
            G.sel_item = 0;
            InvalidateRect(G.treeview, 0, TRUE);
            return 0;
found:
            y = HIWORD(lparam);
            TV_HITTESTINFO ht;
            ht.pt.x = x;
            ht.pt.y = y;
            TreeView_HitTest(G.treeview, &ht);
            if (col) {
                // deselect tree view
                TreeView_SelectItem(G.treeview, 0);
            } else {
                // may select or deselect
                TreeView_SelectItem(G.treeview, ht.hItem);
            }
            // select the cell
            G.sel_col = col;
            G.sel_item = ht.hItem;
            InvalidateRect(G.treeview, 0, TRUE);
            if (col) return 0;
        }
    }
#endif
    TreeListView *c = (TreeListView *) GetWindowLongPtr(GetParent(hwnd), 0);
    return CallWindowProc(c->treeview_wndproc, hwnd, msg, wparam, lparam);
}

void
tlv_add_column(HWND hwnd, int width, const TCHAR *caption)
{
    TreeListView *c = (TreeListView *) GetWindowLongPtr(hwnd, 0);
    if (c->ncol == c->max_ncol) {
        return;//TODO
    }
    c->colwidth[c->ncol++] = width;
    header_insert_item(c->header, c->ncol, width, caption);
}

TLV_Item *
tlv_add_item(HWND hwnd, TLV_Item *parent, int ncol, const TCHAR **cols)
{
    TreeListView *c = (TreeListView *) GetWindowLongPtr(hwnd, 0);
    return add_item(c, parent, ncol, cols);
}

void
tlv_clear(HWND hwnd)
{
    TreeListView *c = (TreeListView *) GetWindowLongPtr(hwnd, 0);
    SendMessage(c->treeview, TVM_DELETEITEM, 0, (LPARAM) TVI_ROOT);
    for (TLV_Item *item = c->first_item, *next; item; item = next) {
        next = item->next;
        free_item(item);
    }
    c->first_item = 0;
    c->last_item = 0;
}
