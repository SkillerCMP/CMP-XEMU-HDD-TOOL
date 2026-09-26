// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include "xhc/theme.hpp"

// UI-thread-only presentation service. Never touches disk sources or editor state.
void InitializeGuiTheme();
xhc::ui::Theme CurrentGuiTheme() noexcept;
// Returns false only if the preference could not be persisted; display still changes.
bool SelectGuiTheme(HWND root, xhc::ui::Theme theme);
void ApplyGuiTheme(HWND root);
void AttachGuiThemeMenu(HWND owner, HMENU menu);
void RefreshGuiThemeSystem(HWND root);
UINT TrackGuiThemePopup(HMENU menu, UINT flags, int x, int y, HWND owner);
void ShutdownGuiTheme();

HBRUSH GuiThemeAccentBrush();
