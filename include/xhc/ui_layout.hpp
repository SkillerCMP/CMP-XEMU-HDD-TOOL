// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
namespace xhc::ui {
// Widths use 96-DPI logical units. Requests survive a temporary narrow window.
struct CsbPanes {
    int left, middle, middle_x, songs_x, songs_width, splitter1_x, splitter2_x;
};
inline CsbPanes csb_panes(int width, int wanted_left, int wanted_middle) {
    width = std::max(width, 820);
    const int left = std::clamp(wanted_left, 190, width - 245 - 300 - 46);
    const int middle = std::clamp(wanted_middle, 245, width - left - 300 - 46);
    return {left, middle, left + 24, left + middle + 34, width - left - middle - 46, left + 14, left + middle + 24};
}
}
