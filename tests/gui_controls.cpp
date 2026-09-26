// SPDX-License-Identifier: GPL-2.0-or-later
// Actual splitter/progress source against a deterministic Win32 message facade.
// Does not render Windows controls and is not an SDK/runtime acceptance test.
#include "../src/gui_splitter_win32.cpp"
#include "../src/gui_progress_win32.hpp"
#include <iostream>
#include <vector>
struct HWND__ {LONG_PTR data=0,style=0;HWND parent=nullptr;RECT rect{195,0,205,100};int id=7,pos=0,range=100;bool marquee=false;};
namespace {
HWND captured=nullptr;POINT cursor{200,30};HWND moving=nullptr;int moves=0,resets=0,invalid_positions=0,repaints=0;unsigned checks=0;
void ok(bool b,const char* what){++checks;xhc::require(b,std::string("GUI message test: ")+what);}
}
extern "C" {
BOOL GetWindowRect(HWND w,RECT* r){*r=w->rect;return TRUE;}BOOL GetClientRect(HWND,RECT* r){*r={0,0,10,100};return TRUE;}
HWND GetParent(HWND w){return w->parent;}BOOL ScreenToClient(HWND,POINT*){return TRUE;}BOOL GetCursorPos(POINT* p){*p=cursor;return TRUE;}
int GetDlgCtrlID(HWND w){return w->id;}LONG_PTR GetWindowLongPtrW(HWND w,int i){return i==GWL_STYLE?w->style:w->data;}
LONG_PTR SetWindowLongPtrW(HWND w,int i,LONG_PTR v){auto old=GetWindowLongPtrW(w,i);(i==GWL_STYLE?w->style:w->data)=v;return old;}
HWND GetCapture(){return captured;}HWND SetCapture(HWND w){auto old=captured;captured=w;return old;}BOOL ReleaseCapture(){auto old=captured;captured=nullptr;if(old)split_proc(old,WM_CAPTURECHANGED,0,0);return TRUE;}
HWND SetFocus(HWND w){return w;}HCURSOR LoadCursorW(HINSTANCE,const wchar_t*){return nullptr;}HCURSOR SetCursor(HCURSOR c){return c;}
LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM){return 0;}
HBRUSH GetSysColorBrush(int){return nullptr;}int FillRect(HDC,const RECT*,HBRUSH){return 1;}HDC BeginPaint(HWND,PAINTSTRUCT*){return nullptr;}BOOL EndPaint(HWND,const PAINTSTRUCT*){return TRUE;}
BOOL InvalidateRect(HWND,const RECT*,BOOL){++repaints;return TRUE;}BOOL UpdateWindow(HWND){return TRUE;}
LRESULT SendMessageW(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_CANCELMODE)return split_proc(w,msg,wp,lp);
    if(msg==XhcSplitMove){++moves;moving->rect.left=static_cast<LONG>(lp)-5;moving->rect.right=static_cast<LONG>(lp)+5;}
    else if(msg==XhcSplitReset)++resets;
    else if(msg==PBM_SETMARQUEE)w->marquee=wp!=0;
    else if(msg==PBM_SETPOS){if(w->style&PBS_MARQUEE)++invalid_positions;w->pos=static_cast<int>(wp);}
    else if(msg==PBM_SETRANGE32)w->range=static_cast<int>(lp);
    return 0;
}
HINSTANCE GetModuleHandleW(const wchar_t*){return nullptr;}WORD RegisterClassExW(const WNDCLASSEXW*){return 1;}DWORD GetLastError(){return 0;}
HWND CreateWindowExW(DWORD,const wchar_t*,const wchar_t*,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,void*){return nullptr;}
}
int main(){try{
    HWND__ parent,child,bar;child.parent=&parent;moving=&child;split_proc(&child,WM_CREATE,0,0);
    ok(child.data!=0,"splitter state allocation");ok(split_proc(&child,WM_GETDLGCODE,0,0)==DLGC_WANTARROWS,"keyboard arrows routed");
    cursor={203,30};split_proc(&child,WM_LBUTTONDOWN,0,0);ok(captured==&child,"mouse captured by actual divider");
    cursor={553,45};split_proc(&child,WM_MOUSEMOVE,0,0);ok(moves==1&&center(&child)==550,"drag remains delivered outside divider");
    split_proc(&child,WM_KEYDOWN,VK_ESCAPE,0);ok(center(&child)==200&&!captured,"Escape restores original divider and releases capture");
    cursor={200,30};split_proc(&child,WM_LBUTTONDOWN,0,0);cursor.x=250;split_proc(&child,WM_MOUSEMOVE,0,0);split_proc(&child,WM_LBUTTONUP,0,0);
    ok(center(&child)==250&&!captured,"released drop keeps chosen width");
    split_proc(&child,WM_KEYDOWN,VK_RIGHT,0);ok(center(&child)==260,"keyboard resize right");split_proc(&child,WM_KEYDOWN,VK_LEFT,0);ok(center(&child)==250,"keyboard resize left");
    split_proc(&child,WM_LBUTTONDOWN,0,0);split_proc(&child,WM_LBUTTONDBLCLK,0,0);ok(resets==1&&!captured,"double click reset releases capture");
    split_proc(&child,WM_LBUTTONDOWN,0,0);ReleaseCapture();int prior=moves;cursor.x=400;split_proc(&child,WM_MOUSEMOVE,0,0);ok(moves==prior,"lost capture stops drag");
    split_proc(&child,WM_NCDESTROY,0,0);ok(child.data==0,"splitter destruction frees state");
    bar.style=WS_VISIBLE|PBS_MARQUEE;bar.pos=35;bar.marquee=true;
    for(int cycle=0;cycle<20;++cycle){SetOperationProgress(&bar,false);ok(!bar.marquee&&!(bar.style&PBS_MARQUEE)&&bar.pos==0,"completion clears stopped green segment");ok(bar.style&WS_VISIBLE,"unrelated style retained");SetOperationProgress(&bar,true);ok(bar.marquee&&(bar.style&PBS_MARQUEE),"next operation restarts marquee");}
    SetOperationProgress(&bar,false);ok(invalid_positions==0,"never PBM_SETPOS while PBS_MARQUEE is set");ok(repaints>0,"repaint after reset");
    std::cout<<"PASS: "<<checks<<" production splitter/progress message checks (facade, not Windows runtime)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
