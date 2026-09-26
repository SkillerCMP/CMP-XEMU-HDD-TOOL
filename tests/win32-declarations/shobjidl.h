#pragma once
#include "windows.h"
using FILEOPENDIALOGOPTIONS=DWORD;
constexpr DWORD FOS_PICKFOLDERS=0x20,FOS_FORCEFILESYSTEM=0x40,FOS_PATHMUSTEXIST=0x800,FOS_NOCHANGEDIR=8;
constexpr int CLSID_FileOpenDialog=1,IID_IFileOpenDialog=1,SIGDN_FILESYSPATH=1;
struct IShellItem {virtual HRESULT GetDisplayName(int,PWSTR*)=0;virtual DWORD Release()=0;};
struct IFileOpenDialog {virtual HRESULT GetOptions(FILEOPENDIALOGOPTIONS*)=0;virtual HRESULT SetOptions(FILEOPENDIALOGOPTIONS)=0;virtual HRESULT SetTitle(const wchar_t*)=0;virtual HRESULT Show(HWND)=0;virtual HRESULT GetResult(IShellItem**)=0;virtual DWORD Release()=0;};
extern "C" HRESULT CoCreateInstance(int,void*,DWORD,int,void**);
