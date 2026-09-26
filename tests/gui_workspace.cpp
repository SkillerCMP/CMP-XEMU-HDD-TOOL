// SPDX-License-Identifier: GPL-2.0-or-later
// Production modal procedures using scripted Win32 messages, not native rendering.
#include "../src/gui_workspace_win32.cpp"
#include "theme_adapter.hpp"
#include <iostream>
#include <map>
#include <cstring>
#include <memory>
struct HWND__ {
    LONG_PTR data=0;int id=0,check=0;bool enabled=true;DWORD style=0;
    RECT rect{0,0,750,430};std::wstring text;HWND parent=nullptr;
    std::map<int,HWND> children;
};
namespace {
std::vector<std::unique_ptr<HWND__>> windows;
std::function<void(HWND,DLGPROC)> scenario;
std::function<LRESULT(HWND,UINT,WPARAM,LPARAM)> send_override;
INT_PTR ended=-999;int message_answer=IDNO,message_calls=0;UINT last_message_flags=0;
std::wstring last_message_text;
std::wstring picked_file,last_filter,picked_folder=L"/tmp";bool save_picker=false;unsigned checks=0;
void check(bool b,const char* why){++checks;xhc::require(b,why);}
HWND make(HWND parent,int id=0) {windows.push_back(std::make_unique<HWND__>());auto w=windows.back().get();w->parent=parent;w->id=id;if(parent)parent->children[id]=w;return w;}
void click_check(HWND w,DLGPROC proc,int id,bool checked){SendMessageW(GetDlgItem(w,id),BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);proc(w,WM_COMMAND,static_cast<WPARAM>(id),0);}
void bounds(HWND w) {
    RECT c{};GetClientRect(w,&c);for(const auto& pair:w->children){auto r=pair.second->rect;check(r.left>=0&&r.top>=0&&r.right<=c.right&&r.bottom<=c.bottom,"dialog control within DPI client bounds");}
}
struct ShellItem final:IShellItem {
    HRESULT GetDisplayName(int,PWSTR* p) override {const size_t n=(picked_folder.size()+1)*sizeof(wchar_t);*p=static_cast<PWSTR>(malloc(n));std::memcpy(*p,picked_folder.c_str(),n);return 0;}
    DWORD Release() override {return 0;}
} shell_item;
struct FolderDialog final:IFileOpenDialog {
    FILEOPENDIALOGOPTIONS options=0;
    HRESULT GetOptions(FILEOPENDIALOGOPTIONS* p) override{*p=options;return 0;}
    HRESULT SetOptions(FILEOPENDIALOGOPTIONS p) override{options=p;return 0;}
    HRESULT SetTitle(const wchar_t*) override{return 0;}HRESULT Show(HWND) override{return 0;}
    HRESULT GetResult(IShellItem** p) override{*p=&shell_item;return 0;}DWORD Release() override{return 0;}
} folder_dialog;
}
extern "C" {
HWND GetDlgItem(HWND w,int id){if(!w)return nullptr;auto i=w->children.find(id);return i==w->children.end()?nullptr:i->second;}
int GetWindowTextLengthW(HWND w){return w?static_cast<int>(w->text.size()):0;}
int GetWindowTextW(HWND w,wchar_t* s,int n){if(!w||n<=0)return 0;int size=std::min(n-1,static_cast<int>(w->text.size()));std::copy_n(w->text.data(),size,s);s[size]=0;return size;}
BOOL SetWindowTextW(HWND w,const wchar_t* s){if(!w)return FALSE;w->text=s?s:L"";return TRUE;}
BOOL EnableWindow(HWND w,BOOL on){if(!w)return FALSE;w->enabled=on;return TRUE;}BOOL IsWindowEnabled(HWND w){return w&&w->enabled;}
LONG_PTR GetWindowLongPtrW(HWND w,int){return w?w->data:0;}LONG_PTR SetWindowLongPtrW(HWND w,int,LONG_PTR p){auto old=w->data;w->data=p;return old;}
LRESULT SendMessageW(HWND w,UINT m,WPARAM p,LPARAM l){if(m==BM_SETCHECK){w->check=static_cast<int>(p);return 0;}if(m==BM_GETCHECK)return w?w->check:0;if(send_override)return send_override(w,m,p,l);return 0;}
BOOL GetWindowRect(HWND w,RECT* r){*r=w->rect;return TRUE;}BOOL GetClientRect(HWND w,RECT* r){*r={0,0,w->rect.right-w->rect.left,w->rect.bottom-w->rect.top};return TRUE;}
BOOL SetWindowPos(HWND w,HWND,int x,int y,int width,int height,UINT){w->rect={x,y,x+width,y+height};return TRUE;}
BOOL MoveWindow(HWND w,int x,int y,int width,int height,BOOL){w->rect={x,y,x+width,y+height};return TRUE;}
int MulDiv(int n,int mul,int div){return static_cast<int>((static_cast<long long>(n)*mul)/div);}
HWND CreateWindowExW(DWORD,const wchar_t*,const wchar_t* text,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE,void*){
    HWND w=make(parent,static_cast<int>(reinterpret_cast<INT_PTR>(menu)));w->rect={x,y,x+width,y+height};w->text=text?text:L"";w->style=style;return w;}
HINSTANCE GetModuleHandleW(const wchar_t*){return nullptr;}HWND SetFocus(HWND w){return w;}
BOOL EndDialog(HWND,INT_PTR r){ended=r;return TRUE;}
INT_PTR DialogBoxIndirectParamW(HINSTANCE,const DLGTEMPLATE*,HWND parent,DLGPROC proc,LPARAM param){auto w=make(parent,-1);ended=-999;proc(w,WM_INITDIALOG,0,param);if(ended==-999)scenario(w,proc);check(ended!=-999,"script ended modal");return ended;}
int MessageBoxW(HWND,const wchar_t* message,const wchar_t*,UINT flags){last_message_text=message;++message_calls;last_message_flags=flags;return message_answer;}
BOOL GetOpenFileNameW(OPENFILENAMEW* o){last_filter=o->lpstrFilter;save_picker=false;if(picked_file.empty())return FALSE;std::copy(picked_file.begin(),picked_file.end(),o->lpstrFile);o->lpstrFile[picked_file.size()]=0;return TRUE;}
BOOL GetSaveFileNameW(OPENFILENAMEW* o){auto r=GetOpenFileNameW(o);save_picker=true;return r;}DWORD CommDlgExtendedError(){return 0;}
HRESULT CoCreateInstance(int,void*,DWORD,int,void** out){*out=&folder_dialog;return 0;}void CoTaskMemFree(void* p){free(p);}
}
int main(){try{
    auto owner=make(nullptr);GuiWorkspace s;s.owner=owner;s.state.source("source.qcow2",xhc::ui::Format::Qcow2,false,false);s.state.output("new.qcow2",xhc::ui::Format::Qcow2);s.state.helpers("qemu-a","ffmpeg-a");
    for(unsigned dpi:{96u,120u,144u,192u})for(bool writes:{false,true})for(bool exclude:{false,true}) {
        s.dpi=dpi;auto o=xhc::ui::disk_options(s.state.settings(),!writes);o.offline_confirmed=true;o.acknowledge_snapshot_exclusion=true;
        scenario=[&](HWND w,DLGPROC proc){
            bounds(w);check(!IsWindowEnabled(GetDlgItem(w,IDOK)),"Proceed starts disabled");
            check(SendMessageW(GetDlgItem(w,Offline),BM_GETCHECK,0,0)==BST_UNCHECKED,"offline checkbox fresh");
            proc(w,WM_COMMAND,IDOK,0);check(ended==-999,"synthetic/Enter cannot bypass offline consent");
            if(writes){click_check(w,proc,Snapshots,true);check(!IsWindowEnabled(GetDlgItem(w,IDOK)),"snapshot checkbox alone cannot enable Proceed");}
            else check(!GetDlgItem(w,Snapshots),"read-only operations have no snapshot export checkbox");
            click_check(w,proc,Offline,true);check(IsWindowEnabled(GetDlgItem(w,IDOK)),"offline consent enables Proceed independently");
            click_check(w,proc,Offline,false);check(!IsWindowEnabled(GetDlgItem(w,IDOK)),"unchecking disables Proceed");
            proc(w,WM_COMMAND,IDOK,0);check(ended==-999,"recheck enforced in submit handler");
            click_check(w,proc,Offline,true);if(writes)click_check(w,proc,Snapshots,exclude);proc(w,WM_COMMAND,IDOK,0);
        };
        check(ConfirmWorkspaceOperation(s,o,L"Test",writes),"confirmation accepted");check(o.offline_confirmed&&o.acknowledge_snapshot_exclusion==(writes&&exclude),"frozen consent exact");
    }
    auto o=xhc::ui::disk_options(s.state.settings(),false);
    for(auto command:{static_cast<UINT>(WM_CLOSE),static_cast<UINT>(WM_COMMAND)}) {
        scenario=[&](HWND w,DLGPROC p){p(w,command,IDCANCEL,0);};
        check(!ConfirmWorkspaceOperation(s,o,L"Cancel test",true)&&!o.offline_confirmed&&!o.acknowledge_snapshot_exclusion,"cancel starts no operation");
    }
    scenario=[&](HWND w,DLGPROC p){bounds(w);SetWindowTextW(GetDlgItem(w,QPath),L"qemu-b");SetWindowTextW(GetDlgItem(w,FPath),L"ffmpeg-b");p(w,WM_COMMAND,IDCANCEL,0);};
    check(!EditWorkspaceHelpers(s),"helper Cancel");check(s.state.settings().qemu_img=="qemu-a"&&s.state.settings().ffmpeg=="ffmpeg-a","cancel leaves shared helper settings unchanged");
    scenario=[&](HWND w,DLGPROC p){p(w,WM_COMMAND,Defaults,0);check(read_control(w,QPath)==(xhc::executable_directory()/"tools"/"qemu-img.exe").wstring(),"restore EXE-relative qemu");check(read_control(w,FPath)==(xhc::executable_directory()/"tools"/"ffmpeg.exe").wstring(),"restore EXE-relative FFmpeg");p(w,WM_COMMAND,IDCANCEL,0);};
    check(!EditWorkspaceHelpers(s)&&s.state.settings().qemu_img=="qemu-a","restore then cancel does not commit");
    scenario=[&](HWND w,DLGPROC p){SetWindowTextW(GetDlgItem(w,QPath),L"qemu-b");SetWindowTextW(GetDlgItem(w,FPath),L"ffmpeg-b");p(w,WM_COMMAND,IDOK,0);};
    check(EditWorkspaceHelpers(s)&&s.state.settings().qemu_img=="qemu-b"&&s.state.settings().ffmpeg=="ffmpeg-b","helper Save commits shared paths together");
    auto ctx=std::make_shared<xhc::Context>();auto ticket=BeginWorkspaceJob(s);
    check(!EditWorkspaceHelpers(s),"settings locked for active job");
    SnapshotQuestion q{ticket.id,3,L"source.qcow2",ctx.get()};
    message_answer=IDNO;check(!AnswerSnapshotQuestion(s,q),"detected snapshots default No");check(last_message_flags&MB_DEFBUTTON2,"destructive exclusion default is No");
    message_answer=IDYES;check(AnswerSnapshotQuestion(s,q),"explicit detected-snapshot Yes");
    int previous=message_calls;q.job=ticket.id+1;check(!AnswerSnapshotQuestion(s,q)&&message_calls==previous,"stale request cannot prompt");q.job=ticket.id;
    ctx->cancelled=true;check(!AnswerSnapshotQuestion(s,q)&&message_calls==previous,"cancelled operation cannot authorize");ctx->cancelled=false;
    send_override=[&](HWND,UINT m,WPARAM,LPARAM l){return m==SnapshotQuestionMessage?static_cast<LRESULT>(AnswerSnapshotQuestion(s,*reinterpret_cast<SnapshotQuestion*>(l))):0;};
    ConnectSnapshotQuestion(s,o,ctx,ticket.id);check(o.confirm_snapshot_exclusion(3),"worker callback marshals actual count to UI");
    FinishWorkspaceJob(s,ticket.id);check(!s.state.busy()&&!o.confirm_snapshot_exclusion(3),"late snapshot request cannot outlive task lease");send_override={};
    picked_file=L"/tmp/example.qcow2";PickWorkspaceFile(owner,xhc::ui::Format::Qcow2,false);check(last_filter==L"QCOW2 disk image"&&!save_picker,"QCOW2 source uses file picker/filter");
    PickWorkspaceFile(owner,xhc::ui::Format::Raw,false);check(last_filter==L"RAW disk image"&&!save_picker,"RAW source uses file picker/filter");
    picked_file=L"/tmp/xhc-absent-"+std::wstring(40,L'7')+L".raw";auto dst=PickWorkspaceFile(owner,xhc::ui::Format::Raw,true);check(save_picker&&!dst.empty(),"image output uses save picker");
    check(PickWorkspaceFolder(owner,false)==xhc::fs::path("/tmp")&&(folder_dialog.options&FOS_PICKFOLDERS),"Folder source uses folder picker");
    auto out=PickWorkspaceFolder(owner,true);check(out.parent_path()=="/tmp"&&!xhc::fs::exists(out),"Folder output selects a new child not existing parent");
    std::cout<<"PASS: "<<checks<<" production confirmation/options/consent message checks (Win32 facade, not OS rendering)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
