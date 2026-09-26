// SPDX-License-Identifier: GPL-2.0-or-later
// Executes the production theme service with deterministic Win32/GDI/registry bindings.
// Not native Windows rendering or a Windows SDK test.
#include "../src/gui_theme_win32.cpp"
#include "../src/gui_progress_win32.hpp"
#include <iostream>
#include <map>
#include <set>
#include <cstring>
#include <stdexcept>

struct HMENU__;
struct HWND__ {
    std::wstring cls,text;
    HWND parent=nullptr;
    LONG_PTR style=0;
    bool enabled=true,marquee=false,selected=false;
    int check=0,button_state=0,pos=0,range=100,tab=0,item_count=0;
    UINT dpi=96;RECT rect{0,0,400,40};
    SUBCLASSPROC procedure=nullptr;DWORD_PTR data=0;
    HMENU menu=nullptr;
    std::vector<std::wstring> columns;
    std::set<UINT_PTR> timers;
    COLORREF bg=0,textbg=0,fg=0;
};
struct FakeMenuItem{std::wstring text;UINT type=0,state=0,id=0;ULONG_PTR data=0;HMENU child=nullptr;};
struct HMENU__{std::vector<FakeMenuItem> items;HBRUSH bg=nullptr;bool live=true;};
struct HDC__{COLORREF text=0,bg=0;int bk=0;};
namespace fake {
std::vector<std::unique_ptr<HWND__>> all;
std::vector<std::unique_ptr<HMENU__>> all_menus;
std::map<HGDIOBJ,COLORREF> objects;
unsigned checks=0,forwarded=0,created=0,deleted=0,paint_begins=0,paint_ends=0,custom_draws=0;
unsigned init_default_calls=0;
bool hc=false,registry_exists=false,registry_fail=false,dwm_fail=false;
DWORD stored_theme=0;HWND focused=nullptr,capture=nullptr;
HDC__ dc;std::vector<HDC__> stack;
std::vector<COLORREF> fills,text_colors;
std::vector<std::wstring> labels;
std::map<HWND,std::map<DWORD,DWORD>> frames;
int invalid_pos=0,registry_reads=0,registry_writes=0;
void check(bool b,const char* what){++checks;if(!b)throw std::runtime_error(what);}
HWND make(const wchar_t* cls,HWND parent=nullptr) {
    auto w=std::make_unique<HWND__>();w->cls=cls;w->parent=parent;w->style=parent?WS_CHILD:0;auto result=w.get();all.push_back(std::move(w));return result;
}
COLORREF brush_color(HBRUSH b){auto it=objects.find(b);return it==objects.end()?0xFFFFFFFF:it->second;}
void destroy(HWND w){SendMessageW(w,WM_NCDESTROY,0,0);}
}
extern "C" {
HBRUSH CreateSolidBrush(COLORREF c){auto p=new unsigned(++fake::created);fake::objects[p]=c;return reinterpret_cast<HBRUSH>(p);}
HPEN CreatePen(int,int,COLORREF c){return reinterpret_cast<HPEN>(CreateSolidBrush(c));}
BOOL DeleteObject(HGDIOBJ o){auto it=fake::objects.find(o);if(it==fake::objects.end())return FALSE;delete reinterpret_cast<unsigned*>(o);fake::objects.erase(it);++fake::deleted;return TRUE;}
HGDIOBJ SelectObject(HDC,HGDIOBJ){return reinterpret_cast<HGDIOBJ>(0x31);}
HGDIOBJ GetStockObject(int n){return reinterpret_cast<HGDIOBJ>(static_cast<intptr_t>(n+0x100));}
HBRUSH GetSysColorBrush(int n){return reinterpret_cast<HBRUSH>(static_cast<intptr_t>(n+0x100));}
COLORREF GetSysColor(int n){return static_cast<COLORREF>(n);}
int SaveDC(HDC d){fake::stack.push_back(*d);return static_cast<int>(fake::stack.size());}
BOOL RestoreDC(HDC d,int n){*d=fake::stack.at(static_cast<size_t>(n-1));fake::stack.resize(static_cast<size_t>(n-1));return TRUE;}
COLORREF SetTextColor(HDC d,COLORREF c){auto old=d->text;d->text=c;fake::text_colors.push_back(c);return old;}
COLORREF SetBkColor(HDC d,COLORREF c){auto old=d->bg;d->bg=c;return old;}
int SetBkMode(HDC d,int n){auto old=d->bk;d->bk=n;return old;}
int DrawTextW(HDC,const wchar_t* s,int n,RECT* r,UINT flags){++fake::custom_draws;const auto text=n<0?std::wstring(s):std::wstring(s,static_cast<size_t>(n));fake::labels.push_back(text);if(flags&DT_CALCRECT){r->right=r->left+static_cast<LONG>(text.size()*7);r->bottom=r->top+16;}return 16;}
BOOL DrawFocusRect(HDC,const RECT*){return TRUE;}
BOOL Ellipse(HDC,int,int,int,int){++fake::custom_draws;return TRUE;}
BOOL MoveToEx(HDC,int,int,POINT*){return TRUE;}BOOL LineTo(HDC,int,int){return TRUE;}
BOOL InflateRect(RECT* r,int x,int y){r->left-=x;r->right+=x;r->top-=y;r->bottom+=y;return TRUE;}
int FillRect(HDC,const RECT*,HBRUSH b){fake::fills.push_back(fake::brush_color(b));return 1;}
HDC BeginPaint(HWND,PAINTSTRUCT* p){++fake::paint_begins;p->hdc=&fake::dc;return &fake::dc;}
BOOL EndPaint(HWND,const PAINTSTRUCT*){++fake::paint_ends;return TRUE;}
BOOL GetClientRect(HWND w,RECT* r){*r=w->rect;return TRUE;}
HDC GetDC(HWND){return &fake::dc;}int ReleaseDC(HWND,HDC){return 1;}
int MulDiv(int n,int m,int d){return static_cast<int>(static_cast<long long>(n)*m/d);}
UINT GetDpiForWindow(HWND w){return w->dpi;}
int GetClassNameW(HWND w,wchar_t* s,int n){int len=std::min(n-1,static_cast<int>(w->cls.size()));std::copy_n(w->cls.c_str(),len,s);s[len]=0;return len;}
int lstrcmpiW(const wchar_t* a,const wchar_t* b){while(*a&&*b){auto x=std::towlower(*a++),y=std::towlower(*b++);if(x!=y)return x<y?-1:1;}return *a==*b?0:*a?1:-1;}
int GetWindowTextLengthW(HWND w){return static_cast<int>(w->text.size());}
int GetWindowTextW(HWND w,wchar_t* s,int n){int len=std::min(n-1,static_cast<int>(w->text.size()));std::copy_n(w->text.c_str(),len,s);s[len]=0;return len;}
HWND GetParent(HWND w){return w->parent;}
BOOL IsWindowEnabled(HWND w){return w->enabled;}
LONG_PTR GetWindowLongPtrW(HWND w,int){return w->style;}
LONG_PTR SetWindowLongPtrW(HWND w,int,LONG_PTR n){auto old=w->style;w->style=n;return old;}
HWND GetFocus(){return fake::focused;}HWND GetCapture(){return fake::capture;}
BOOL TrackMouseEvent(TRACKMOUSEEVENT*){return TRUE;}
BOOL InvalidateRect(HWND,const RECT*,BOOL){return TRUE;}BOOL UpdateWindow(HWND){return TRUE;}
BOOL RedrawWindow(HWND,const RECT*,HANDLE,UINT){return TRUE;}
UINT_PTR SetTimer(HWND w,UINT_PTR n,UINT,void*){w->timers.insert(n);return n;}
BOOL KillTimer(HWND w,UINT_PTR n){return w->timers.erase(n)!=0;}
BOOL SetWindowSubclass(HWND w,SUBCLASSPROC p,UINT_PTR,DWORD_PTR d){w->procedure=p;w->data=d;return TRUE;}
BOOL GetWindowSubclass(HWND w,SUBCLASSPROC p,UINT_PTR,DWORD_PTR* d){if(w->procedure!=p)return FALSE;*d=w->data;return TRUE;}
BOOL RemoveWindowSubclass(HWND w,SUBCLASSPROC p,UINT_PTR){if(w->procedure!=p)return FALSE;w->procedure=nullptr;w->data=0;return TRUE;}
LRESULT DefSubclassProc(HWND w,UINT m,WPARAM p,LPARAM l){
    ++fake::forwarded;
    if(m==BM_GETSTATE)return w->button_state;
    if(m==BM_GETCHECK)return w->check;
    if(m==BM_SETCHECK){w->check=static_cast<int>(p);return 0;}
    if(m==BM_SETSTATE){w->button_state=p?BST_PUSHED:0;return 0;}
    if(m==TCM_GETCURSEL)return w->tab;
    if(m==TCM_SETCURSEL){w->tab=static_cast<int>(p);return 0;}
    if(m==PBM_GETPOS)return w->pos;
    if(m==PBM_GETRANGE){auto* range=reinterpret_cast<PBRANGE*>(l);range->iLow=0;range->iHigh=w->range;return w->range;}
    if(m==PBM_SETRANGE32){w->range=static_cast<int>(l);return 0;}
    if(m==PBM_SETPOS){if(w->style&PBS_MARQUEE)++fake::invalid_pos;w->pos=static_cast<int>(p);return 0;}
    if(m==PBM_SETMARQUEE){w->marquee=p!=0;return 0;}
    if(m==WM_NOTIFY&&l){auto* n=reinterpret_cast<NMLVCUSTOMDRAW*>(l);if(n->nmcd.hdr.code==NM_CUSTOMDRAW&&n->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYPOSTPAINT;}
    if(m==WM_SETTEXT){w->text=reinterpret_cast<const wchar_t*>(l);return TRUE;}
    if(m==WM_COMMAND)return 123; // Commands must still reach the real controller.
    return 0;
}
LRESULT SendMessageW(HWND w,UINT m,WPARAM p,LPARAM l){return w->procedure?w->procedure(w,m,p,l,kSubclass,w->data):DefSubclassProc(w,m,p,l);}
BOOL EnumChildWindows(HWND root,WNDENUMPROC callback,LPARAM lp){for(auto& w:fake::all){for(HWND p=w->parent;p;p=p->parent)if(p==root){if(!callback(w.get(),lp))return FALSE;break;}}return TRUE;}
BOOL SystemParametersInfoW(UINT action,UINT,void* p,UINT){if(action==SPI_GETHIGHCONTRAST){static_cast<HIGHCONTRASTW*>(p)->dwFlags=fake::hc?HCF_HIGHCONTRASTON:0;return TRUE;}return FALSE;}
HRESULT SetWindowTheme(HWND,const wchar_t*,const wchar_t*){return 0;}
HRESULT DwmSetWindowAttribute(HWND w,DWORD attr,const void* data,DWORD size){if(fake::dwm_fail)return -1;DWORD val=0;std::memcpy(&val,data,std::min<size_t>(size,sizeof(val)));fake::frames[w][attr]=val;return 0;}
BOOL ListView_SetBkColor(HWND w,COLORREF c){w->bg=c;return TRUE;}
BOOL ListView_SetTextBkColor(HWND w,COLORREF c){w->textbg=c;return TRUE;}
BOOL ListView_SetTextColor(HWND w,COLORREF c){w->fg=c;return TRUE;}
UINT ListView_GetItemState(HWND w,int,UINT){return w->selected?LVIS_SELECTED:0;}
int ListView_GetItemCount(HWND w){return w->item_count;}
BOOL ListView_GetItemRect(HWND w,int n,RECT* r,int){if(n<0||n>=w->item_count)return FALSE;*r={0,20+n*20,w->rect.right,40+n*20};return TRUE;}
int TabCtrl_GetItemCount(HWND w){return static_cast<int>(w->columns.size());}
int TabCtrl_GetCurSel(HWND w){return w->tab;}
BOOL TabCtrl_GetItemRect(HWND w,int n,RECT* r){*r={MulDiv(n*140,w->dpi,96),0,MulDiv((n+1)*140,w->dpi,96),MulDiv(30,w->dpi,96)};return TRUE;}
BOOL TabCtrl_GetItem(HWND w,int n,TCITEMW* i){auto& s=w->columns.at(static_cast<size_t>(n));int len=std::min(static_cast<int>(s.size()),i->cchTextMax-1);std::copy_n(s.c_str(),len,i->pszText);i->pszText[len]=0;return TRUE;}
int Header_GetItemCount(HWND w){return static_cast<int>(w->columns.size());}
BOOL Header_GetItemRect(HWND w,int n,RECT* r){return TabCtrl_GetItemRect(w,n,r);}
BOOL Header_GetItem(HWND w,int n,HDITEMW* i){auto& s=w->columns.at(static_cast<size_t>(n));int len=std::min(static_cast<int>(s.size()),i->cchTextMax-1);std::copy_n(s.c_str(),len,i->pszText);i->pszText[len]=0;return TRUE;}
HMENU GetMenu(HWND w){return w->menu;}
HMENU CreatePopupMenu(){auto m=std::make_unique<HMENU__>();auto p=m.get();fake::all_menus.push_back(std::move(m));return p;}
BOOL AppendMenuW(HMENU m,UINT flags,UINT_PTR id,const wchar_t* label){FakeMenuItem i;i.text=label?label:L"";i.type=flags&(MFT_SEPARATOR|MFT_OWNERDRAW);i.state=flags&(MF_GRAYED|MF_CHECKED);if(flags&MF_POPUP)i.child=reinterpret_cast<HMENU>(id);else i.id=static_cast<UINT>(id);m->items.push_back(i);return TRUE;}
BOOL IsMenu(HMENU m){return m&&m->live;}
BOOL DestroyMenu(HMENU m){m->live=false;return TRUE;}
int GetMenuItemCount(HMENU m){return static_cast<int>(m->items.size());}
HMENU GetSubMenu(HMENU m,int n){return m->items.at(static_cast<size_t>(n)).child;}
BOOL GetMenuInfo(HMENU m,MENUINFO* i){i->hbrBack=m->bg;return TRUE;}
BOOL SetMenuInfo(HMENU m,const MENUINFO* i){m->bg=i->hbrBack;return TRUE;}
BOOL GetMenuItemInfoW(HMENU m,UINT n,BOOL,MENUITEMINFOW* i){auto& v=m->items.at(n);if(i->fMask&MIIM_FTYPE)i->fType=v.type;if(i->fMask&MIIM_DATA)i->dwItemData=v.data;if(i->fMask&MIIM_STATE)i->fState=v.state;if((i->fMask&MIIM_STRING)&&i->cch>0){auto size=std::min(v.text.size(),static_cast<size_t>(i->cch)-1);std::copy_n(v.text.data(),size,i->dwTypeData);i->dwTypeData[size]=0;i->cch=static_cast<UINT>(size);}return TRUE;}
BOOL SetMenuItemInfoW(HMENU m,UINT n,BOOL,const MENUITEMINFOW* i){auto& v=m->items.at(n);if(i->fMask&MIIM_FTYPE)v.type=i->fType;if(i->fMask&MIIM_DATA)v.data=i->dwItemData;return TRUE;}
BOOL CheckMenuRadioItem(HMENU m,UINT,UINT,UINT selected,UINT){for(auto& i:m->items){i.state&=~MF_CHECKED;if(i.id==selected)i.state|=MF_CHECKED;}return TRUE;}
BOOL TrackPopupMenu(HMENU m,UINT,int,int,int,HWND,const RECT*){fake::check(!custom()||(m->items[0].type&MFT_OWNERDRAW),"popup themed before tracking");return 77;}
LSTATUS RegGetValueW(HKEY,const wchar_t*,const wchar_t*,DWORD,DWORD*,void* data,DWORD* size){++fake::registry_reads;if(!fake::registry_exists)return ERROR_FILE_NOT_FOUND;if(*size<sizeof(DWORD))return ERROR_ACCESS_DENIED;*static_cast<DWORD*>(data)=fake::stored_theme;*size=sizeof(DWORD);return ERROR_SUCCESS;}
LSTATUS RegCreateKeyExW(HKEY,const wchar_t*,DWORD,wchar_t*,DWORD,DWORD,void*,HKEY* out,DWORD*){if(fake::registry_fail)return ERROR_ACCESS_DENIED;*out=reinterpret_cast<HKEY>(1);return ERROR_SUCCESS;}
LSTATUS RegSetValueExW(HKEY,const wchar_t*,DWORD,DWORD,const BYTE* data,DWORD){++fake::registry_writes;std::memcpy(&fake::stored_theme,data,sizeof(DWORD));fake::registry_exists=true;return ERROR_SUCCESS;}
LSTATUS RegCloseKey(HKEY){return ERROR_SUCCESS;}
}
BOOL DrawMenuBar(HWND){return TRUE;}
int main(){try {
    using fake::check;
    InitializeGuiTheme();check(CurrentGuiTheme()==Theme::Default,"no setting starts white");
    HWND root=fake::make(L"XemuHddConverterWindow");
    HWND csb=fake::make(L"XhcCsbPage",root),hdd=fake::make(L"XhcHddDirectoryPage",root);
    HWND list=fake::make(WC_LISTVIEWW,csb),header=fake::make(WC_HEADERW,list),hdd_list=fake::make(WC_LISTVIEWW,hdd);
    HWND radio=fake::make(L"Button",root),checkbox=fake::make(L"Button",root),button=fake::make(L"Button",root);
    HWND tabs=fake::make(WC_TABCONTROLW,root),progress=fake::make(PROGRESS_CLASSW,csb),split=fake::make(L"XhcPaneSplitter",csb);
    HWND edit=fake::make(L"Edit",root),label=fake::make(L"Static",root);
    radio->style|=BS_AUTORADIOBUTTON;checkbox->style|=BS_AUTOCHECKBOX|BS_MULTILINE;
    radio->text=L"QCOW2";checkbox->text=L"XEMU and all other programs using this source are closed.";button->text=L"Save";
    tabs->columns={L"HDD Converter",L"Custom Soundtrack Builder",L"HDD Directory"};header->columns={L"Name",L"Size"};
    const auto radio_style=radio->style;root->menu=CreatePopupMenu();HMENU options=CreatePopupMenu(),choices=CreatePopupMenu();
    AppendMenuW(root->menu,MF_POPUP,reinterpret_cast<UINT_PTR>(options),L"&Options");
    AppendMenuW(options,MF_POPUP,reinterpret_cast<UINT_PTR>(choices),L"&Theme");
    AppendMenuW(choices,MF_STRING,1,L"&Default (White)");AppendMenuW(choices,MF_STRING,2,L"D&ark Mode");AppendMenuW(choices,MF_STRING,3,L"&Xbox Mode");
    ApplyGuiTheme(root);
    check(windows.size()==fake::all.size(),"all descendants including hidden tab are attached");
    check(list->bg==RGB(255,255,255)&&hdd_list->bg==list->bg,"both lists white by default");
    check(SendMessageW(root,WM_ERASEBKGND,reinterpret_cast<WPARAM>(&fake::dc),0)==TRUE,"root erases to white");
    check(fake::fills.back()==RGB(255,255,255),"default background is white");
    check(!(choices->items[0].type&MFT_OWNERDRAW),"default menus native");
    const auto native_before=fake::forwarded;SendMessageW(radio,WM_PAINT,0,0);check(fake::forwarded>native_before,"default button uses native painter");
    for(auto mode:{Theme::Dark,Theme::Xbox,Theme::Default,Theme::Xbox}) {
        check(SelectGuiTheme(root,mode),"selection persists");
        check(CurrentGuiTheme()==mode&&fake::stored_theme==static_cast<DWORD>(mode),"mode recorded");
        const auto p=xhc::ui::theme_palette(mode);
        check(list->bg==color(p.field)&&hdd_list->fg==color(p.text),"list theme applied to hidden tab");
        check(radio->style==radio_style,"radio behavior/style preserved");
        check(SendMessageW(root,WM_COMMAND,7,0)==123,"commands reach original controller");
        if(mode!=Theme::Default) {
            for(UINT dpi:{96u,120u,144u,192u}) {
                for(HWND w:{radio,checkbox,button,tabs,header,progress,split}) {
                    w->dpi=dpi;w->rect={0,0,MulDiv(460,dpi,96),MulDiv(48,dpi,96)};
                    SendMessageW(w,WM_PAINT,0,0);check(fake::paint_begins==fake::paint_ends,"BeginPaint/EndPaint balanced");
                    check(fake::stack.empty(),"DC state restored");
                }
            }
            SendMessageW(radio,BM_SETCHECK,BST_CHECKED,0);SendMessageW(radio,WM_PAINT,0,0);check(radio->check==BST_CHECKED,"checked radio preserved through drawing");
            SendMessageW(checkbox,BM_SETCHECK,BST_CHECKED,0);SendMessageW(checkbox,WM_PAINT,0,0);check(checkbox->check==BST_CHECKED,"checkbox uses native check state");
            button->enabled=false;SendMessageW(button,WM_PAINT,0,0);check(fake::text_colors.back()==color(p.muted),"disabled button legible");button->enabled=true;
            for(HWND w:{edit,label}){SendMessageW(root,WM_CTLCOLORSTATIC,reinterpret_cast<WPARAM>(&fake::dc),reinterpret_cast<LPARAM>(w));check(fake::dc.bg==color(w==edit?p.field:p.background),"readonly edit and label use appropriate backgrounds");}
            list->selected=true;NMLVCUSTOMDRAW n{};n.nmcd.hdr={list,0,NM_CUSTOMDRAW};n.nmcd.dwDrawStage=CDDS_PREPAINT;
            auto flags=SendMessageW(csb,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&n));check((flags&CDRF_NOTIFYPOSTPAINT)&&(flags&CDRF_NOTIFYITEMDRAW),"theme retains CSB postpaint insertion marker");
            list->item_count=2;list->rect={0,0,500,300};n.nmcd.dwDrawStage=CDDS_POSTPAINT;n.nmcd.hdc=&fake::dc;
            const auto fills_before=fake::fills.size();SendMessageW(csb,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&n));
            check(fake::fills.size()>fills_before&&fake::fills.back()==color(p.field),"unused list interior repainted with theme field color");
            const auto panel_before=fake::fills.size();SendMessageW(csb,WM_PAINT,0,0);
            check(fake::fills.size()>panel_before&&fake::fills.back()==color(p.background),"app-owned page paints full theme background");
            n.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;n.nmcd.uItemState=CDIS_SELECTED;
            check(SendMessageW(csb,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&n))==CDRF_NEWFONT,"selected row custom color response");
            check(n.clrText==color(p.selected_text)&&n.clrTextBk==color(p.selection)&&list->selected,"selection display changes, real selection does not");
            check(choices->items[0].type&MFT_OWNERDRAW,"theme menu owner draw enabled");
            MEASUREITEMSTRUCT measure{};measure.CtlType=ODT_MENU;measure.itemData=choices->items[1].data;
            check(SendMessageW(root,WM_MEASUREITEM,0,reinterpret_cast<LPARAM>(&measure))&&measure.itemWidth>0&&measure.itemHeight>0,"menu text measured");
            DRAWITEMSTRUCT draw{};draw.CtlType=ODT_MENU;draw.itemData=choices->items[2].data;draw.hDC=&fake::dc;draw.rcItem={0,0,200,30};draw.itemState=ODS_SELECTED|ODS_CHECKED;
            check(SendMessageW(root,WM_DRAWITEM,0,reinterpret_cast<LPARAM>(&draw))==TRUE,"selected/checked menu drawn");
            auto key=SendMessageW(root,WM_MENUCHAR,L'X',reinterpret_cast<LPARAM>(choices));check(LOWORD(key)==2&&HIWORD(key)==MNC_EXECUTE,"custom menu keyboard mnemonic works");
        }
    }
    for(int cycle=0;cycle<15;++cycle) {
        SetOperationProgress(progress,true);check(progress->marquee&&!progress->timers.empty(),"custom marquee starts");
        SendMessageW(progress,WM_TIMER,kAnimationTimer,0);SendMessageW(progress,WM_PAINT,0,0);
        check(SelectGuiTheme(root,Theme::Default)&&progress->marquee&&progress->timers.empty(),"switch to white keeps job marquee, drops custom timer");
        check(SelectGuiTheme(root,Theme::Xbox)&&progress->marquee&&!progress->timers.empty(),"switch to Xbox while busy keeps animation");
        SetOperationProgress(progress,false);check(!progress->marquee&&progress->pos==0&&progress->timers.empty(),"completion clears position and stops animation");
        SendMessageW(progress,WM_PAINT,0,0);
    }
    check(fake::invalid_pos==0,"never resets PBM_SETPOS with marquee style active");
    for(int n=0;n<20;++n) {
        auto old_menus=menus.size();HMENU popup=CreatePopupMenu();AppendMenuW(popup,MF_STRING,77,L"&Rename");
        check(TrackGuiThemePopup(popup,TPM_RETURNCMD|TPM_NONOTIFY,0,0,csb)==77,"popup returns original action id");
        check(menus.size()==old_menus&&popup->items[0].data==0&&!(popup->items[0].type&MFT_OWNERDRAW),"temporary menu restored without stale item pointers");DestroyMenu(popup);
    }
    HWND dialog=fake::make(L"#32770");HWND d_edit=fake::make(L"Edit",dialog),d_ok=fake::make(L"Button",dialog);d_ok->text=L"Proceed";d_ok->enabled=false;
    ApplyGuiTheme(dialog);check(d_edit->procedure&&d_ok->procedure&&!d_ok->enabled,"new modal inherits theme without enabling Proceed");
    fake::dwm_fail=true;ApplyGuiTheme(dialog);check(dialog->procedure!=nullptr,"older Windows titlebar attribute failure is harmless");fake::dwm_fail=false;
    fake::hc=true;SendMessageW(root,WM_SETTINGCHANGE,0,0);check(CurrentGuiTheme()==Theme::Xbox,"high contrast does not erase preference");
    check(!(choices->items[0].type&MFT_OWNERDRAW)&&list->bg==GetSysColor(COLOR_WINDOW),"high contrast restores native menu/list colors");
    auto before=fake::forwarded;SendMessageW(button,WM_PAINT,0,0);check(fake::forwarded>before,"high contrast delegates button rendering");
    fake::hc=false;RefreshGuiThemeSystem(root);check(choices->items[0].type&MFT_OWNERDRAW,"leaving high contrast restores user theme");
    fake::registry_fail=true;check(!SelectGuiTheme(root,Theme::Dark)&&CurrentGuiTheme()==Theme::Dark,"unwritable preferences do not block session theme");fake::registry_fail=false;
    fake::stored_theme=77;InitializeGuiTheme();check(CurrentGuiTheme()==Theme::Default,"invalid saved mode safe fallback");
    fake::stored_theme=2;InitializeGuiTheme();check(CurrentGuiTheme()==Theme::Xbox,"saved Xbox mode restored");
    for(auto it=fake::all.rbegin();it!=fake::all.rend();++it)fake::destroy(it->get());
    check(windows.empty()&&menus.empty(),"all window state and menu registrations released");
    ShutdownGuiTheme();check(fake::objects.empty()&&fake::created==fake::deleted,"owned GDI resources all released");
    check(fake::paint_begins==fake::paint_ends&&fake::stack.empty(),"all drawing lifetime pairs balanced");
    std::cout<<"PASS: "<<fake::checks<<" production theme/drawing/lifecycle checks (simulated Win32/GDI/registry; not OS rendering)\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";ShutdownGuiTheme();return 1;}}
