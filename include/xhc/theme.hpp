// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace xhc::ui {
enum class Theme : uint32_t { Default = 0, Dark = 1, Xbox = 2 };
// Colors use RGB notation, independent of Win32 COLORREF byte order.
struct ThemePalette {
    uint32_t background, surface, field, text, muted, border;
    uint32_t accent, accent_text, selection, selected_text, hover;
};
constexpr Theme valid_theme(uint32_t value) noexcept { return value <= 2 ? static_cast<Theme>(value) : Theme::Default; }
constexpr const wchar_t* theme_name(Theme theme) noexcept {
    switch (theme) {
    case Theme::Dark:
        return L"Dark Mode";
    case Theme::Xbox:
        return L"Xbox Mode";
    default:
        return L"Default (White)";
    }
}
constexpr ThemePalette theme_palette(Theme theme) noexcept {
    switch (theme) {
    case Theme::Dark:
        return {0x191B1F, 0x26292F, 0x202329, 0xEFF1F5, 0xAFB6C2, 0x616976,
                0x88B4FF, 0x10233F, 0x30496C, 0xF5F8FF, 0x353B46};
    case Theme::Xbox:
        return {0x101710, 0x1B281C, 0x142015, 0xEFF8EB, 0xADBEA7, 0x54734C,
                0x9BF00B, 0x112000, 0x285624, 0xF4FFE8, 0x2A3D26};
    default:
        return {0xFFFFFF, 0xF2F4F7, 0xFFFFFF, 0x20242A, 0x5B6572, 0x8D98A6,
                0x1761B0, 0xFFFFFF, 0xD9E9FC, 0x123859, 0xE6EDF5};
    }
}
} // namespace xhc::ui
