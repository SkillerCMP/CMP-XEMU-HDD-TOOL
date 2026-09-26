// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Include after the host's guarded Windows headers.
struct GuiWorkspace;
HWND CreateCsbPage(GuiWorkspace& shared);
bool CsbPageBusy(HWND page);
bool CsbPageCanClose(HWND page);
void CsbPageDpi(HWND page, unsigned dpi);

bool CsbPageDirty(HWND page);
void CsbPageInvalidate(HWND page);
enum class CsbUiAction { AddFiles, Clear, OpenOutput, OpenBackups, ResetPanes };
void CsbPageAction(HWND page, CsbUiAction action);

void CsbPageKeepOpen(HWND page);
