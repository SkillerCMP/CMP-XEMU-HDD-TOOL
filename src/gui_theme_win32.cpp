// SPDX-License-Identifier: GPL-2.0-or-later
// All drawing uses documented Win32/GDI APIs. Native input/accessibility remains intact.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "gui_theme_win32.hpp"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <cwctype>

namespace {
using xhc::ui::Theme;
constexpr UINT_PTR kSubclass = 0x58484354, kAnimationTimer = 0x58484341;
constexpr const wchar_t* kPreferenceKey = L"Software\\XEMU HDD Converter";
Theme selected = Theme::Default;
bool high_contrast = false, applying = false;
enum class Kind { Panel, Edit, Button, List, Header, Tabs, Progress, Splitter, Other };
struct WindowState {
    Kind kind = Kind::Other;
    bool hover = false, marquee = false;
    unsigned frame = 0;
};
struct Brush {
    HBRUSH handle = nullptr;
    Brush() = default;
    Brush(const Brush&) = delete;
    Brush& operator=(const Brush&) = delete;
    ~Brush() {
        if (handle)
            DeleteObject(handle);
    }
};
struct Brushes {
    Brush background, surface, field, border, accent, selection, hover;
};
std::array<Brushes, 3> brushes;
struct MenuItem {
    HMENU menu = nullptr;
    UINT index = 0, original_type = 0;
    ULONG_PTR original_data = 0;
    bool bar = false;
    std::wstring text;
};
struct MenuState {
    HMENU menu = nullptr;
    HWND owner = nullptr;
    HBRUSH original_background = nullptr;
    bool drawn = false;
    std::vector<std::unique_ptr<MenuItem>> items;
};
std::vector<std::unique_ptr<MenuState>> menus;
std::vector<HWND> windows;
COLORREF color(uint32_t rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
auto palette() { return xhc::ui::theme_palette(selected); }
bool custom() { return selected != Theme::Default && !high_contrast; }
HBRUSH sys_or_brush(Brush Brushes::* member, uint32_t rgb, int sys) {
    if (high_contrast)
        return GetSysColorBrush(sys);
    auto& b = brushes[static_cast<size_t>(selected)].*member;
    if (!b.handle)
        b.handle = CreateSolidBrush(color(rgb));
    return b.handle ? b.handle : GetSysColorBrush(sys);
}
HBRUSH background() { return sys_or_brush(&Brushes::background, palette().background, COLOR_WINDOW); }
HBRUSH surface() { return sys_or_brush(&Brushes::surface, palette().surface, COLOR_BTNFACE); }
HBRUSH field() { return sys_or_brush(&Brushes::field, palette().field, COLOR_WINDOW); }
HBRUSH border() { return sys_or_brush(&Brushes::border, palette().border, COLOR_3DSHADOW); }
HBRUSH accent() { return sys_or_brush(&Brushes::accent, palette().accent, COLOR_HIGHLIGHT); }
HBRUSH selection() { return sys_or_brush(&Brushes::selection, palette().selection, COLOR_HIGHLIGHT); }
HBRUSH hover() { return sys_or_brush(&Brushes::hover, palette().hover, COLOR_BTNFACE); }
COLORREF foreground(bool enabled = true) {
    return high_contrast ? GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT)
                         : color(enabled ? palette().text : palette().muted);
}
int scale(HWND w, int n) {
    const UINT dpi = GetDpiForWindow(w);
    return MulDiv(n, static_cast<int>(dpi ? dpi : 96), 96);
}
Kind kind_of(HWND w) {
    wchar_t name[128]{};
    GetClassNameW(w, name, 128);
    if (lstrcmpiW(name, L"Edit") == 0)
        return Kind::Edit;
    if (lstrcmpiW(name, L"Button") == 0)
        return Kind::Button;
    if (lstrcmpiW(name, WC_LISTVIEWW) == 0)
        return Kind::List;
    if (lstrcmpiW(name, WC_HEADERW) == 0)
        return Kind::Header;
    if (lstrcmpiW(name, WC_TABCONTROLW) == 0)
        return Kind::Tabs;
    if (lstrcmpiW(name, PROGRESS_CLASSW) == 0)
        return Kind::Progress;
    if (lstrcmpiW(name, L"XhcPaneSplitter") == 0)
        return Kind::Splitter;
    // Only our own pages/dialogs paint panel backgrounds; do not repaint shell pickers.
    if (lstrcmpiW(name, L"#32770") == 0 || lstrcmpiW(name, L"XemuHddConverterWindow") == 0 ||
        lstrcmpiW(name, L"XhcCsbPage") == 0 || lstrcmpiW(name, L"XhcHddDirectoryPage") == 0)
        return Kind::Panel;
    return Kind::Other;
}
void frame(HDC dc, RECT r, HBRUSH brush, int thickness = 1) {
    RECT edges[] = {{r.left, r.top, r.right, r.top + thickness},
                    {r.left, r.bottom - thickness, r.right, r.bottom},
                    {r.left, r.top, r.left + thickness, r.bottom},
                    {r.right - thickness, r.top, r.right, r.bottom}};
    for (const RECT& e : edges)
        FillRect(dc, &e, brush);
}
struct DcState {
    HDC dc;
    int saved;
    explicit DcState(HDC value) : dc(value), saved(SaveDC(value)) {}
    ~DcState() {
        if (saved)
            RestoreDC(dc, saved);
    }
};
void set_font(HWND w, HDC dc) {
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(w, WM_GETFONT, 0, 0));
    SelectObject(dc, font ? reinterpret_cast<HGDIOBJ>(font) : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
}
std::wstring caption(HWND w) {
    const int n = std::min(65535, GetWindowTextLengthW(w));
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    const int got = GetWindowTextW(w, s.data(), n + 1);
    s.resize(static_cast<size_t>(std::max(0, got)));
    return s;
}
void ellipse(HDC dc, RECT r, HBRUSH b) {
    auto oldb = SelectObject(dc, b), oldp = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, oldp);
    SelectObject(dc, oldb);
}
void draw_button(HWND w, HDC dc, RECT r, WindowState& s) {
    const bool enabled = IsWindowEnabled(w) != FALSE;
    const auto style = GetWindowLongPtrW(w, GWL_STYLE);
    const auto type = style & BS_TYPEMASK;
    const bool radio = type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON;
    const bool check = type == BS_AUTOCHECKBOX || type == BS_CHECKBOX || type == BS_AUTO3STATE || type == BS_3STATE;
    const auto state = SendMessageW(w, BM_GETSTATE, 0, 0);
    const bool pressed = (state & BST_PUSHED) != 0, focus = (state & BST_FOCUS) != 0;
    FillRect(dc, &r, radio || check ? background() : pressed ? selection() : s.hover && enabled ? hover() : surface());
    auto text = caption(w);
    RECT text_rect = r;
    if (radio || check) {
        const int size = scale(w, 16);
        RECT box{r.left + scale(w, 2), r.top + (r.bottom - r.top - size) / 2, r.left + scale(w, 2) + size,
                 r.top + (r.bottom - r.top - size) / 2 + size};
        const bool marked = SendMessageW(w, BM_GETCHECK, 0, 0) != BST_UNCHECKED;
        if (radio) {
            ellipse(dc, box, enabled && (focus || marked) ? accent() : border());
            InflateRect(&box, -scale(w, 2), -scale(w, 2));
            ellipse(dc, box, field());
            if (marked) {
                InflateRect(&box, -scale(w, 3), -scale(w, 3));
                ellipse(dc, box, enabled ? accent() : border());
            }
        } else {
            FillRect(dc, &box, marked && enabled ? accent() : field());
            frame(dc, box, enabled && focus ? accent() : border());
            if (marked) {
                HPEN pen = CreatePen(PS_SOLID, scale(w, 2), color(enabled ? palette().accent_text : palette().muted));
                auto old = SelectObject(dc, pen);
                const LONG mx = (box.left + box.right) / 2, my = (box.top + box.bottom) / 2;
                MoveToEx(dc, box.left + scale(w, 3), my, nullptr);
                LineTo(dc, mx - scale(w, 1), box.bottom - scale(w, 4));
                LineTo(dc, box.right - scale(w, 3), box.top + scale(w, 4));
                SelectObject(dc, old);
                DeleteObject(pen);
            }
        }
        text_rect.left += size + scale(w, 10);
    } else {
        frame(dc, r, enabled && (focus || type == BS_DEFPUSHBUTTON) ? accent() : border(), focus ? scale(w, 2) : 1);
        InflateRect(&text_rect, -scale(w, 6), 0);
    }
    SetTextColor(dc, foreground(enabled));
    UINT flags = DT_END_ELLIPSIS | ((radio || check) ? DT_LEFT : DT_CENTER);
    if (style & BS_MULTILINE)
        flags |= DT_WORDBREAK;
    else
        flags |= DT_SINGLELINE | DT_VCENTER;
    if (SendMessageW(w, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL)
        flags |= DT_HIDEPREFIX;
    if (style & BS_MULTILINE) {
        RECT measured = text_rect;
        DrawTextW(dc, text.c_str(), -1, &measured, flags | DT_CALCRECT);
        text_rect.top = std::max(
            text_rect.top, text_rect.top + (text_rect.bottom - text_rect.top - (measured.bottom - measured.top)) / 2);
    }
    DrawTextW(dc, text.c_str(), -1, &text_rect, flags);
    if (focus && !(SendMessageW(w, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        RECT f = radio || check ? text_rect : r;
        InflateRect(&f, -scale(w, 3), -scale(w, 3));
        DrawFocusRect(dc, &f);
    }
}
void draw_tabs(HWND w, HDC dc, RECT r) {
    FillRect(dc, &r, background());
    const int n = TabCtrl_GetItemCount(w), active = TabCtrl_GetCurSel(w);
    for (int i = 0; i < n; ++i) {
        RECT item{};
        if (!TabCtrl_GetItemRect(w, i, &item))
            continue;
        FillRect(dc, &item, i == active ? surface() : background());
        frame(dc, item, border());
        if (i == active) {
            RECT line{item.left, item.bottom - scale(w, 3), item.right, item.bottom};
            FillRect(dc, &line, accent());
        }
        wchar_t name[512]{};
        TCITEMW info{};
        info.mask = TCIF_TEXT;
        info.pszText = name;
        info.cchTextMax = 512;
        TabCtrl_GetItem(w, i, &info);
        SetTextColor(dc,
                     IsWindowEnabled(w) ? color(i == active ? palette().accent : palette().text) : foreground(false));
        RECT tr = item;
        InflateRect(&tr, -scale(w, 8), -scale(w, 3));
        DrawTextW(dc, name, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (i == active && GetFocus() == w && !(SendMessageW(w, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
            InflateRect(&tr, -1, -1);
            DrawFocusRect(dc, &tr);
        }
    }
}
void fill_list_empty_area(HWND list, HDC dc) {
    RECT client{};
    if (!GetClientRect(list, &client))
        return;
    LONG top = client.top;
    // The header is a separate child window and paints above the list client.
    // Filling from client.top is therefore safe for an empty report view.
    const int count = ListView_GetItemCount(list);
    if (count > 0) {
        RECT item{};
        if (ListView_GetItemRect(list, count - 1, &item, LVIR_BOUNDS))
            top = std::max(top, std::min(client.bottom, item.bottom));
    }
    if (top < client.bottom) {
        RECT empty{client.left, top, client.right, client.bottom};
        FillRect(dc, &empty, field());
    }
}
void draw_header(HWND w, HDC dc, RECT r) {
    FillRect(dc, &r, surface());
    const int n = Header_GetItemCount(w);
    for (int i = 0; i < n; ++i) {
        RECT item{};
        if (!Header_GetItemRect(w, i, &item))
            continue;
        wchar_t text[512]{};
        HDITEMW h{};
        h.mask = HDI_TEXT | HDI_FORMAT;
        h.pszText = text;
        h.cchTextMax = 512;
        Header_GetItem(w, i, &h);
        RECT line{item.right - 1, item.top, item.right, item.bottom};
        FillRect(dc, &line, border());
        RECT tr = item;
        InflateRect(&tr, -scale(w, 8), 0);
        SetTextColor(dc, foreground(IsWindowEnabled(GetParent(w)) != FALSE));
        UINT align = (h.fmt & HDF_RIGHT) ? DT_RIGHT : (h.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT;
        DrawTextW(dc, text, -1, &tr, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    RECT bottom{r.left, r.bottom - 1, r.right, r.bottom};
    FillRect(dc, &bottom, border());
}
void draw_progress(HWND w, HDC dc, RECT r, WindowState& s) {
    FillRect(dc, &r, field());
    frame(dc, r, border());
    InflateRect(&r, -1, -1);
    const int width = static_cast<int>(r.right - r.left);
    if (width <= 0 || r.bottom <= r.top)
        return;
    if (s.marquee) {
        const int length = std::max(8, width / 5), range = width + length;
        int x = static_cast<int>(s.frame % static_cast<unsigned>(range)) - length;
        RECT fill{r.left + std::max(0, x), r.top, r.left + std::min(width, x + length), r.bottom};
        if (fill.right > fill.left)
            FillRect(dc, &fill, accent());
    } else {
        const int pos = static_cast<int>(SendMessageW(w, PBM_GETPOS, 0, 0));
        PBRANGE range{};
        SendMessageW(w, PBM_GETRANGE, FALSE, reinterpret_cast<LPARAM>(&range));
        if (pos > range.iLow && range.iHigh > range.iLow) {
            RECT fill = r;
            fill.right = r.left + static_cast<LONG>(static_cast<int64_t>(width) *
                                                    std::clamp(pos - range.iLow, 0, range.iHigh - range.iLow) /
                                                    (range.iHigh - range.iLow));
            FillRect(dc, &fill, accent());
        }
    }
}
void paint(HWND w, HDC dc, WindowState& s) {
    if (!dc)
        return;
    DcState saved(dc);
    set_font(w, dc);
    RECT r{};
    GetClientRect(w, &r);
    switch (s.kind) {
    case Kind::Panel:
        FillRect(dc, &r, background());
        break;
    case Kind::Button:
        draw_button(w, dc, r, s);
        break;
    case Kind::Tabs:
        draw_tabs(w, dc, r);
        break;
    case Kind::Header:
        draw_header(w, dc, r);
        break;
    case Kind::Progress:
        draw_progress(w, dc, r, s);
        break;
    case Kind::Splitter: {
        FillRect(dc, &r, background());
        const LONG middle = (r.left + r.right) / 2;
        RECT line{middle - 1, r.top + 2, middle + 1, r.bottom - 2};
        FillRect(dc, &line, GetCapture() == w || GetFocus() == w ? accent() : border());
        break;
    }
    default:
        break;
    }
}
void update_animation(HWND w, WindowState& s) {
    KillTimer(w, kAnimationTimer);
    if (custom() && s.marquee)
        SetTimer(w, kAnimationTimer, 35, nullptr);
    InvalidateRect(w, nullptr, FALSE);
}
void frame_theme(HWND w) {
    if (GetWindowLongPtrW(w, GWL_STYLE) & WS_CHILD)
        return;
    BOOL dark = custom() ? TRUE : FALSE;
    // These documented attributes are optional on older Windows; failures leave OS framing.
    DwmSetWindowAttribute(w, 20, &dark, sizeof(dark));
    COLORREF caption_color = custom() ? color(palette().surface) : 0xFFFFFFFF;
    COLORREF text_color = custom() ? foreground() : 0xFFFFFFFF;
    COLORREF edge = custom() ? color(palette().border) : 0xFFFFFFFF;
    DwmSetWindowAttribute(w, 35, &caption_color, sizeof(caption_color));
    DwmSetWindowAttribute(w, 36, &text_color, sizeof(text_color));
    DwmSetWindowAttribute(w, 34, &edge, sizeof(edge));
}
MenuItem* find_item(ULONG_PTR data) {
    for (auto& m : menus)
        for (auto& i : m->items)
            if (reinterpret_cast<ULONG_PTR>(i.get()) == data)
                return i.get();
    return nullptr;
}
void menu_native(MenuState& m) {
    if (!m.drawn)
        return;
    if (IsMenu(m.menu)) {
        for (auto& item : m.items) {
            MENUITEMINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = MIIM_FTYPE | MIIM_DATA;
            info.fType = item->original_type;
            info.dwItemData = item->original_data;
            SetMenuItemInfoW(m.menu, item->index, TRUE, &info);
        }
        MENUINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = MIM_BACKGROUND;
        info.hbrBack = m.original_background;
        SetMenuInfo(m.menu, &info);
    }
    m.items.clear();
    m.drawn = false;
}
void update_menu(MenuState& m, bool bar) {
    if (!IsMenu(m.menu))
        return;
    if (!custom()) {
        menu_native(m);
        return;
    }
    if (!m.drawn) {
        MENUINFO old{};
        old.cbSize = sizeof(old);
        old.fMask = MIM_BACKGROUND;
        GetMenuInfo(m.menu, &old);
        m.original_background = old.hbrBack;
        m.drawn = true; // Partial owner-draw setup must also be restorable.
        int count = GetMenuItemCount(m.menu);
        for (int n = 0; n < count; ++n) {
            MENUITEMINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING;
            wchar_t label[1024]{};
            info.dwTypeData = label;
            info.cch = 1024;
            if (!GetMenuItemInfoW(m.menu, static_cast<UINT>(n), TRUE, &info) || (info.fType & MFT_OWNERDRAW))
                continue;
            auto item = std::make_unique<MenuItem>();
            item->menu = m.menu;
            item->index = static_cast<UINT>(n);
            item->bar = bar;
            item->text = label;
            item->original_type = info.fType;
            item->original_data = info.dwItemData;
            MENUITEMINFOW set{};
            set.cbSize = sizeof(set);
            set.fMask = MIIM_FTYPE | MIIM_DATA;
            set.fType = info.fType | MFT_OWNERDRAW;
            set.dwItemData = reinterpret_cast<ULONG_PTR>(item.get());
            const auto index = item->index;
            m.items.push_back(std::move(item));
            if (!SetMenuItemInfoW(m.menu, index, TRUE, &set))
                m.items.pop_back();
        }
        m.drawn = true;
    }
    MENUINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND;
    info.hbrBack = surface();
    SetMenuInfo(m.menu, &info);
}
void apply_menu(HWND owner, HMENU menu, bool bar) {
    if (!menu)
        return;
    MenuState* record = nullptr;
    for (auto& m : menus)
        if (m->menu == menu) {
            record = m.get();
            break;
        }
    if (!record) {
        auto m = std::make_unique<MenuState>();
        m->menu = menu;
        m->owner = owner;
        record = m.get();
        menus.push_back(std::move(m));
    }
    update_menu(*record, bar);
    const int n = GetMenuItemCount(menu);
    for (int i = 0; i < n; ++i)
        if (HMENU sub = GetSubMenu(menu, i))
            apply_menu(owner, sub, false);
}
void forget_menu(HMENU menu) {
    if (IsMenu(menu)) {
        int n = GetMenuItemCount(menu);
        for (int i = 0; i < n; ++i)
            if (HMENU sub = GetSubMenu(menu, i))
                forget_menu(sub);
    }
    menus.erase(std::remove_if(menus.begin(), menus.end(),
                               [&](auto& m) {
                                   if (m->menu != menu)
                                       return false;
                                   menu_native(*m);
                                   return true;
                               }),
                menus.end());
}
bool measure_menu(HWND w, MEASUREITEMSTRUCT* m) {
    if (!m || m->CtlType != ODT_MENU)
        return false;
    auto* item = find_item(m->itemData);
    if (!item)
        return false;
    HDC dc = GetDC(w);
    if (!dc)
        return false;
    {
        DcState saved(dc);
        set_font(w, dc);
        RECT r{};
        DrawTextW(dc, item->text.c_str(), -1, &r, DT_SINGLELINE | DT_CALCRECT);
        m->itemWidth = static_cast<UINT>(std::max<LONG>(0, r.right - r.left) + scale(w, item->bar ? 22 : 58));
        m->itemHeight = static_cast<UINT>((item->original_type & MFT_SEPARATOR)
                                              ? scale(w, 9)
                                              : std::max<LONG>(scale(w, 26), r.bottom - r.top + scale(w, 10)));
    }
    ReleaseDC(w, dc);
    return true;
}
bool draw_menu(HWND w, DRAWITEMSTRUCT* d) {
    if (!d || d->CtlType != ODT_MENU)
        return false;
    auto* item = find_item(d->itemData);
    if (!item)
        return false;
    HDC dc = d->hDC;
    DcState saved(dc);
    set_font(w, dc);
    RECT r = d->rcItem;
    const bool enabled = (d->itemState & (ODS_DISABLED | ODS_GRAYED)) == 0,
               active = (d->itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
    FillRect(dc, &r, active && enabled ? selection() : surface());
    if (item->original_type & MFT_SEPARATOR) {
        RECT line{r.left + scale(w, 8), (r.top + r.bottom) / 2, r.right - scale(w, 8), (r.top + r.bottom) / 2 + 1};
        FillRect(dc, &line, border());
        return true;
    }
    SetTextColor(dc, enabled ? color(active ? palette().selected_text : palette().text) : foreground(false));
    RECT tr = r;
    tr.left += scale(w, item->bar ? 11 : 30);
    tr.right -= scale(w, item->bar ? 11 : 26);
    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
    if (d->itemState & ODS_NOACCEL)
        flags |= DT_HIDEPREFIX;
    DrawTextW(dc, item->text.c_str(), -1, &tr, flags);
    if (d->itemState & ODS_CHECKED) {
        RECT dot{r.left + scale(w, 10), (r.top + r.bottom) / 2 - scale(w, 3), r.left + scale(w, 17),
                 (r.top + r.bottom) / 2 + scale(w, 4)};
        ellipse(dc, dot, enabled ? accent() : border());
    }
    if (GetSubMenu(item->menu, static_cast<int>(item->index)) && !item->bar) {
        RECT ar = r;
        ar.left = ar.right - scale(w, 20);
        DrawTextW(dc, L"\x203A", 1, &ar, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    return true;
}
LRESULT menu_key(WPARAM wp, LPARAM lp) {
    HMENU m = reinterpret_cast<HMENU>(lp);
    const wchar_t key = static_cast<wchar_t>(std::towupper(static_cast<wchar_t>(LOWORD(wp))));
    for (auto& group : menus)
        if (group->menu == m)
            for (auto& i : group->items) {
                MENUITEMINFOW info{};
                info.cbSize = sizeof(info);
                info.fMask = MIIM_STATE;
                if (!GetMenuItemInfoW(m, i->index, TRUE, &info) || (info.fState & (MFS_DISABLED | MFS_GRAYED)))
                    continue;
                for (size_t pos = 0; pos + 1 < i->text.size(); ++pos)
                    if (i->text[pos] == L'&') {
                        if (i->text[pos + 1] == L'&') {
                            ++pos;
                            continue;
                        }
                        if (std::towupper(i->text[pos + 1]) == static_cast<wint_t>(key))
                            return MAKELRESULT(i->index, MNC_EXECUTE);
                    }
            }
    return MAKELRESULT(0, MNC_IGNORE);
}
LRESULT CALLBACK theme_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data);
void apply_one(HWND w) {
    DWORD_PTR data = 0;
    WindowState* s = nullptr;
    if (GetWindowSubclass(w, theme_proc, kSubclass, &data))
        s = reinterpret_cast<WindowState*>(data);
    else {
        auto owned = std::make_unique<WindowState>();
        owned->kind = kind_of(w);
        owned->marquee = owned->kind == Kind::Progress && (GetWindowLongPtrW(w, GWL_STYLE) & PBS_MARQUEE) != 0;
        windows.push_back(w);
        if (!SetWindowSubclass(w, theme_proc, kSubclass, reinterpret_cast<DWORD_PTR>(owned.get()))) {
            windows.pop_back();
            return;
        }
        s = owned.release();
    }
    if (s->kind == Kind::List) {
        SetWindowTheme(w, custom() ? L"" : nullptr, custom() ? L"" : nullptr);
        ListView_SetBkColor(w, high_contrast ? GetSysColor(COLOR_WINDOW) : color(palette().field));
        ListView_SetTextBkColor(w, high_contrast ? GetSysColor(COLOR_WINDOW) : color(palette().field));
        ListView_SetTextColor(w, foreground(IsWindowEnabled(w) != FALSE));
        SendMessageW(w, LVM_SETINSERTMARKCOLOR, 0, color(palette().accent));
    }
    if (s->kind == Kind::Progress)
        update_animation(w, *s);
    frame_theme(w);
    InvalidateRect(w, nullptr, TRUE);
}
BOOL CALLBACK apply_child(HWND w, LPARAM) {
    try {
        apply_one(w);
        return TRUE;
    } catch (...) {
        return FALSE;
    }
}
LRESULT CALLBACK theme_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* s = reinterpret_cast<WindowState*>(data);
    if (msg == WM_NCDESTROY) {
        if (s->kind == Kind::Progress)
            KillTimer(w, kAnimationTimer);
        for (auto& m : menus)
            if (m->owner == w)
                menu_native(*m);
        menus.erase(std::remove_if(menus.begin(), menus.end(), [w](auto& m) { return m->owner == w; }), menus.end());
        windows.erase(std::remove(windows.begin(), windows.end(), w), windows.end());
        RemoveWindowSubclass(w, theme_proc, kSubclass);
        delete s;
        return DefSubclassProc(w, msg, wp, lp);
    }
    // Do not let a paint allocation/API failure escape a Win32 callback.
    try {
        if (msg == WM_SETTINGCHANGE || msg == WM_SYSCOLORCHANGE) {
            const LRESULT result = DefSubclassProc(w, msg, wp, lp);
            if (s->kind == Kind::Panel && !(GetWindowLongPtrW(w, GWL_STYLE) & WS_CHILD))
                RefreshGuiThemeSystem(w);
            return result;
        }
        if (msg == WM_ERASEBKGND && !high_contrast &&
            (s->kind == Kind::Panel || (custom() && (s->kind == Kind::Tabs || s->kind == Kind::Header ||
                                                     s->kind == Kind::Splitter || s->kind == Kind::List)))) {
            RECT r{};
            GetClientRect(w, &r);
            FillRect(reinterpret_cast<HDC>(wp), &r, s->kind == Kind::List ? field() : background());
            return TRUE;
        }
        if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORBTN || msg == WM_CTLCOLORLISTBOX ||
            msg == WM_CTLCOLORDLG) {
            if (high_contrast)
                return DefSubclassProc(w, msg, wp, lp);
            HWND child = reinterpret_cast<HWND>(lp);
            const bool input =
                msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX || (child && kind_of(child) == Kind::Edit);
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, foreground(!child || IsWindowEnabled(child) != FALSE));
            SetBkColor(dc, color(input ? palette().field : palette().background));
            return reinterpret_cast<LRESULT>(input ? field() : background());
        }
        if (msg == WM_DRAWITEM && custom() && draw_menu(w, reinterpret_cast<DRAWITEMSTRUCT*>(lp)))
            return TRUE;
        if (msg == WM_MEASUREITEM && custom() && measure_menu(w, reinterpret_cast<MEASUREITEMSTRUCT*>(lp)))
            return TRUE;
        if (msg == WM_MENUCHAR && custom())
            return menu_key(wp, lp);
        if (msg == WM_NOTIFY && custom() && lp) {
            auto* n = reinterpret_cast<NMHDR*>(lp);
            if (n->code == NM_CUSTOMDRAW && kind_of(n->hwndFrom) == Kind::List) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    // Keep an existing CSB insertion-marker postpaint callback armed.
                    const auto prior = DefSubclassProc(w, msg, wp, lp);
                    return prior | CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
                }
                if (draw->nmcd.dwDrawStage == CDDS_POSTPAINT) {
                    // Native list views may leave the area below the final row in the
                    // system window color. Repaint only that unused interior, then let
                    // the page's post-paint callback draw markers on top of it.
                    fill_list_empty_area(n->hwndFrom, draw->nmcd.hdc);
                    return DefSubclassProc(w, msg, wp, lp);
                }
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const bool enabled = IsWindowEnabled(n->hwndFrom) != FALSE;
                    const auto state =
                        ListView_GetItemState(n->hwndFrom, static_cast<int>(draw->nmcd.dwItemSpec), LVIS_SELECTED);
                    draw->clrText =
                        enabled ? color(state ? palette().selected_text : palette().text) : foreground(false);
                    draw->clrTextBk = color(state ? palette().selection : palette().field);
                    // Change only this paint notification, never the actual selected row.
                    draw->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_HOT);
                    return CDRF_NEWFONT;
                }
            }
        }
        if (s->kind == Kind::Progress && msg == PBM_SETMARQUEE) {
            const auto result = DefSubclassProc(w, msg, wp, lp);
            s->marquee = wp != FALSE;
            s->frame = 0;
            update_animation(w, *s);
            return result;
        }
        if (s->kind == Kind::Progress && msg == WM_TIMER && wp == kAnimationTimer) {
            if (custom() && s->marquee) {
                s->frame += static_cast<unsigned>(scale(w, 6));
                InvalidateRect(w, nullptr, FALSE);
            }
            return 0;
        }
        const bool paints =
            custom() && (s->kind == Kind::Panel || s->kind == Kind::Button || s->kind == Kind::Tabs ||
                         s->kind == Kind::Header || s->kind == Kind::Progress || s->kind == Kind::Splitter);
        if (paints && msg == WM_PAINT) {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(w, &ps);
            struct End {
                HWND window;
                PAINTSTRUCT* p;
                ~End() { EndPaint(window, p); }
            } end{w, &ps};
            paint(w, dc, *s);
            return 0;
        }
        if (paints && msg == WM_PRINTCLIENT) {
            paint(w, reinterpret_cast<HDC>(wp), *s);
            return 0;
        }
        if (s->kind == Kind::Button && msg == WM_MOUSEMOVE && !s->hover) {
            s->hover = true;
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, w, 0};
            TrackMouseEvent(&tracking);
            InvalidateRect(w, nullptr, FALSE);
        }
        if (s->kind == Kind::Button && msg == WM_MOUSELEAVE) {
            s->hover = false;
            InvalidateRect(w, nullptr, FALSE);
        }
        const auto result = DefSubclassProc(w, msg, wp, lp);
        if (paints && (msg == WM_ENABLE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == BM_SETSTATE ||
                       msg == BM_SETCHECK || msg == WM_UPDATEUISTATE || msg == WM_SETTEXT || msg == WM_SETFONT ||
                       msg == TCM_SETCURSEL || msg == PBM_SETPOS))
            InvalidateRect(w, nullptr, FALSE);
        return result;
    } catch (...) {
        return DefSubclassProc(w, msg, wp, lp);
    }
}
void read_contrast() {
    HIGHCONTRASTW info{};
    info.cbSize = sizeof(info);
    high_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(info), &info, 0) &&
                    ((info.dwFlags & HCF_HIGHCONTRASTON) != 0);
}
}
void InitializeGuiTheme() {
    selected = Theme::Default;
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kPreferenceKey, L"Theme", RRF_RT_REG_DWORD, nullptr, &value, &size) ==
            ERROR_SUCCESS &&
        size == sizeof(value))
        selected = xhc::ui::valid_theme(value);
    read_contrast();
}
xhc::ui::Theme CurrentGuiTheme() noexcept { return selected; }
void ApplyGuiTheme(HWND root) {
    if (!root || applying)
        return;
    applying = true;
    try {
        apply_one(root);
        EnumChildWindows(root, apply_child, 0);
        if (HMENU m = GetMenu(root))
            apply_menu(root, m, true);
        DrawMenuBar(root);
        RedrawWindow(root, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    } catch (...) {
        applying = false;
        throw;
    }
    applying = false;
}
bool SelectGuiTheme(HWND root, Theme theme) {
    selected = xhc::ui::valid_theme(static_cast<uint32_t>(theme));
    read_contrast();
    ApplyGuiTheme(root);
    const DWORD value = static_cast<DWORD>(selected);
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kPreferenceKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    const auto result =
        RegSetValueExW(key, L"Theme", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
void AttachGuiThemeMenu(HWND owner, HMENU menu) { apply_menu(owner, menu, true); }
void RefreshGuiThemeSystem(HWND root) {
    if (applying)
        return;
    read_contrast();
    ApplyGuiTheme(root);
}
UINT TrackGuiThemePopup(HMENU menu, UINT flags, int x, int y, HWND owner) {
    struct Restore {
        HMENU m;
        ~Restore() { forget_menu(m); }
    } restore{menu};
    apply_menu(owner, menu, false);
    return static_cast<UINT>(TrackPopupMenu(menu, flags, x, y, 0, owner, nullptr));
}
void ShutdownGuiTheme() {
    for (auto& m : menus)
        menu_native(*m);
    menus.clear();
    for (HWND w : windows) {
        DWORD_PTR data = 0;
        if (GetWindowSubclass(w, theme_proc, kSubclass, &data)) {
            KillTimer(w, kAnimationTimer);
            RemoveWindowSubclass(w, theme_proc, kSubclass);
            delete reinterpret_cast<WindowState*>(data);
        }
    }
    windows.clear();
    for (auto& set : brushes)
        for (Brush* b :
             {&set.background, &set.surface, &set.field, &set.border, &set.accent, &set.selection, &set.hover}) {
            if (b->handle)
                DeleteObject(b->handle);
            b->handle = nullptr;
        }
}

HBRUSH GuiThemeAccentBrush() { return accent(); }
