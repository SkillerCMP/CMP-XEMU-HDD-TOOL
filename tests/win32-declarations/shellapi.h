#pragma once
#include "windows.h"
struct HDROP__;using HDROP=HDROP__*;
extern "C" {void DragAcceptFiles(HWND,BOOL);UINT DragQueryFileW(HDROP,UINT,wchar_t*,UINT);BOOL DragQueryPoint(HDROP,POINT*);void DragFinish(HDROP);HINSTANCE ShellExecuteW(HWND,const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*,int);}
