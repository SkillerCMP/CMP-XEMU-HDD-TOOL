#pragma once
#include "windows.h"
#define PROGRESS_CLASSW L"msctls_progress32"
#define WC_LISTVIEWW L"SysListView32"
#define WC_TABCONTROLW L"SysTabControl32"
#define TCS_FIXEDWIDTH 0x0400
#define PBS_MARQUEE 8
#define ICC_PROGRESS_CLASS 32
#define ICC_TAB_CLASSES 8
#define ICC_LISTVIEW_CLASSES 1
#define LVS_REPORT 1
#define LVS_SINGLESEL 4
#define LVS_SHOWSELALWAYS 8
#define LVS_EX_FULLROWSELECT 32
#define LVS_EX_GRIDLINES 1
#define LVS_EX_DOUBLEBUFFER 65536
#define LVS_EX_LABELTIP 16384
#define LVNI_SELECTED 2
#define LVIS_SELECTED 2
#define LVIS_FOCUSED 1
#define LVIF_TEXT 1
#define LVCF_TEXT 4
#define LVCF_WIDTH 2
#define LVCF_SUBITEM 8
#define LVIR_BOUNDS 0
#define LVIM_AFTER 1
#define LVHT_NOWHERE 1
#define TCIF_TEXT 1
constexpr UINT LVN_ITEMCHANGED=UINT(-101),NM_DBLCLK=UINT(-3),LVN_BEGINDRAG=UINT(-109),NM_RCLICK=UINT(-5),LVN_KEYDOWN=UINT(-155),TCN_SELCHANGING=UINT(-552),TCN_SELCHANGE=UINT(-551);
enum { PBM_SETMARQUEE=0x40a,LVM_SETITEMTEXTW=0x1074,LVM_INSERTITEMW=0x104d,LVM_INSERTCOLUMNW=0x1061,LVM_SETINSERTMARK=0x10a6,
 TCM_GETCURSEL=0x130b,TCM_INSERTITEMW=0x133e };
struct INITCOMMONCONTROLSEX {DWORD dwSize,dwICC;};struct NMHDR {HWND hwndFrom;UINT_PTR idFrom;UINT code;};
struct NMLISTVIEW {NMHDR hdr;int iItem,iSubItem;UINT uNewState,uOldState,uChanged;POINT ptAction;LPARAM lParam;};
struct NMLVKEYDOWN {NMHDR hdr;WORD wVKey;UINT flags;};
struct LVITEMW {UINT mask;int iItem,iSubItem;UINT state,stateMask;wchar_t* pszText;int cchTextMax,iImage;LPARAM lParam;};
struct LVCOLUMNW {UINT mask;int fmt,cx;wchar_t* pszText;int cchTextMax,iSubItem,iImage,iOrder;};
struct LVINSERTMARK {UINT cbSize;DWORD dwFlags;int iItem;DWORD dwReserved;};
struct LVHITTESTINFO {POINT pt;UINT flags;int iItem,iSubItem,iGroup;};
struct TCITEMW {UINT mask;DWORD dwState,dwStateMask;wchar_t* pszText;int cchTextMax,iImage;LPARAM lParam;};
extern "C" {BOOL InitCommonControlsEx(const INITCOMMONCONTROLSEX*);int ListView_GetNextItem(HWND,int,UINT);BOOL ListView_DeleteAllItems(HWND);
BOOL ListView_SetItemState(HWND,int,UINT,UINT);BOOL ListView_EnsureVisible(HWND,int,BOOL);HWND ListView_GetHeader(HWND);
BOOL ListView_Scroll(HWND,int,int);int ListView_HitTest(HWND,LVHITTESTINFO*);BOOL ListView_GetItemRect(HWND,int,RECT*,int);int ListView_GetItemCount(HWND);
DWORD ListView_SetExtendedListViewStyle(HWND,DWORD);int ListView_GetColumnWidth(HWND,int);BOOL ListView_SetColumnWidth(HWND,int,int);}

constexpr UINT NM_CUSTOMDRAW=UINT(-12);
constexpr DWORD CDDS_PREPAINT=1,CDDS_POSTPAINT=2;
constexpr LRESULT CDRF_DODEFAULT=0,CDRF_NOTIFYPOSTPAINT=16;
struct NMCUSTOMDRAW {NMHDR hdr;DWORD dwDrawStage;HDC hdc;RECT rc;DWORD_PTR dwItemSpec;UINT uItemState;LPARAM lItemlParam;};
struct NMLVCUSTOMDRAW {NMCUSTOMDRAW nmcd;COLORREF clrText,clrTextBk;int iSubItem;};

#define PBM_SETRANGE32 0x406
#define PBM_SETPOS 0x402
#define TCM_SETCURSEL 0x130c
#define TCM_SETITEMSIZE 0x1329
inline int ListView_InsertItem(HWND w,LVITEMW* i){return static_cast<int>(SendMessageW(w,LVM_INSERTITEMW,0,reinterpret_cast<LPARAM>(i)));}
inline int ListView_InsertColumn(HWND w,int n,LVCOLUMNW* c){return static_cast<int>(SendMessageW(w,LVM_INSERTCOLUMNW,static_cast<WPARAM>(n),reinterpret_cast<LPARAM>(c)));}
inline void ListView_SetItemText(HWND w,int n,int col,wchar_t* text){LVITEMW i{};i.iSubItem=col;i.pszText=text;SendMessageW(w,LVM_SETITEMTEXTW,static_cast<WPARAM>(n),reinterpret_cast<LPARAM>(&i));}

// Theme declarations, not native execution.
#define WC_HEADERW L"SysHeader32"
#define HDI_TEXT 2
#define HDI_FORMAT 4
#define HDF_RIGHT 1
#define HDF_CENTER 2
#define CDIS_SELECTED 1
#define CDIS_HOT 64
#define CDDS_ITEMPREPAINT 0x10001
#define CDRF_NOTIFYITEMDRAW 0x20
#define CDRF_NEWFONT 2
#define LVM_SETINSERTMARKCOLOR 0x10AA
#define PBM_GETPOS 0x408
#define PBM_GETRANGE 0x407
struct PBRANGE{int iLow,iHigh;};
struct HDITEMW{UINT mask;int cxy;wchar_t* pszText;HANDLE hbm;int cchTextMax,fmt;LPARAM lParam;int iImage,iOrder;UINT type;void* pvFilter;UINT state;};
using SUBCLASSPROC=LRESULT(CALLBACK*)(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
extern "C" {
BOOL SetWindowSubclass(HWND,SUBCLASSPROC,UINT_PTR,DWORD_PTR);BOOL GetWindowSubclass(HWND,SUBCLASSPROC,UINT_PTR,DWORD_PTR*);
BOOL RemoveWindowSubclass(HWND,SUBCLASSPROC,UINT_PTR);LRESULT DefSubclassProc(HWND,UINT,WPARAM,LPARAM);
BOOL ListView_SetBkColor(HWND,COLORREF);BOOL ListView_SetTextBkColor(HWND,COLORREF);BOOL ListView_SetTextColor(HWND,COLORREF);UINT ListView_GetItemState(HWND,int,UINT);
int TabCtrl_GetItemCount(HWND);int TabCtrl_GetCurSel(HWND);BOOL TabCtrl_GetItemRect(HWND,int,RECT*);BOOL TabCtrl_GetItem(HWND,int,TCITEMW*);
int Header_GetItemCount(HWND);BOOL Header_GetItemRect(HWND,int,RECT*);BOOL Header_GetItem(HWND,int,HDITEMW*);
}
