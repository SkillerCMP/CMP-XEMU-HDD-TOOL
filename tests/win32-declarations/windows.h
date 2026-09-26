#pragma once
// Declaration-only host syntax facade. NOT a Windows SDK, renderer, or runtime.
#include <cstdint>
#include <cstddef>
#include <cwchar>
#define WINAPI
#define CALLBACK
#define TRUE 1
#define FALSE 0
using BOOL=int;using BYTE=unsigned char;using WORD=unsigned short;using UINT=unsigned int;
using DWORD=unsigned long;using LONG=long;using INT_PTR=intptr_t;using LONG_PTR=intptr_t;using UINT_PTR=uintptr_t;
using WPARAM=uintptr_t;using LPARAM=intptr_t;using LRESULT=intptr_t;using HRESULT=long;using PWSTR=wchar_t*;
struct HWND__;using HWND=HWND__*;struct HINSTANCE__;using HINSTANCE=HINSTANCE__*;
struct HMENU__;using HMENU=HMENU__*;struct HDC__;using HDC=HDC__*;struct HFONT__;using HFONT=HFONT__*;
struct HBRUSH__;using HBRUSH=HBRUSH__*;struct HICON__;using HICON=HICON__*;struct HCURSOR__;using HCURSOR=HCURSOR__*;
using HANDLE=void*;using HGDIOBJ=void*;
struct POINT { LONG x,y; }; struct RECT {LONG left,top,right,bottom;};
struct PAINTSTRUCT {HDC hdc;BOOL fErase;RECT rcPaint;BOOL fRestore,fIncUpdate;BYTE rgbReserved[32];};
struct CREATESTRUCTW {void* lpCreateParams;};
struct MINMAXINFO {POINT ptReserved,ptMaxSize,ptMaxPosition,ptMinTrackSize,ptMaxTrackSize;};
struct MSG {HWND hwnd;UINT message;WPARAM wParam;LPARAM lParam;DWORD time;POINT pt;};
using WNDPROC=LRESULT(CALLBACK*)(HWND,UINT,WPARAM,LPARAM);using DLGPROC=INT_PTR(CALLBACK*)(HWND,UINT,WPARAM,LPARAM);
struct WNDCLASSEXW {UINT cbSize,style;WNDPROC lpfnWndProc;int cbClsExtra,cbWndExtra;HINSTANCE hInstance;HICON hIcon;HCURSOR hCursor;HBRUSH hbrBackground;const wchar_t* lpszMenuName;const wchar_t* lpszClassName;HICON hIconSm;};
#pragma pack(push,2)
struct DLGTEMPLATE {DWORD style,dwExtendedStyle;WORD cdit;short x,y,cx,cy;};
#pragma pack(pop)
#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 8
#define WC_ERR_INVALID_CHARS 128
#define GWLP_USERDATA (-21)
#define DWLP_USER 16
#define ERROR_CLASS_ALREADY_EXISTS 1410
#define ERROR_CANCELLED 1223
#define HRESULT_FROM_WIN32(x) (static_cast<HRESULT>(x))
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define CW_USEDEFAULT (-2147483647)
#define IDC_ARROW ((const wchar_t*)32512)
#define IDI_APPLICATION ((const wchar_t*)32512)
#define MAKEINTRESOURCEW(i) ((const wchar_t*)((uintptr_t)((WORD)(i))))
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
#define WM_APP 0x8000
#define LOWORD(x) (static_cast<WORD>(static_cast<uintptr_t>(x)&0xffff))
#define HIWORD(x) (static_cast<WORD>((static_cast<uintptr_t>(x)>>16)&0xffff))
#define MAKELPARAM(lo,hi) (static_cast<LPARAM>((static_cast<uintptr_t>(static_cast<WORD>(lo))) | (static_cast<uintptr_t>(static_cast<WORD>(hi))<<16)))
#define WS_CHILD 0x40000000UL
#define WS_VISIBLE 0x10000000UL
#define WS_TABSTOP 0x00010000UL
#define WS_VSCROLL 0x00200000UL
#define WS_POPUP 0x80000000UL
#define WS_CAPTION 0x00c00000UL
#define WS_SYSMENU 0x00080000UL
#define WS_OVERLAPPEDWINDOW 0x00cf0000UL
#define WS_CLIPCHILDREN 0x02000000UL
#define WS_EX_CLIENTEDGE 0x200UL
#define WS_EX_CONTROLPARENT 0x10000UL
#define DS_MODALFRAME 0x80
#define DS_CENTER 0x800
#define ES_AUTOHSCROLL 0x80
#define ES_MULTILINE 4
#define ES_READONLY 0x800
#define ES_AUTOVSCROLL 0x40
#define SS_NOPREFIX 0x80
#define BS_AUTOCHECKBOX 3
#define BS_PUSHBUTTON 0
#define BS_DEFPUSHBUTTON 1
#define CBS_DROPDOWNLIST 3
#define BST_CHECKED 1
#define EN_CHANGE 0x300
#define CBN_SELCHANGE 1
#define COLOR_BTNFACE 15
#define COLOR_3DSHADOW 16
#define FW_NORMAL 400
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define CLEARTYPE_QUALITY 5
#define DEFAULT_PITCH 0
#define LOGPIXELSX 88
#define SW_SHOW 5
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SWP_NOZORDER 4
#define SWP_NOACTIVATE 16
#define MB_OK 0
#define MB_OKCANCEL 1
#define MB_YESNO 4
#define MB_ICONWARNING 0x30
#define MB_ICONQUESTION 0x20
#define MB_ICONINFORMATION 0x40
#define MB_ICONERROR 0x10
#define MB_DEFBUTTON2 0x100
#define MF_STRING 0
#define MF_GRAYED 1
#define MF_CHECKED 8
#define MF_SEPARATOR 0x800
#define TPM_RETURNCMD 0x100
#define TPM_NONOTIFY 0x80
#define VK_ESCAPE 27
#define VK_F2 113
#define VK_DELETE 46
#define CLSCTX_INPROC_SERVER 1
#define COINIT_APARTMENTTHREADED 2
#define COINIT_DISABLE_OLE1DDE 4
// Unique message values preserve switch-case checking in this syntax-only facade.
enum { WM_CREATE=1, WM_SIZE=5, WM_PAINT=15, WM_CLOSE=16, WM_DESTROY=2, WM_NCCREATE=129,WM_NCDESTROY=130,
 WM_COMMAND=273,WM_NOTIFY=78,WM_DPICHANGED=736,WM_GETMINMAXINFO=36,WM_DROPFILES=563,WM_SETREDRAW=11,WM_SETFONT=48,
 WM_INITDIALOG=272,WM_LBUTTONDOWN=513,WM_LBUTTONUP=514,WM_MOUSEMOVE=512,WM_TIMER=275,WM_CAPTURECHANGED=533,WM_KEYDOWN=256,
 EM_SETSEL=177,EM_REPLACESEL=194,EM_SCROLLCARET=183,EM_SETLIMITTEXT=197,BM_GETCHECK=240,CB_GETCURSEL=327,CB_ADDSTRING=323,CB_SETCURSEL=334 };
extern "C" {
HWND GetDlgItem(HWND,int);int GetWindowTextLengthW(HWND);int GetWindowTextW(HWND,wchar_t*,int);BOOL SetWindowTextW(HWND,const wchar_t*);
LRESULT SendMessageW(HWND,UINT,WPARAM,LPARAM);BOOL PostMessageW(HWND,UINT,WPARAM,LPARAM);
BOOL EnableWindow(HWND,BOOL);BOOL IsWindowEnabled(HWND);BOOL GetClientRect(HWND,RECT*);BOOL GetWindowRect(HWND,RECT*);
BOOL MoveWindow(HWND,int,int,int,int,BOOL);int MulDiv(int,int,int);BOOL ShowWindow(HWND,int);BOOL UpdateWindow(HWND);
BOOL SetWindowPos(HWND,HWND,int,int,int,int,UINT);BOOL InvalidateRect(HWND,const RECT*,BOOL);
HFONT CreateFontW(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,const wchar_t*);BOOL DeleteObject(HGDIOBJ);
HWND CreateWindowExW(DWORD,const wchar_t*,const wchar_t*,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,void*);
HINSTANCE GetModuleHandleW(const wchar_t*);WORD RegisterClassExW(const WNDCLASSEXW*);DWORD GetLastError();
LONG_PTR GetWindowLongPtrW(HWND,int);LONG_PTR SetWindowLongPtrW(HWND,int,LONG_PTR);HWND GetParent(HWND);
LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM);HCURSOR LoadCursorW(HINSTANCE,const wchar_t*);HICON LoadIconW(HINSTANCE,const wchar_t*);
int MultiByteToWideChar(UINT,DWORD,const char*,int,wchar_t*,int);int WideCharToMultiByte(UINT,DWORD,const wchar_t*,int,char*,int,const char*,BOOL*);
int MessageBoxW(HWND,const wchar_t*,const wchar_t*,UINT);BOOL DestroyWindow(HWND);void PostQuitMessage(int);
BOOL SetProcessDPIAware();HDC GetDC(HWND);int ReleaseDC(HWND,HDC);int GetDeviceCaps(HDC,int);
BOOL GetMessageW(MSG*,HWND,UINT,UINT);BOOL IsDialogMessageW(HWND,MSG*);BOOL TranslateMessage(const MSG*);LRESULT DispatchMessageW(const MSG*);
HWND GetCapture();HWND SetCapture(HWND);BOOL ReleaseCapture();BOOL GetCursorPos(POINT*);BOOL ScreenToClient(HWND,POINT*);BOOL ClientToScreen(HWND,POINT*);
int MapWindowPoints(HWND,HWND,POINT*,UINT);BOOL PtInRect(const RECT*,POINT);UINT_PTR SetTimer(HWND,UINT_PTR,UINT,void*);BOOL KillTimer(HWND,UINT_PTR);
HDC BeginPaint(HWND,PAINTSTRUCT*);BOOL EndPaint(HWND,const PAINTSTRUCT*);int FillRect(HDC,const RECT*,HBRUSH);
INT_PTR DialogBoxIndirectParamW(HINSTANCE,const DLGTEMPLATE*,HWND,DLGPROC,LPARAM);BOOL EndDialog(HWND,INT_PTR);HWND SetFocus(HWND);
HMENU CreatePopupMenu();BOOL DestroyMenu(HMENU);BOOL AppendMenuW(HMENU,UINT,UINT_PTR,const wchar_t*);BOOL TrackPopupMenu(HMENU,UINT,int,int,int,HWND,const RECT*);
HRESULT CoInitializeEx(void*,DWORD);void CoUninitialize();void CoTaskMemFree(void*);
}

constexpr int COLOR_HIGHLIGHT=13;
using DWORD_PTR=uintptr_t;
extern "C" HBRUSH GetSysColorBrush(int);

#define GWL_STYLE (-16)
#define IDC_SIZEWE ((const wchar_t*)32644)
#define CS_DBLCLKS 8
#define DLGC_WANTARROWS 1
#define VK_LEFT 37
#define VK_RIGHT 39
#define VK_RETURN 13
#define VK_BACK 8
#define BN_CLICKED 0
#define WM_CANCELMODE 31
#define WM_SETCURSOR 32
#define WM_GETDLGCODE 135
#define WM_LBUTTONDBLCLK 515
using HGLOBAL=HANDLE;
#define GMEM_MOVEABLE 2
#define CF_UNICODETEXT 13
extern "C" {
int GetDlgCtrlID(HWND);HCURSOR SetCursor(HCURSOR);
BOOL OpenClipboard(HWND);BOOL CloseClipboard();BOOL EmptyClipboard();HANDLE SetClipboardData(UINT,HANDLE);
HGLOBAL GlobalAlloc(UINT,size_t);void* GlobalLock(HGLOBAL);BOOL GlobalUnlock(HGLOBAL);HGLOBAL GlobalFree(HGLOBAL);
}

// Additional declarations for the global workspace / modal UI tests.
#define BM_SETCHECK 0x00F1
#define BST_UNCHECKED 0
#define BS_AUTORADIOBUTTON 0x00000009UL
#define BS_MULTILINE 0x00002000UL
#define WS_GROUP 0x00020000UL
#define MF_BYCOMMAND 0x00000000UL
#define MF_ENABLED 0x00000000UL
#define MF_POPUP 0x00000010UL
HMENU CreateMenu();
BOOL SetMenu(HWND, HMENU);
BOOL DrawMenuBar(HWND);
UINT EnableMenuItem(HMENU, UINT, UINT);

// Theme service declarations; deliberately not an SDK implementation.
using ULONG_PTR=uintptr_t;using COLORREF=DWORD;using HKEY=void*;using LSTATUS=LONG;
struct HPEN__;using HPEN=HPEN__*;
struct SIZE{LONG cx,cy;};
struct TRACKMOUSEEVENT{DWORD cbSize,dwFlags;HWND hwndTrack;DWORD dwHoverTime;};
struct HIGHCONTRASTW{UINT cbSize;DWORD dwFlags;wchar_t* lpszDefaultScheme;};
struct MENUINFO{DWORD cbSize,fMask,dwStyle,cyMax;HBRUSH hbrBack;DWORD dwContextHelpID;ULONG_PTR dwMenuData;};
struct MENUITEMINFOW{UINT cbSize,fMask,fType,fState,wID;HMENU hSubMenu;HANDLE hbmpChecked,hbmpUnchecked;ULONG_PTR dwItemData;wchar_t* dwTypeData;UINT cch;HANDLE hbmpItem;};
struct MEASUREITEMSTRUCT{UINT CtlType,CtlID,itemID,itemWidth,itemHeight;ULONG_PTR itemData;};
struct DRAWITEMSTRUCT{UINT CtlType,CtlID,itemID,itemAction,itemState;HWND hwndItem;HDC hDC;RECT rcItem;ULONG_PTR itemData;};
#define RGB(r,g,b) (static_cast<COLORREF>((static_cast<BYTE>(r)) | (static_cast<WORD>(static_cast<BYTE>(g))<<8) | (static_cast<DWORD>(static_cast<BYTE>(b))<<16)))
#define BS_TYPEMASK 15
#define BS_RADIOBUTTON 4
#define BS_CHECKBOX 2
#define BS_3STATE 5
#define BS_AUTO3STATE 6
#define BST_PUSHED 4
#define BST_FOCUS 8
#define BM_GETSTATE 242
#define BM_SETSTATE 243
#define DEFAULT_GUI_FONT 17
#define NULL_PEN 8
#define TRANSPARENT 1
#define PS_SOLID 0
#define DT_LEFT 0
#define DT_CENTER 1
#define DT_RIGHT 2
#define DT_VCENTER 4
#define DT_WORDBREAK 16
#define DT_SINGLELINE 32
#define DT_CALCRECT 1024
#define DT_NOPREFIX 2048
#define DT_END_ELLIPSIS 32768
#define DT_HIDEPREFIX 1048576
#define WM_GETFONT 49
#define WM_SETTEXT 12
#define WM_SETFOCUS 7
#define WM_KILLFOCUS 8
#define WM_ENABLE 10
#define WM_ERASEBKGND 20
#define WM_SYSCOLORCHANGE 21
#define WM_SETTINGCHANGE 26
#define WM_DRAWITEM 43
#define WM_MEASUREITEM 44
#define WM_MENUCHAR 288
#define WM_CTLCOLORMSGBOX 306
#define WM_CTLCOLOREDIT 307
#define WM_CTLCOLORLISTBOX 308
#define WM_CTLCOLORBTN 309
#define WM_CTLCOLORDLG 310
#define WM_CTLCOLORSTATIC 312
#define WM_UPDATEUISTATE 295
#define WM_QUERYUISTATE 297
#define WM_PRINTCLIENT 792
#define WM_MOUSELEAVE 675
#define UISF_HIDEFOCUS 1
#define UISF_HIDEACCEL 2
#define TME_LEAVE 2
#define RDW_INVALIDATE 1
#define RDW_ERASE 4
#define RDW_ALLCHILDREN 128
#define RDW_FRAME 1024
#define COLOR_WINDOW 5
#define COLOR_WINDOWTEXT 8
#define COLOR_GRAYTEXT 17
#define SPI_GETHIGHCONTRAST 66
#define HCF_HIGHCONTRASTON 1
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_ACCESS_DENIED 5
#define RRF_RT_REG_DWORD 16
#define REG_DWORD 4
#define KEY_SET_VALUE 2
#define HKEY_CURRENT_USER ((HKEY)(uintptr_t)0x80000001UL)
#define ODT_MENU 1
#define ODS_SELECTED 1
#define ODS_GRAYED 2
#define ODS_DISABLED 4
#define ODS_CHECKED 8
#define ODS_HOTLIGHT 64
#define ODS_NOACCEL 256
#define MIIM_STATE 1
#define MIIM_FTYPE 256
#define MIIM_DATA 32
#define MIIM_STRING 64
#define MIM_BACKGROUND 2
#define MFT_OWNERDRAW 256
#define MFT_SEPARATOR 2048
#define MFS_DISABLED 3
#define MFS_GRAYED 3
#define MNC_IGNORE 0
#define MNC_EXECUTE 2
#define MAKELRESULT(lo,hi) ((LRESULT)(((DWORD)(WORD)(lo))|((DWORD)(WORD)(hi)<<16)))
using WNDENUMPROC=BOOL(CALLBACK*)(HWND,LPARAM);
extern "C" {
HBRUSH CreateSolidBrush(COLORREF);HPEN CreatePen(int,int,COLORREF);HGDIOBJ SelectObject(HDC,HGDIOBJ);HGDIOBJ GetStockObject(int);
int SaveDC(HDC);BOOL RestoreDC(HDC,int);COLORREF SetTextColor(HDC,COLORREF);COLORREF SetBkColor(HDC,COLORREF);int SetBkMode(HDC,int);
int DrawTextW(HDC,const wchar_t*,int,RECT*,UINT);BOOL DrawFocusRect(HDC,const RECT*);BOOL Ellipse(HDC,int,int,int,int);
BOOL MoveToEx(HDC,int,int,POINT*);BOOL LineTo(HDC,int,int);BOOL InflateRect(RECT*,int,int);COLORREF GetSysColor(int);
int GetClassNameW(HWND,wchar_t*,int);int lstrcmpiW(const wchar_t*,const wchar_t*);UINT GetDpiForWindow(HWND);
HWND GetFocus();BOOL TrackMouseEvent(TRACKMOUSEEVENT*);BOOL EnumChildWindows(HWND,WNDENUMPROC,LPARAM);
BOOL SystemParametersInfoW(UINT,UINT,void*,UINT);BOOL RedrawWindow(HWND,const RECT*,HANDLE,UINT);
HMENU GetMenu(HWND);BOOL IsMenu(HMENU);int GetMenuItemCount(HMENU);HMENU GetSubMenu(HMENU,int);
BOOL GetMenuInfo(HMENU,MENUINFO*);BOOL SetMenuInfo(HMENU,const MENUINFO*);
BOOL GetMenuItemInfoW(HMENU,UINT,BOOL,MENUITEMINFOW*);BOOL SetMenuItemInfoW(HMENU,UINT,BOOL,const MENUITEMINFOW*);
BOOL CheckMenuRadioItem(HMENU,UINT,UINT,UINT,UINT);
LSTATUS RegGetValueW(HKEY,const wchar_t*,const wchar_t*,DWORD,DWORD*,void*,DWORD*);
LSTATUS RegCreateKeyExW(HKEY,const wchar_t*,DWORD,wchar_t*,DWORD,DWORD,void*,HKEY*,DWORD*);
LSTATUS RegSetValueExW(HKEY,const wchar_t*,DWORD,DWORD,const BYTE*,DWORD);LSTATUS RegCloseKey(HKEY);
}
