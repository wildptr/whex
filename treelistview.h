typedef struct TLV_Item TLV_Item;

ATOM tlv_register_class(void);
void tlv_add_column(HWND hwnd, int width, const TCHAR *caption);
TLV_Item *tlv_add_item(HWND, TLV_Item *parent, int ncol, const TCHAR **cols);
void tlv_clear(HWND);
