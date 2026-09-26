// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
constexpr UINT XhcSplitMove = WM_APP + 70;
constexpr UINT XhcSplitReset = WM_APP + 71;
HWND CreateXhcSplitter(HWND parent, int id, const wchar_t* caption);
