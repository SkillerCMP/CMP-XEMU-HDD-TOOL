// SPDX-License-Identifier: GPL-2.0-or-later
// Real shell/event/state code; dialogs use the same scripted API; child page adapters are simulated.
#define main modal_suite_not_run
#include "gui_workspace.cpp"
#undef main
#include "../src/gui_win32.cpp"
#include <set>
namespace {
std::map<HWND,bool> dirty_pages;
std::map<HWND,int> invalidated;
std::map<HWND,bool> shown;
std::map<HWND,UINT> menu_flags;
int reset_count=0,keep_open_count=0;bool csb_can_close=true,hdd_can_close=true;
}
extern "C" {
BOOL PostMessageW(HWND,UINT,WPARAM,LPARAM){return TRUE;}
BOOL ShowWindow(HWND w,int n){shown[w]=n!=SW_HIDE;return TRUE;}BOOL UpdateWindow(HWND){return TRUE;}
BOOL InvalidateRect(HWND,const RECT*,BOOL){return TRUE;}
HFONT CreateFontW(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,const wchar_t*){return reinterpret_cast<HFONT>(1);}
BOOL DeleteObject(HGDIOBJ){return TRUE;}
WORD RegisterClassExW(const WNDCLASSEXW*){return 1;}DWORD GetLastError(){return 0;}
HWND GetParent(HWND w){return w?w->parent:nullptr;}
LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM){return 0;}
HCURSOR LoadCursorW(HINSTANCE,const wchar_t*){return nullptr;}HICON LoadIconW(HINSTANCE,const wchar_t*){return nullptr;}
int MultiByteToWideChar(UINT,DWORD,const char* s,int n,wchar_t* out,int){if(out)for(int i=0;i<n;++i)out[i]=static_cast<unsigned char>(s[i]);return n;}
BOOL DestroyWindow(HWND){return TRUE;}void PostQuitMessage(int){}
BOOL SetProcessDPIAware(){return TRUE;}HDC GetDC(HWND){return nullptr;}int ReleaseDC(HWND,HDC){return 0;}int GetDeviceCaps(HDC,int){return 96;}
BOOL GetMessageW(MSG*,HWND,UINT,UINT){return FALSE;}BOOL IsDialogMessageW(HWND,MSG*){return FALSE;}BOOL TranslateMessage(const MSG*){return TRUE;}LRESULT DispatchMessageW(const MSG*){return 0;}
HMENU CreatePopupMenu(){return reinterpret_cast<HMENU>(1);}BOOL DestroyMenu(HMENU){return TRUE;}BOOL AppendMenuW(HMENU,UINT,UINT_PTR,const wchar_t*){return TRUE;}
HRESULT CoInitializeEx(void*,DWORD){return 0;}void CoUninitialize(){}
BOOL CheckMenuRadioItem(HMENU,UINT,UINT,UINT,UINT){return TRUE;}
BOOL InitCommonControlsEx(const INITCOMMONCONTROLSEX*){return TRUE;}
void DragAcceptFiles(HWND,BOOL){}void DragFinish(HDROP){}
HINSTANCE ShellExecuteW(HWND,const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*,int){return reinterpret_cast<HINSTANCE>(33);}
}
HMENU CreateMenu(){return reinterpret_cast<HMENU>(1);}BOOL SetMenu(HWND,HMENU){return TRUE;}BOOL DrawMenuBar(HWND){return TRUE;}
UINT EnableMenuItem(HMENU,UINT id,UINT flags){menu_flags[reinterpret_cast<HWND>(static_cast<UINT_PTR>(id))]=flags;return 0;}
HWND CreateCsbPage(GuiWorkspace& s){return make(s.owner,7001);}HWND CreateHddPage(GuiWorkspace& s){return make(s.owner,7002);}
bool CsbPageBusy(HWND){return false;}bool HddPageBusy(HWND){return false;}
bool CsbPageCanClose(HWND){return csb_can_close;}bool HddPageCanClose(HWND){return hdd_can_close;}
void CsbPageKeepOpen(HWND){++keep_open_count;}void HddPageKeepOpen(HWND){++keep_open_count;}
void CsbPageDpi(HWND,unsigned){}void HddPageDpi(HWND,unsigned){}
bool CsbPageDirty(HWND w){return dirty_pages[w];}bool HddPageDirty(HWND w){return dirty_pages[w];}
void CsbPageInvalidate(HWND w){if(w){++invalidated[w];dirty_pages[w]=false;}}void HddPageInvalidate(HWND w){if(w){++invalidated[w];dirty_pages[w]=false;}}
void CsbPageAction(HWND,CsbUiAction a){if(a==CsbUiAction::ResetPanes)++reset_count;}void HddPageOpenOutput(HWND,bool){}
int main(){try{
    App app;app.window=make(nullptr);app.window->rect={0,0,1280,820};SetWindowLongPtrW(app.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&app));
    send_override=[&](HWND w,UINT m,WPARAM p,LPARAM l)->LRESULT{
        if(m==TCM_GETCURSEL)return w->check;if(m==TCM_SETCURSEL){w->check=static_cast<int>(p);return 0;}
        if(m==WorkspaceChangedMessage||m==SnapshotQuestionMessage)return window_proc(w,m,p,l);return 0;
    };
    create_controls(app);
    check(app.shared.state.settings().qemu_img==xhc::executable_directory()/"tools"/"qemu-img.exe","global qemu default");
    check(app.shared.state.settings().ffmpeg==xhc::executable_directory()/"tools"/"ffmpeg.exe","global ffmpeg default");
    check(GetDlgItem(app.csb_page,SourceEdit)==nullptr&&GetDlgItem(app.hdd_page,SourceEdit)==nullptr,"source belongs to root shell only");
    for(int first:{SourceQcow,OutputQcow,ConvertRadio})check(control(app,first)->style&WS_GROUP,"radio groups start separately");
    check(control(app,SourceEdit)->style&WS_GROUP,"source path ends source radio group");check(control(app,OutputEdit)->style&WS_GROUP,"output path ends output radio group");
    auto select_tab=[&](int n){app.tabs->check=n;NMHDR h{app.tabs,kTabs,TCN_SELCHANGE};window_proc(app.window,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&h));};
    for(unsigned dpi:{96u,120u,144u,192u}) {
        app.dpi=dpi;app.window->rect={0,0,MulDiv(1120,dpi,96),MulDiv(738,dpi,96)};set_font(app);
        for(int n=0;n<3;++n) {
            select_tab(n);RECT root{};GetClientRect(app.window,&root);
            for(int id=SourceLabel;id<=OutputBrowse;++id){auto r=control(app,id)->rect;check(r.left>=0&&r.top>=0&&r.right<=root.right&&r.bottom<=app.tabs->rect.top,"header controls fit and stay above tabs at DPI");}
            check(app.csb_page->rect.top>=app.tabs->rect.bottom&&app.hdd_page->rect.top>=app.tabs->rect.bottom,"page content stays below tabs");
            check(shown[app.csb_page]==(n==1)&&shown[app.hdd_page]==(n==2),"only active page visible");
            check(shown[control(app,ConvertRadio)]==(n==0),"Convert/Verify appear only in converter tab");
        }
    }
    app.dpi=96;app.window->rect={0,0,1280,820};set_font(app);select_tab(0);
    change_source(app,"one.qcow2",xhc::ui::Format::Qcow2);source_fields(app);app.shared.state.output("new.qcow2",xhc::ui::Format::Qcow2);output_fields(app);
    for(unsigned bits=1;bits<4;++bits) {
        dirty_pages[app.csb_page]=(bits&1)!=0;dirty_pages[app.hdd_page]=(bits&2)!=0;
        int cs=invalidated[app.csb_page],hd=invalidated[app.hdd_page];auto rev=app.shared.state.revision();message_answer=IDNO;
        check(!change_source(app,"two.qcow2",xhc::ui::Format::Qcow2),"Keep Editing rejects source switch");
        check(app.shared.state.revision()==rev&&app.shared.state.settings().source=="one.qcow2","Keep Editing retains source and revision");
        check(invalidated[app.csb_page]==cs&&invalidated[app.hdd_page]==hd,"no partial editor discard on refusal");
        check(dirty_pages[app.csb_page]==((bits&1)!=0)&&dirty_pages[app.hdd_page]==((bits&2)!=0),"both pending editors retained");
        if(bits==3)check(last_message_text.find(L"Custom Soundtrack Builder and HDD Directory")!=std::wstring::npos,"one combined prompt names both editors");
    }
    message_answer=IDYES;int cs=invalidated[app.csb_page],hd=invalidated[app.hdd_page];
    check(change_source(app,"two.qcow2",xhc::ui::Format::Qcow2),"explicit discard accepts source");source_fields(app);
    check(invalidated[app.csb_page]==cs+1&&invalidated[app.hdd_page]==hd+1&&!dirty_pages[app.csb_page]&&!dirty_pages[app.hdd_page],"both caches invalidated together after commit");
    check(app.shared.state.settings().output=="new.qcow2","source switch retains output");
    window_proc(app.window,WM_COMMAND,VerifyRadio,0);check(app.verify&&!IsWindowEnabled(control(app,OutputEdit)),"Verify disables global output on converter tab");
    select_tab(1);check(IsWindowEnabled(control(app,OutputEdit)),"CSB can use output when converter is in Verify mode");
    select_tab(2);check(IsWindowEnabled(control(app,OutputEdit)),"HDD can use same output");
    select_tab(0);window_proc(app.window,WM_COMMAND,ConvertRadio,0);check(IsWindowEnabled(control(app,OutputEdit)),"Convert restores prior output");
    dirty_pages[app.csb_page]=true;dirty_pages[app.hdd_page]=true;
    const auto theme_revision=app.shared.state.revision();
    const int cs_before_theme=invalidated[app.csb_page],hd_before_theme=invalidated[app.hdd_page];
    auto ticket=BeginWorkspaceJob(app.shared);
    for(int id:{ThemeDarkMenu,ThemeXboxMenu,ThemeDefaultMenu}) {
        window_proc(app.window,WM_COMMAND,id,0);
        check(CurrentGuiTheme()==static_cast<xhc::ui::Theme>(id-ThemeDefaultMenu),"global theme changes during a busy job");
        check(app.shared.state.active_job()==ticket.id&&app.shared.state.revision()==theme_revision,"theme does not alter operation or settings");
        check(dirty_pages[app.csb_page]&&dirty_pages[app.hdd_page],"theme retains both dirty editors");
        check(invalidated[app.csb_page]==cs_before_theme&&invalidated[app.hdd_page]==hd_before_theme,"theme does not invalidate either catalog");
    }
    dirty_pages[app.csb_page]=dirty_pages[app.hdd_page]=false;
    check(!IsWindowEnabled(control(app,SourceEdit))&&!IsWindowEnabled(control(app,OutputEdit))&&!IsWindowEnabled(app.tabs),"background task locks global header and tabs");
    NMHDR change{app.tabs,kTabs,TCN_SELCHANGING};check(window_proc(app.window,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&change))==TRUE,"busy tab switch refused");
    auto src=app.shared.state.settings().source;window_proc(app.window,WM_COMMAND,SourceFolder,0);check(app.shared.state.settings().source==src,"busy programmatic source command refused");
    FinishWorkspaceJob(app.shared,ticket.id+1);check(!IsWindowEnabled(control(app,SourceEdit)),"stale completion leaves header locked");
    FinishWorkspaceJob(app.shared,ticket.id);check(IsWindowEnabled(control(app,SourceEdit))&&IsWindowEnabled(app.tabs),"completion unlocks header and tabs");
    ticket=BeginWorkspaceJob(app.shared);app.job=ticket.id;app.busy=true;
    auto done=std::make_unique<Completion>();done->success=true;done->result.destination="saved.qcow2";done->message=L"done";
    window_proc(app.window,kDoneMessage,0,reinterpret_cast<LPARAM>(done.release()));
    check(app.shared.state.settings().source==src&&app.shared.state.settings().output=="new.qcow2","successful save never silently switches source/output");
    check(!app.shared.state.busy()&&!app.busy&&app.last.destination=="saved.qcow2","completion retains separate output result");
    window_proc(app.window,WM_COMMAND,SourceFolder,0);check(app.shared.state.settings().source_format==xhc::ui::Format::Folder&&app.shared.state.settings().source.empty(),"file-to-folder source choice clears incompatible path");
    check(control(app,SourceFolder)->check==BST_CHECKED&&control(app,SourceQcow)->check==BST_UNCHECKED,"source radios synchronized");
    window_proc(app.window,WM_COMMAND,OutputRaw,0);check(app.shared.state.settings().output_format==xhc::ui::Format::Raw&&app.shared.state.settings().output.empty(),"new output type cannot silently reuse old extension");
    window_proc(app.window,WM_COMMAND,ResetPanesMenu,0);check(reset_count==1,"global Options forwards CSB pane reset");
    send_override={};csb_can_close=true;hdd_can_close=false;keep_open_count=0;
    window_proc(app.window,WM_CLOSE,0,0);
    check(keep_open_count==2,"one dirty tab veto clears both old idle close permissions");
    auto closing_job=BeginWorkspaceJob(app.shared);keep_open_count=0;csb_can_close=false;
    window_proc(app.window,WM_CLOSE,0,0);
    check(keep_open_count==0,"active cancellation is not disarmed before worker completes");
    FinishWorkspaceJob(app.shared,closing_job.id);csb_can_close=hdd_can_close=true;
    std::cout<<"PASS: "<<checks<<" production shell/header/state event checks (simulated child adapters, not Windows rendering)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
