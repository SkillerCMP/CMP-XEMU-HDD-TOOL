// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "gui_splitter_win32.hpp"
#include "xhc/common.hpp"
namespace {
struct Splitter {
    bool dragging = false;
    int origin = 0, offset = 0;
};
int center(HWND w) {
    RECT r{};
    GetWindowRect(w, &r);
    POINT p{(r.left + r.right) / 2, r.top};
    ScreenToClient(GetParent(w), &p);
    return static_cast<int>(p.x);
}
void move_to(HWND w, int x) { SendMessageW(GetParent(w), XhcSplitMove, static_cast<WPARAM>(GetDlgCtrlID(w)), x); }
LRESULT CALLBACK split_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<Splitter*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        s = new (std::nothrow) Splitter;
        if (!s)
            return -1;
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        return 0;
    }
    if (!s)
        return DefWindowProcW(w, msg, wp, lp);
    switch (msg) {
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return TRUE;
    case WM_LBUTTONDOWN: {
        POINT p{};
        GetCursorPos(&p);
        ScreenToClient(GetParent(w), &p);
        s->origin = center(w);
        s->offset = static_cast<int>(p.x) - s->origin;
        s->dragging = true;
        SetFocus(w);
        SetCapture(w);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (s->dragging && GetCapture() == w) {
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(GetParent(w), &p);
            move_to(w, static_cast<int>(p.x) - s->offset);
        }
        return 0;
    case WM_LBUTTONUP:
        s->dragging = false;
        if (GetCapture() == w)
            ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        s->dragging = false;
        return 0;
    case WM_CANCELMODE:
        if (s->dragging) {
            move_to(w, s->origin);
            s->dragging = false;
        }
        if (GetCapture() == w)
            ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK:
        s->dragging = false;
        if (GetCapture() == w)
            ReleaseCapture();
        SendMessageW(GetParent(w), XhcSplitReset, static_cast<WPARAM>(GetDlgCtrlID(w)), 0);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_KEYDOWN:
        if (wp == VK_LEFT || wp == VK_RIGHT) {
            move_to(w, center(w) + (wp == VK_RIGHT ? 10 : -10));
            return 0;
        }
        if (wp == VK_ESCAPE) {
            SendMessageW(w, WM_CANCELMODE, 0, 0);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(w, &paint);
        RECT r{};
        GetClientRect(w, &r);
        FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
        LONG middle = (r.left + r.right) / 2;
        RECT line{middle - 1, r.top + 2, middle + 1, r.bottom - 2};
        FillRect(dc, &line, GetSysColorBrush(COLOR_3DSHADOW));
        EndPaint(w, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(w, GWLP_USERDATA, 0);
        delete s;
        break;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
}
HWND CreateXhcSplitter(HWND parent, int id, const wchar_t* caption) {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"XhcPaneSplitter";
    cls.lpfnWndProc = split_proc;
    cls.style = CS_DBLCLKS;
    cls.hCursor = LoadCursorW(nullptr, IDC_SIZEWE);
    cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    xhc::require(RegisterClassExW(&cls) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                 "Cannot register pane divider.");
    HWND w = CreateWindowExW(0, cls.lpszClassName, caption, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 100, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), cls.hInstance, nullptr);
    xhc::require(w != nullptr, "Cannot create pane divider.");
    return w;
}
