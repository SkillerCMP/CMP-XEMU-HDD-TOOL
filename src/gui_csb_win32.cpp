// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include "gui_csb_win32.hpp"
#include "gui_workspace_win32.hpp"
#include "gui_theme_win32.hpp"
#include "xhc/csb.hpp"
#include "xhc/ui_layout.hpp"
#include "gui_progress_win32.hpp"
#include "gui_splitter_win32.hpp"
#include <algorithm>
#include <memory>
#include <thread>

namespace {
using namespace xhc;
constexpr UINT LogMessage = WM_APP + 21, DoneMessage = WM_APP + 22;
constexpr UINT_PTR ScrollTimer = 91;
enum Id {
    Refresh = 1000,
    BackupLabel,
    NameLabel,
    Name,
    NameApply,
    IdLabel,
    Identity,
    EditStatus,
    Save,
    Clear,
    NewSoundtrack,
    Sounds,
    AddFiles,
    Songs,
    Status,
    Log,
    Cancel,
    OpenOutput,
    OpenBackup,
    Progress,
    Splitter1,
    Splitter2,
    Last
};
struct Completion {
    bool success = false, loaded = false;
    std::string error;
    csb::Catalog catalog;
    csb::Saved saved;
};
struct Page {
    HWND window = nullptr, owner = nullptr;
    HFONT font = nullptr;
    unsigned dpi = 96;
    bool busy = false, populating = false, closing = false, discard_on_close = false;
    GuiWorkspace* shared = nullptr;
    uint64_t job = 0;
    bool source_changed = false;
    fs::path backup_location;
    int left = 226, middle = 360;
    ui::CsbPanes panes{};
    bool dragging = false;
    size_t drag_from = 0;
    int drag_boundary = -1;
    uint64_t drag_generation = 0;
    csb::Editor editor;
    csb::Saved last;
    std::shared_ptr<Context> context;
    std::thread worker;
};
Page* page_state(HWND w) { return reinterpret_cast<Page*>(GetWindowLongPtrW(w, GWLP_USERDATA)); }
HWND item(Page& p, int id) { return GetDlgItem(p.window, id); }
std::wstring wide(const std::string& s) {
    if (s.empty())
        return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    require(size > 0, "Invalid UTF-8 display text.");
    std::wstring w(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), w.data(), size);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty())
        return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                   nullptr, nullptr);
    require(size > 0, "Invalid Unicode input.");
    std::string s(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr,
                        nullptr);
    return s;
}
std::wstring text(Page& p, int id) {
    int size = GetWindowTextLengthW(item(p, id));
    std::wstring s(static_cast<size_t>(size) + 1, L'\0');
    int read = GetWindowTextW(item(p, id), s.data(), size + 1);
    s.resize(read > 0 ? static_cast<size_t>(read) : 0);
    return s;
}
void set(Page& p, int id, const std::wstring& value) { SetWindowTextW(item(p, id), value.c_str()); }
int selection(HWND list) { return ListView_GetNextItem(list, -1, LVNI_SELECTED); }
void log_line(Page& p, const std::wstring& message) {
    HWND edit = item(p, Log);
    if (GetWindowTextLengthW(edit) > 700000)
        SetWindowTextW(edit, L"[Earlier display messages omitted.]\r\n");
    auto value = message;
    for (size_t i = 0; i < value.size(); ++i)
        if (value[i] == L'\n' && (i == 0 || value[i - 1] != L'\r')) {
            value.insert(i, 1, L'\r');
            ++i;
        }
    value += L"\r\n";
    SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(value.c_str()));
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}
void post_log(HWND w, const std::string& s) {
    auto line = std::make_unique<std::wstring>(wide(s));
    if (PostMessageW(w, LogMessage, 0, reinterpret_cast<LPARAM>(line.get())))
        line.release();
}
void open_path(HWND owner, const fs::path& p) {
    if (p.empty())
        return;
    const auto target = fs::is_directory(p) ? p : p.parent_path();
    auto result = ShellExecuteW(owner, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    require(reinterpret_cast<INT_PTR>(result) > 32, "Windows could not open the selected folder.");
}
std::vector<fs::path> choose_files(HWND owner, bool audio, bool save = false) {
    std::vector<wchar_t> b(65536, L'\0');
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = owner;
    o.lpstrFile = b.data();
    o.nMaxFile = static_cast<DWORD>(b.size());
    o.lpstrFilter =
        audio ? L"Audio files\0*.mp3;*.wav;*.flac;*.wma;*.aac;*.m4a;*.ogg;*.opus;*.aif;*.aiff\0All files\0*.*\0"
              : L"Files\0*.*\0";
    o.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (audio ? OFN_ALLOWMULTISELECT : 0) |
              (save ? 0 : OFN_FILEMUSTEXIST);
    o.lpstrTitle = save    ? L"Choose a NEW output file (existing files are never replaced)"
                   : audio ? L"Add audio files"
                           : L"Select file";
    BOOL ok = save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o);
    if (!ok) {
        require(CommDlgExtendedError() == 0, "Windows file picker failed.");
        return {};
    }
    std::vector<fs::path> paths;
    std::wstring first = b.data();
    const wchar_t* next = b.data() + first.size() + 1;
    if (!audio || !*next)
        paths.emplace_back(first);
    else
        for (; *next; next += wcslen(next) + 1)
            paths.push_back(fs::path(first) / next);
    return paths;
}
struct Prompt {
    std::wstring value, caption;
    HFONT font = nullptr;
    unsigned dpi = 96;
};
INT_PTR CALLBACK prompt_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto* p = reinterpret_cast<Prompt*>(GetWindowLongPtrW(w, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        p = reinterpret_cast<Prompt*>(lp);
        SetWindowLongPtrW(w, DWLP_USER, lp);
        SetWindowTextW(w, p->caption.c_str());
        RECT r{};
        GetClientRect(w, &r);
        const int width = static_cast<int>(r.right), height = static_cast<int>(r.bottom);
        auto px = [&](int n) { return MulDiv(n, static_cast<int>(p->dpi), 96); };
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", p->value.c_str(),
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, px(12), px(12), width - px(24),
                                    px(26), w, reinterpret_cast<HMENU>(100), GetModuleHandleW(nullptr), nullptr);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                  width - px(180), height - px(38), px(78), px(26), w, reinterpret_cast<HMENU>(IDOK),
                                  GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, width - px(92),
                                      height - px(38), px(78), px(26), w, reinterpret_cast<HMENU>(IDCANCEL),
                                      GetModuleHandleW(nullptr), nullptr);
        for (HWND c : {edit, ok, cancel})
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(p->font), TRUE);
        SendMessageW(edit, EM_SETLIMITTEXT, 512, 0);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        ApplyGuiTheme(w);
        SetFocus(edit);
        return FALSE;
    }
    if (msg == WM_COMMAND && p) {
        if (LOWORD(wp) == IDOK) {
            HWND edit = GetDlgItem(w, 100);
            int n = GetWindowTextLengthW(edit);
            p->value.resize(static_cast<size_t>(n) + 1);
            int got = GetWindowTextW(edit, p->value.data(), n + 1);
            p->value.resize(static_cast<size_t>(std::max(0, got)));
            EndDialog(w, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(w, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}
std::optional<std::wstring> prompt(Page& p, const std::wstring& initial, const wchar_t* caption) {
    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog;
        WORD menu = 0, window_class = 0, title = 0;
    } t{};
    t.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    t.dialog.cx = 270;
    t.dialog.cy = 62;
    Prompt value{initial, caption, p.font, p.dpi};
    auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &t.dialog, p.window, prompt_proc,
                                          reinterpret_cast<LPARAM>(&value));
    require(result != -1, "Cannot create rename dialog.");
    if (result != IDOK)
        return {};
    return value.value;
}
void update(Page& p) {
    const bool idle = !p.busy && !p.shared->state.busy(),
               editable = idle && p.editor.selected() && !p.editor.read_only() && !p.source_changed;
    EnableWindow(item(p, Refresh), idle);
    for (int id : {Name, NameApply, Clear, AddFiles})
        EnableWindow(item(p, id), editable);
    EnableWindow(item(p, Songs), idle && p.editor.selected() && !p.source_changed);
    EnableWindow(item(p, Sounds), idle && !p.editor.catalog().fingerprint.empty() && !p.source_changed);
    EnableWindow(item(p, NewSoundtrack),
                 idle && !p.source_changed && !p.editor.catalog().fingerprint.empty() && p.editor.catalog().writable);
    EnableWindow(item(p, Save), editable && p.editor.can_save());
    EnableWindow(item(p, Cancel), p.busy && !p.closing);
    EnableWindow(item(p, OpenOutput), idle && !p.last.disk.destination.empty());
    EnableWindow(item(p, OpenBackup), idle && !p.backup_location.empty());
    set(p, EditStatus,
        p.source_changed       ? L"Source changed: REFRESH required."
        : p.editor.read_only() ? L"Read-only catalog / orphan."
        : p.editor.dirty()     ? L"Pending edits (not yet saved)."
        : p.editor.can_save()  ? L"ST.DB repair is available."
                               : L"No pending edits.");
    set(p, BackupLabel,
        L"SAVE creates a new output. Original retained; backups go beside the output in Backups\\CSB-<transaction>.");
}
void layout(Page& p) {
    RECT r{};
    GetClientRect(p.window, &r);
    const int w = MulDiv(static_cast<int>(r.right), 96, static_cast<int>(p.dpi));
    const int h = MulDiv(static_cast<int>(r.bottom), 96, static_cast<int>(p.dpi));
    auto place = [&](int id, int x, int y, int width, int height) {
        MoveWindow(item(p, id), MulDiv(x, p.dpi, 96), MulDiv(y, p.dpi, 96), MulDiv(std::max(1, width), p.dpi, 96),
                   MulDiv(std::max(1, height), p.dpi, 96), TRUE);
    };
    place(BackupLabel, 12, 10, w - 230, 32);
    place(Refresh, w - 208, 8, 196, 27);
    const int y = 52;
    const int bottom = h - 170;
    p.panes = ui::csb_panes(w, p.left, p.middle);
    const int left = p.panes.left, mid = p.panes.middle_x, right = p.panes.songs_x, right_width = p.panes.songs_width;
    place(NameLabel, 12, y, left, 20);
    place(Name, 12, y + 23, left - 8, 25);
    place(NameApply, 12, y + 53, left - 8, 25);
    place(IdLabel, 12, y + 88, 45, 20);
    place(Identity, 60, y + 88, left - 56, 20);
    place(EditStatus, 12, y + 117, left - 8, 54);
    place(Save, 12, y + 177, left - 8, 32);
    place(Clear, 12, y + 217, left - 8, 26);
    place(NewSoundtrack, mid, y, p.panes.middle, 26);
    place(Sounds, mid, y + 32, p.panes.middle, bottom - y - 32);
    place(Splitter1, p.panes.splitter1_x, y, 10, bottom - y);
    place(Splitter2, p.panes.splitter2_x, y, 10, bottom - y);
    place(AddFiles, right, y, right_width, 26);
    place(Songs, right, y + 32, right_width, bottom - y - 32);
    place(Status, 12, bottom + 8, w - 24, 22);
    place(Progress, 12, bottom + 31, w - 24, 12);
    place(Log, 12, bottom + 49, w - 24, 71);
    place(OpenOutput, 12, h - 37, 144, 27);
    place(OpenBackup, 164, h - 37, 140, 27);
    place(Cancel, w - 116, h - 37, 104, 27);
    InvalidateRect(p.window, nullptr, FALSE);
}
void fonts(Page& p) {
    auto old = p.font;
    p.font =
        CreateFontW(-MulDiv(10, static_cast<int>(p.dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    for (int id = Refresh; id < Last; ++id)
        SendMessageW(item(p, id), WM_SETFONT, reinterpret_cast<WPARAM>(p.font), TRUE);
    if (old)
        DeleteObject(old);
}
void cell(HWND list, int row, int column, const std::wstring& s) {
    LVITEMW v{};
    v.mask = LVIF_TEXT;
    v.iItem = row;
    v.iSubItem = column;
    v.pszText = const_cast<wchar_t*>(s.c_str());
    SendMessageW(list, LVM_SETITEMTEXTW, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&v));
}
void add_row(HWND list, int row, const std::wstring& first) {
    LVITEMW v{};
    v.mask = LVIF_TEXT;
    v.iItem = row;
    v.pszText = const_cast<wchar_t*>(first.c_str());
    SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&v));
}
void populate_songs(Page& p, int selected = -1) {
    p.populating = true;
    HWND list = item(p, Songs);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    const auto& e = p.editor.edit();
    if (p.editor.selected())
        for (size_t i = 0; i < e.rows.size(); ++i) {
            const auto& row = e.rows[i];
            int index = static_cast<int>(i);
            add_row(list, index, std::to_wstring(i + 1));
            cell(list, index, 1, wide(row.track.title));
            cell(list, index, 2, wide(XemuCsb::XboxSoundtrackFileName(e.id, row.track.song_id)));
            cell(list, index, 3, wide(csb::duration(row.track.duration_ms)));
            cell(list, index, 4, row.retained() ? L"HDD (unchanged audio)" : row.local_file.wstring());
        }
    if (selected >= 0 && selected < static_cast<int>(e.rows.size())) {
        ListView_SetItemState(list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selected, FALSE);
    }
    set(p, Name, p.editor.selected() ? wide(e.name) : L"");
    set(p, Identity, p.editor.selected() ? wide(XemuCsb::XboxSoundtrackFolderName(e.id)) : L"--");
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    p.populating = false;
    update(p);
}
void populate_catalog(Page& p) {
    p.populating = true;
    HWND list = item(p, Sounds);
    ListView_DeleteAllItems(list);
    const auto& catalog = p.editor.catalog();
    for (size_t i = 0; i < catalog.soundtracks.size(); ++i) {
        const auto& s = catalog.soundtracks[i];
        int row = static_cast<int>(i);
        add_row(list, row, wide(XemuCsb::XboxSoundtrackFolderName(s.record.soundtrack_id)));
        cell(list, row, 1, wide(s.record.name));
        cell(list, row, 2, std::to_wstring(s.record.tracks.size()));
        cell(list, row, 3, std::to_wstring(s.bytes / 1024) + L" KiB");
    }
    p.populating = false;
    populate_songs(p);
    if (!catalog.warning.empty())
        log_line(p, wide(catalog.warning));
}
bool dirty(Page& p) {
    return p.editor.dirty() ||
           (p.editor.selected() && !p.editor.read_only() && text(p, Name) != wide(p.editor.edit().name));
}
bool discard(Page& p) {
    return !dirty(p) || MessageBoxW(p.window, L"Discard the pending soundtrack edits?\r\nChoose No to keep editing.",
                                    L"Unsaved CSB edits", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}
void apply_name(Page& p) {
    if (p.editor.selected() && !p.editor.read_only())
        p.editor.rename_soundtrack(narrow(text(p, Name)));
}
void end_drag(Page& p) {
    p.dragging = false;
    p.drag_boundary = -1;
    KillTimer(p.window, ScrollTimer);
    InvalidateRect(item(p, Songs), nullptr, FALSE);
    if (GetCapture() == p.window)
        ReleaseCapture();
}
void drag_update(Page& p, bool scroll) {
    if (!p.dragging)
        return;
    HWND list = item(p, Songs);
    POINT pt{};
    GetCursorPos(&pt);
    ScreenToClient(list, &pt);
    RECT area{};
    GetClientRect(list, &area);
    RECT header{};
    GetWindowRect(ListView_GetHeader(list), &header);
    const int top = static_cast<int>(header.bottom - header.top);
    const int bottom = static_cast<int>(area.bottom);
    p.drag_boundary = -1;
    if (pt.x >= 0 && pt.x < area.right && pt.y >= top && pt.y < bottom) {
        if (scroll && pt.y < top + 20)
            ListView_Scroll(list, 0, -20);
        if (scroll && pt.y > bottom - 20)
            ListView_Scroll(list, 0, 20);
        LVHITTESTINFO hit{};
        hit.pt = pt;
        int row = ListView_HitTest(list, &hit);
        if (row >= 0) {
            RECT rect{};
            ListView_GetItemRect(list, row, &rect, LVIR_BOUNDS);
            p.drag_boundary = row + (pt.y >= (rect.top + rect.bottom) / 2 ? 1 : 0);
        } else if (hit.flags & LVHT_NOWHERE)
            p.drag_boundary = static_cast<int>(p.editor.edit().rows.size());
    }
    InvalidateRect(list, nullptr, FALSE);
}
LRESULT draw_song_marker(Page& p, NMLVCUSTOMDRAW* draw) {
    // Report view does not support LVM_SETINSERTMARK. Paint its row boundary after the control.
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
        return CDRF_NOTIFYPOSTPAINT;
    if (draw->nmcd.dwDrawStage != CDDS_POSTPAINT || !p.dragging || p.drag_boundary < 0)
        return CDRF_DODEFAULT;
    const int count = static_cast<int>(p.editor.edit().rows.size());
    if (!count)
        return CDRF_DODEFAULT;
    HWND list = item(p, Songs);
    RECT row{}, area{}, header{};
    if (!ListView_GetItemRect(list, std::min(p.drag_boundary, count - 1), &row, LVIR_BOUNDS))
        return CDRF_DODEFAULT;
    GetClientRect(list, &area);
    GetWindowRect(ListView_GetHeader(list), &header);
    int y = static_cast<int>(p.drag_boundary == count ? row.bottom : row.top);
    const int top = static_cast<int>(header.bottom - header.top);
    const int thick = std::max(2, MulDiv(2, static_cast<int>(p.dpi), 96));
    if (y < top || y > area.bottom)
        return CDRF_DODEFAULT;
    y = std::min(y, std::max(top, static_cast<int>(area.bottom) - thick));
    RECT line{0, y, area.right, y + thick};
    FillRect(draw->nmcd.hdc, &line, GuiThemeAccentBrush());
    return CDRF_DODEFAULT;
}

Options source_options(Page& p) {
    auto o = ui::disk_options(p.shared->state.settings(), false);
    require(!o.source.empty(), "Choose the global Source first.");
    return o;
}
void begin_job(Page& p, bool load, bool remove = false) {
    require(!p.busy && !p.shared->state.busy(), "An operation is already active.");
    end_drag(p);
    if (load && !discard(p))
        return;
    auto options = source_options(p);
    csb::Catalog catalog = p.editor.catalog();
    csb::Edit edit = p.editor.edit();
    csb::SaveOptions save;
    if (!load) {
        apply_name(p);
        edit = p.editor.edit();
        edit.delete_soundtrack = remove;
        require(!p.source_changed, "Source changed. REFRESH before saving.");
        if (remove)
            require(edit.existing && !p.editor.read_only(), "Select a listed soundtrack to delete.");
        else
            require(p.editor.can_save(), "No pending edits or repair to save.");
        save.disk = options;
        save.folder_output = p.shared->state.settings().output_format == ui::Format::Folder;
        save.ffmpeg = p.shared->state.settings().ffmpeg;
        require(!save.disk.destination.empty(), "Choose a NEW output path first.");
        std::wstring message = remove ? L"Delete soundtrack " : L"Save soundtrack ";
        message += wide(XemuCsb::XboxSoundtrackFolderName(edit.id)) + L" - " + wide(edit.name) +
                   L"?\r\n\r\nThis rebuilds a NEW complete disk/folder; the source is not overwritten.\r\n";
        message +=
            L"Backups are retained in: " + (fs::absolute(save.disk.destination).parent_path() / L"Backups").wstring();
        if (!ConfirmWorkspaceOperation(*p.shared, save.disk,
                                       remove ? L"Delete soundtrack in NEW output" : L"Save soundtrack", true, message))
            return;
    }
    if (load && !ConfirmWorkspaceOperation(*p.shared, options, L"Refresh HDD music", false,
                                           L"Read the selected source into the CSB catalog."))
        return;
    if (p.worker.joinable())
        p.worker.join();
    p.context = std::make_shared<Context>();
    auto context = p.context;
    HWND w = p.window;
    context->log = [w](const std::string& message) { post_log(w, message); };
    if (!load)
        p.backup_location = fs::absolute(save.disk.destination).parent_path() / L"Backups";
    auto ticket = BeginWorkspaceJob(*p.shared);
    p.job = ticket.id;
    ConnectSnapshotQuestion(*p.shared, save.disk, context, p.job);
    p.busy = true;
    update(p);
    SetOperationProgress(item(p, Progress), true);
    set(p, Status,
        load ? L"Reading the selected HDD and verifying its catalog..."
             : L"Staging audio, making Backups, and rebuilding a NEW clean output...");
    try {
        p.worker = std::thread([w, context, options, save, catalog, edit, load] {
            auto done = std::make_unique<Completion>();
            done->loaded = load;
            try {
                if (load)
                    done->catalog = csb::read_catalog(options, *context);
                else
                    done->saved = csb::save(catalog, edit, save, *context);
                done->success = true;
            } catch (const std::exception& e) {
                done->error = e.what();
            } catch (...) {
                done->error =
                    "Unexpected CSB failure. Source retained; inspect the reported backup/recovery workspace.";
            }
            if (PostMessageW(w, DoneMessage, 0, reinterpret_cast<LPARAM>(done.get())))
                done.release();
        });
    } catch (...) {
        p.busy = false;
        p.context.reset();
        FinishWorkspaceJob(*p.shared, p.job);
        SetOperationProgress(item(p, Progress), false);
        update(p);
        throw;
    }
}
void command(Page& p, int id) {
    if (id == Cancel) {
        if (p.context)
            p.context->cancelled.store(true);
        set(p, Status, L"Cancelling safely; backup/recovery files will be retained.");
        return;
    }
    if (p.busy || p.shared->state.busy())
        return;
    switch (id) {
    case Refresh:
        begin_job(p, true);
        break;
    case Save:
        begin_job(p, false);
        break;
    case NameApply:
        apply_name(p);
        populate_songs(p);
        break;
    case NewSoundtrack:
        if (discard(p)) {
            p.editor.new_soundtrack();
            p.populating = true;
            ListView_SetItemState(item(p, Sounds), -1, 0, LVIS_SELECTED);
            p.populating = false;
            populate_songs(p);
        }
        break;
    case Clear:
        if (MessageBoxW(p.window, L"Clear all pending song rows? This does not delete anything until a separate save.",
                        L"Clear editor", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
            p.editor.clear();
            populate_songs(p);
        }
        break;
    case AddFiles: {
        auto files = choose_files(p.window, true);
        if (!files.empty()) {
            apply_name(p);
            p.editor.add_files(files);
            populate_songs(p, static_cast<int>(p.editor.edit().rows.size()) - 1);
        }
        break;
    }
    case OpenOutput:
        open_path(p.window, p.last.disk.destination);
        break;
    case OpenBackup:
        open_path(p.window, p.backup_location);
        break;
    }
}
void rename_song(Page& p, int index) {
    if (index < 0 || p.busy || p.editor.read_only())
        return;
    auto value = prompt(p, wide(p.editor.edit().rows.at(static_cast<size_t>(index)).track.title),
                        L"Rename track (maximum 31 UTF-16 units)");
    if (value) {
        apply_name(p);
        p.editor.rename_track(static_cast<size_t>(index), narrow(*value));
        populate_songs(p, index);
    }
}
void notify(Page& p, NMHDR* h) {
    if (p.populating || p.busy || p.shared->state.busy())
        return;
    if (h->idFrom == Sounds && h->code == LVN_ITEMCHANGED) {
        const auto* changed = reinterpret_cast<NMLISTVIEW*>(h);
        if (!(changed->uNewState & LVIS_SELECTED) || (changed->uOldState & LVIS_SELECTED))
            return;
        int row = changed->iItem;
        if (row < 0 || row >= static_cast<int>(p.editor.catalog().soundtracks.size()))
            return;
        const auto id = p.editor.catalog().soundtracks[static_cast<size_t>(row)].record.soundtrack_id;
        if (p.editor.selected() && p.editor.edit().existing && p.editor.edit().id == id)
            return;
        if (discard(p)) {
            p.editor.select(id);
            populate_songs(p);
        } else {
            p.populating = true;
            ListView_SetItemState(item(p, Sounds), row, 0, LVIS_SELECTED);
            if (p.editor.selected() && p.editor.edit().existing)
                for (size_t i = 0; i < p.editor.catalog().soundtracks.size(); ++i)
                    if (p.editor.catalog().soundtracks[i].record.soundtrack_id == p.editor.edit().id)
                        ListView_SetItemState(item(p, Sounds), static_cast<int>(i), LVIS_SELECTED, LVIS_SELECTED);
            p.populating = false;
        }
    } else if (h->idFrom == Songs && h->code == NM_DBLCLK && !p.editor.read_only())
        rename_song(p, selection(item(p, Songs)));
    else if (h->idFrom == Songs && h->code == LVN_BEGINDRAG && !p.editor.read_only()) {
        int row = reinterpret_cast<NMLISTVIEW*>(h)->iItem;
        if (row < 0)
            return;
        apply_name(p);
        p.dragging = true;
        p.drag_from = static_cast<size_t>(row);
        p.drag_generation = p.editor.generation();
        SetCapture(p.window);
        SetTimer(p.window, ScrollTimer, 90, nullptr);
        drag_update(p, false);
    } else if ((h->idFrom == Songs || h->idFrom == Sounds) && h->code == NM_RCLICK) {
        const bool song = h->idFrom == Songs;
        HWND list = item(p, song ? Songs : Sounds);
        const int row = selection(list);
        if (row < 0 || p.editor.read_only() || !p.editor.selected())
            return;
        POINT clicked{};
        GetCursorPos(&clicked);
        ScreenToClient(list, &clicked);
        LVHITTESTINFO hit{};
        hit.pt = clicked;
        // A dirty-switch refusal may restore a different selection. Never act on that old row.
        if (ListView_HitTest(list, &hit) != row)
            return;
        HMENU menu = CreatePopupMenu();
        require(menu != nullptr, "Cannot create context menu.");
        if (song) {
            AppendMenuW(menu, MF_STRING, 1, L"Rename");
            AppendMenuW(menu, MF_STRING, 2, L"Delete track (pending edit)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 3, L"Move up");
            AppendMenuW(menu, MF_STRING, 4, L"Move down");
        } else
            AppendMenuW(menu, MF_STRING, 5, L"Delete soundtrack in new output...");
        POINT pt{};
        GetCursorPos(&pt);
        int action = static_cast<int>(TrackGuiThemePopup(menu, TPM_RETURNCMD | TPM_NONOTIFY, static_cast<int>(pt.x),
                                                         static_cast<int>(pt.y), p.window));
        DestroyMenu(menu);
        if (action == 1)
            rename_song(p, row);
        if (action == 2) {
            apply_name(p);
            p.editor.remove_track(static_cast<size_t>(row));
            populate_songs(p, row);
        }
        if (action == 3 && row > 0) {
            apply_name(p);
            p.editor.reorder(static_cast<size_t>(row), static_cast<size_t>(row - 1), p.editor.generation());
            populate_songs(p, row - 1);
        }
        if (action == 4 && row + 1 < static_cast<int>(p.editor.edit().rows.size())) {
            apply_name(p);
            p.editor.reorder(static_cast<size_t>(row), static_cast<size_t>(row + 2), p.editor.generation());
            populate_songs(p, row + 1);
        }
        if (action == 5)
            begin_job(p, false, true);
    } else if (h->idFrom == Songs && h->code == LVN_KEYDOWN) {
        auto* key = reinterpret_cast<NMLVKEYDOWN*>(h);
        int row = selection(item(p, Songs));
        if (key->wVKey == VK_ESCAPE) {
            end_drag(p);
            return;
        }
        if (key->wVKey == VK_F2 && !p.editor.read_only())
            rename_song(p, row);
        if (key->wVKey == VK_DELETE && row >= 0 && !p.editor.read_only()) {
            apply_name(p);
            p.editor.remove_track(static_cast<size_t>(row));
            populate_songs(p, row);
        }
    }
}
void create(Page& p) {
    auto add = [&](int id, const wchar_t* cls, const wchar_t* caption, DWORD style, DWORD ex = 0) {
        require(CreateWindowExW(ex, cls, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, p.window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
                                nullptr) != nullptr,
                "Cannot create CSB control.");
    };
    add(Refresh, L"BUTTON", L"REFRESH HDD MUSIC", WS_TABSTOP);
    add(BackupLabel, L"STATIC", L"", SS_NOPREFIX);
    add(NameLabel, L"STATIC", L"Soundtrack name", SS_NOPREFIX);
    add(Name, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE);
    SendMessageW(item(p, Name), EM_SETLIMITTEXT, 63, 0);
    add(NameApply, L"BUTTON", L"Apply name", WS_TABSTOP);
    add(IdLabel, L"STATIC", L"ID", SS_NOPREFIX);
    add(Identity, L"STATIC", L"--", SS_NOPREFIX);
    add(EditStatus, L"STATIC", L"No pending edits.", SS_NOPREFIX);
    add(Save, L"BUTTON", L"SAVE TO NEW OUTPUT", WS_TABSTOP);
    add(Clear, L"BUTTON", L"Clear Editor", WS_TABSTOP);
    add(NewSoundtrack, L"BUTTON", L"NEW SOUNDTRACK", WS_TABSTOP);
    add(AddFiles, L"BUTTON", L"Add audio files... (or drop files onto Songs)", WS_TABSTOP);
    for (int id : {Sounds, Songs}) {
        add(id, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(item(p, id), LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER |
                                                           LVS_EX_LABELTIP);
    }
    auto column = [&](int list, int index, const wchar_t* name, int width) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.iSubItem = index;
        c.pszText = const_cast<wchar_t*>(name);
        c.cx = MulDiv(width, p.dpi, 96);
        SendMessageW(item(p, list), LVM_INSERTCOLUMNW, index, reinterpret_cast<LPARAM>(&c));
    };
    column(Sounds, 0, L"ID", 58);
    column(Sounds, 1, L"Sound Track Name", 165);
    column(Sounds, 2, L"Songs Count", 86);
    column(Sounds, 3, L"Size", 90);
    column(Songs, 0, L"#", 40);
    column(Songs, 1, L"Track Name", 230);
    column(Songs, 2, L"Xbox File", 124);
    column(Songs, 3, L"Duration", 72);
    column(Songs, 4, L"Source", 230);
    add(Status, L"STATIC", L"Load an HDD source. SAVE rebuilds the complete disk into a new output.", SS_NOPREFIX);
    add(Progress, PROGRESS_CLASSW, L"", 0);
    CreateXhcSplitter(p.window, Splitter1, L"Editor / Soundtracks divider");
    CreateXhcSplitter(p.window, Splitter2, L"Soundtracks / Songs divider");
    add(Log, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE);
    SendMessageW(item(p, Log), EM_SETLIMITTEXT, 1000000, 0);
    add(Cancel, L"BUTTON", L"Cancel", WS_TABSTOP);
    add(OpenOutput, L"BUTTON", L"Open Last Output", WS_TABSTOP);
    add(OpenBackup, L"BUTTON", L"Open Backups", WS_TABSTOP);
    SetOperationProgress(item(p, Progress), false);
    fonts(p);
    layout(p);
    update(p);
    DragAcceptFiles(p.window, TRUE);
    log_line(
        p,
        L"TEST12 soundtrack format. Existing WMA files keep their bytes/IDs. New local audio uses external FFmpeg, WMA2 stereo 44.1 kHz / 128 kbps.");
    log_line(
        p,
        L"SAVE rebuilds a NEW clean disk/folder. Backups retain only the original database and selected soundtrack; the complete source is left untouched.");
}
LRESULT CALLBACK procedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    Page* p = page_state(w);
    try {
        if (msg == WM_CREATE) {
            p = new Page;
            p->window = w;
            p->owner = GetParent(w);
            p->shared = static_cast<GuiWorkspace*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            p->dpi = p->shared->dpi;
            SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
            create(*p);
            return 0;
        }
        if (!p)
            return DefWindowProcW(w, msg, wp, lp);
        switch (msg) {
        case WM_SIZE:
            layout(*p);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == Name && HIWORD(wp) == EN_CHANGE && !p->populating && !p->busy &&
                !p->shared->state.busy() && p->editor.selected() && !p->editor.read_only()) {
                // An empty temporary name still needs dirty-edit protection; commit validation is deferred.
                p->editor.rename_soundtrack(narrow(text(*p, Name)));
                update(*p);
                return 0;
            }
            command(*p, LOWORD(wp));
            return 0;
        case WM_NOTIFY: {
            auto* h = reinterpret_cast<NMHDR*>(lp);
            if (h->idFrom == Songs && h->code == NM_CUSTOMDRAW)
                return draw_song_marker(*p, reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
            notify(*p, h);
            return 0;
        }
        case XhcSplitMove: {
            int x = MulDiv(static_cast<int>(lp), 96, static_cast<int>(p->dpi));
            if (wp == Splitter1)
                p->left = x - 19;
            else if (wp == Splitter2)
                p->middle = x - p->panes.left - 29;
            layout(*p);
            return 0;
        }
        case XhcSplitReset:
            p->left = 226;
            p->middle = 360;
            layout(*p);
            return 0;
        case WM_MOUSEMOVE:
            if (p->dragging) {
                drag_update(*p, false);
                return 0;
            }
            break;
        case WM_TIMER:
            if (wp == ScrollTimer) {
                drag_update(*p, true);
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (p->dragging) {
                auto from = p->drag_from;
                auto generation = p->drag_generation;
                int boundary = p->drag_boundary;
                end_drag(*p);
                if (boundary >= 0 && p->editor.reorder(from, static_cast<size_t>(boundary), generation))
                    populate_songs(*p, boundary > static_cast<int>(from) ? boundary - 1 : boundary);
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (p->dragging)
                end_drag(*p);
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                end_drag(*p);
                if (GetCapture() == w)
                    ReleaseCapture();
                SendMessageW(item(*p, Splitter1), WM_CANCELMODE, 0, 0);
                SendMessageW(item(*p, Splitter2), WM_CANCELMODE, 0, 0);
                return 0;
            }
            break;
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            struct Finish {
                HDROP d;
                ~Finish() { DragFinish(d); }
            } finish{drop};
            if (p->busy || p->shared->state.busy())
                return 0;
            POINT point{};
            DragQueryPoint(drop, &point);
            if (lp == 1)
                MapWindowPoints(p->owner, w, &point, 1);
            RECT bounds{};
            GetWindowRect(item(*p, Songs), &bounds);
            ClientToScreen(w, &point);
            require(PtInRect(&bounds, point) != 0,
                    "Drop audio onto the Songs pane. Use the global Source Browse button to choose an HDD source.");
            UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
            require(count <= 504, "Too many dropped audio files.");
            std::vector<fs::path> paths;
            for (UINT i = 0; i < count; ++i) {
                UINT size = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring value(static_cast<size_t>(size) + 1, L'\0');
                DragQueryFileW(drop, i, value.data(), size + 1);
                value.resize(size);
                paths.emplace_back(value);
            }
            apply_name(*p);
            p->editor.add_files(paths);
            populate_songs(*p);
            return 0;
        }
        case LogMessage: {
            std::unique_ptr<std::wstring> line(reinterpret_cast<std::wstring*>(lp));
            log_line(*p, *line);
            return 0;
        }
        case DoneMessage: {
            std::unique_ptr<Completion> done(reinterpret_cast<Completion*>(lp));
            if (p->worker.joinable())
                p->worker.join();
            p->busy = false;
            p->context.reset();
            FinishWorkspaceJob(*p->shared, p->job);
            SetOperationProgress(item(*p, Progress), false);
            if (done->success && done->loaded) {
                p->editor.set_catalog(std::move(done->catalog));
                p->source_changed = false;
                populate_catalog(*p);
                set(*p, Status, L"Catalog loaded. Select a soundtrack or NEW SOUNDTRACK.");
            } else if (done->success) {
                p->last = done->saved;
                p->backup_location = done->saved.backup;
                p->editor.set_catalog({});
                p->source_changed = true;
                populate_catalog(*p);
                set(*p, Status,
                    L"Verified NEW output saved. Source selection unchanged. Refresh the original, or select the new output above.");
            } else {
                log_line(*p, wide(done->error));
                set(*p, Status, L"Not completed. Original retained; see backup/recovery paths below.");
                if (!p->closing)
                    MessageBoxW(w, wide(done->error).c_str(), L"CSB operation not completed", MB_OK | MB_ICONWARNING);
            }
            update(*p);
            if (p->closing)
                PostMessageW(p->owner, WM_CLOSE, 0, 0);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(w, &paint);
            RECT r{};
            GetClientRect(w, &r);
            FillRect(dc, &r, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
            EndPaint(w, &paint);
            return 0;
        }
        case WM_NCDESTROY:
            if (p->context)
                p->context->cancelled.store(true);
            if (p->worker.joinable())
                p->worker.join();
            DragAcceptFiles(w, FALSE);
            if (p->font)
                DeleteObject(p->font);
            SetWindowLongPtrW(w, GWLP_USERDATA, 0);
            delete p;
            return 0;
        }
    } catch (const std::exception& e) {
        MessageBoxW(w, wide(e.what()).c_str(), L"Custom Soundtrack Builder", MB_OK | MB_ICONWARNING);
        if (msg == WM_CREATE)
            return -1;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
} // namespace
HWND CreateCsbPage(GuiWorkspace& shared) {
    HWND parent = shared.owner;
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = procedure;
    cls.lpszClassName = L"XhcCsbPage";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw xhc::Error("Cannot register CSB page.");
    HWND w = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 100, 100,
                             parent, nullptr, cls.hInstance, &shared);
    xhc::require(w != nullptr, "Cannot create CSB tab.");
    return w;
}
bool CsbPageBusy(HWND w) {
    auto* p = page_state(w);
    return p && p->busy;
}
bool CsbPageCanClose(HWND w) {
    auto* p = page_state(w);
    if (!p)
        return true;
    if (p->busy) {
        if (p->closing)
            return false;
        if (MessageBoxW(
                w,
                L"Cancel CSB and close after safe shutdown?\r\nThe original and uncertain recovery files will be retained.",
                L"CSB is active", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
            p->closing = true;
            p->discard_on_close = true;
            if (p->context)
                p->context->cancelled.store(true);
            update(*p);
        }
        return false;
    }
    return p->discard_on_close || discard(*p);
}
void CsbPageDpi(HWND w, unsigned dpi) {
    if (auto* p = page_state(w)) {
        p->dpi = dpi;
        fonts(*p);
        layout(*p);
    }
}

bool CsbPageDirty(HWND w) {
    auto* p = page_state(w);
    return p && dirty(*p);
}
void CsbPageInvalidate(HWND w) {
    if (auto* p = page_state(w)) {
        require(!p->busy, "CSB is busy.");
        end_drag(*p);
        p->editor.set_catalog({});
        p->source_changed = true;
        populate_catalog(*p);
        set(*p, Status, L"Global source changed. REFRESH HDD MUSIC to load it.");
    }
}
void CsbPageAction(HWND w, CsbUiAction action) {
    auto* p = page_state(w);
    if (!p || p->busy || p->shared->state.busy())
        return;
    if (action == CsbUiAction::ResetPanes) {
        p->left = 226;
        p->middle = 360;
        layout(*p);
        return;
    }
    int id = action == CsbUiAction::AddFiles     ? AddFiles
             : action == CsbUiAction::Clear      ? Clear
             : action == CsbUiAction::OpenOutput ? OpenOutput
                                                 : OpenBackup;
    if (IsWindowEnabled(item(*p, id)))
        command(*p, id);
}

void CsbPageKeepOpen(HWND w) {
    if (auto* p = page_state(w))
        if (!p->busy) {
            p->closing = false;
            p->discard_on_close = false;
            update(*p);
        }
}
