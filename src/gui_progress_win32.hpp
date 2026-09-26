// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <commctrl.h>
// PBM_SETMARQUEE(FALSE) only stops animation; explicitly return to an empty bar.
inline void SetOperationProgress(HWND bar, bool busy) {
    SendMessageW(bar, PBM_SETMARQUEE, FALSE, 0);
    LONG_PTR style = GetWindowLongPtrW(bar, GWL_STYLE) & ~static_cast<LONG_PTR>(PBS_MARQUEE);
    SetWindowLongPtrW(bar, GWL_STYLE, style);
    SendMessageW(bar, PBM_SETRANGE32, 0, 100);
    SendMessageW(bar, PBM_SETPOS, 0, 0);
    if (busy) {
        SetWindowLongPtrW(bar, GWL_STYLE, style | PBS_MARQUEE);
        SendMessageW(bar, PBM_SETMARQUEE, TRUE, 35);
    }
    InvalidateRect(bar, nullptr, TRUE);
    UpdateWindow(bar);
}
