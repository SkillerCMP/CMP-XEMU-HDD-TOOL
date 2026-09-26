// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
struct GuiWorkspace;
HWND CreateHddPage(GuiWorkspace& shared);
bool HddPageBusy(HWND page);
bool HddPageCanClose(HWND page);
void HddPageDpi(HWND page, unsigned dpi);

bool HddPageDirty(HWND page);
void HddPageInvalidate(HWND page);
void HddPageOpenOutput(HWND page, bool backups);

void HddPageKeepOpen(HWND page);
