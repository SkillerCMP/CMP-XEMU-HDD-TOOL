#pragma once
#include "windows.h"
#define OFN_EXPLORER 0x80000
#define OFN_NOCHANGEDIR 8
#define OFN_PATHMUSTEXIST 0x800
#define OFN_ALLOWMULTISELECT 0x200
#define OFN_FILEMUSTEXIST 0x1000
#define OFN_OVERWRITEPROMPT 2
struct OPENFILENAMEW {DWORD lStructSize;HWND hwndOwner;HINSTANCE hInstance;const wchar_t* lpstrFilter;wchar_t* lpstrCustomFilter;DWORD nMaxCustFilter,nFilterIndex;wchar_t* lpstrFile;DWORD nMaxFile;wchar_t* lpstrFileTitle;DWORD nMaxFileTitle;const wchar_t* lpstrInitialDir;const wchar_t* lpstrTitle;DWORD Flags;WORD nFileOffset,nFileExtension;const wchar_t* lpstrDefExt;LPARAM lCustData;void* lpfnHook;const wchar_t* lpTemplateName;};
extern "C" {BOOL GetSaveFileNameW(OPENFILENAMEW*);BOOL GetOpenFileNameW(OPENFILENAMEW*);DWORD CommDlgExtendedError();}
