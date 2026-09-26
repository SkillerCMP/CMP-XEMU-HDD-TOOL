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
#include "gui_hdd_win32.hpp"
#include "gui_workspace_win32.hpp"
#include "gui_theme_win32.hpp"
#include "gui_progress_win32.hpp"
#include "xhc/hdd.hpp"
#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <thread>
#include <set>

namespace {
using namespace xhc;
constexpr UINT LogMessage = WM_APP + 51, DoneMessage = WM_APP + 52;
enum Id {
    Refresh = 3000,
    SafetyLabel,
    Partitions,
    Location,
    Up,
    FilterLabel,
    Filter,
    Entries,
    ImportFile,
    ImportFolder,
    NewFolder,
    Export,
    Save,
    Discard,
    Status,
    Progress,
    Log,
    OpenOutput,
    OpenBackup,
    Cancel,
    Last
};
enum Action { Load = 1, Import = 2, Replace = 3, Write = 4, ExportItems = 5 };
enum Menu {
    CopyPath = 4100,
    Rename,
    Delete,
    ReplaceAction,
    Properties,
    SelectCopy,
    SelectMove,
    Paste,
    OpenFolder,
    ExportCurrent
};
struct Completion {
    bool success = false;
    Action action = Load;
    std::string error;
    hdd::Editor editor;
    hdd::Saved saved;
    fs::path exported;
};
struct Page {
    HWND window = nullptr, owner = nullptr;
    HFONT font = nullptr;
    unsigned dpi = 96;
    bool busy = false, populating = false, source_changed = false, closing = false, discard_on_close = false;
    GuiWorkspace* shared = nullptr;
    uint64_t job_id = 0;
    hdd::Editor editor;
    hdd::Path current{'E', {}};
    std::vector<hdd::Path> rows;
    std::set<std::string> expanded;
    std::optional<hdd::Path> clipboard;
    bool move = false;
    hdd::Saved last;
    fs::path last_export, backup_location;
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
fs::path choose_folder(HWND owner, const wchar_t* title) {
    IFileOpenDialog* dialog = nullptr;
    require(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                       reinterpret_cast<void**>(&dialog))),
            "Cannot open folder picker.");
    struct Release {
        IFileOpenDialog* p;
        ~Release() { p->Release(); }
    } release{dialog};
    FILEOPENDIALOGOPTIONS options{};
    HRESULT hr = dialog->GetOptions(&options);
    if (SUCCEEDED(hr))
        hr = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    if (SUCCEEDED(hr))
        hr = dialog->SetTitle(title);
    if (SUCCEEDED(hr))
        hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    require(SUCCEEDED(hr), "Folder picker failed.");
    IShellItem* selected = nullptr;
    require(SUCCEEDED(dialog->GetResult(&selected)), "Cannot read folder selection.");
    PWSTR raw = nullptr;
    hr = selected->GetDisplayName(SIGDN_FILESYSPATH, &raw);
    selected->Release();
    require(SUCCEEDED(hr), "Selection is not a filesystem path.");
    fs::path result(raw);
    CoTaskMemFree(raw);
    return result;
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
std::optional<hdd::Path> selected(Page& p) {
    int i = selection(item(p, Entries));
    if (i < 0 || static_cast<size_t>(i) >= p.rows.size())
        return {};
    return p.rows[static_cast<size_t>(i)];
}
hdd::Path target(Page& p) {
    if (auto path = selected(p))
        if (hdd::lookup(p.editor.volumes(), *path).directory())
            return *path;
    return p.current;
}
bool discard_prompt(Page& p) {
    if (!p.editor.dirty())
        return true;
    return MessageBoxW(p.window, L"Discard the pending HDD Directory edits?\r\nNothing has been written to the source.",
                       L"Pending HDD edits", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}
void update(Page& p) {
    const bool idle = !p.busy && !p.shared->state.busy(), loaded = idle && p.editor.loaded() && !p.source_changed;
    EnableWindow(item(p, Refresh), idle);
    for (int id : {Partitions, Entries, Filter, Up, ImportFile, ImportFolder, NewFolder})
        EnableWindow(item(p, id), loaded);
    EnableWindow(item(p, Export), loaded && !p.editor.dirty());
    EnableWindow(item(p, Save), loaded && p.editor.dirty());
    EnableWindow(item(p, Discard), loaded && p.editor.dirty());
    EnableWindow(item(p, Cancel), p.busy && !p.closing);
    EnableWindow(item(p, OpenOutput), idle && (!p.last.disk.destination.empty() || !p.last_export.empty()));
    EnableWindow(item(p, OpenBackup), idle && !p.backup_location.empty());
}
void layout(Page& p) {
    RECT r{};
    GetClientRect(p.window, &r);
    int w = MulDiv(static_cast<int>(r.right), 96, static_cast<int>(p.dpi)),
        h = MulDiv(static_cast<int>(r.bottom), 96, static_cast<int>(p.dpi));
    auto place = [&](int id, int x, int y, int width, int height) {
        MoveWindow(item(p, id), MulDiv(x, p.dpi, 96), MulDiv(y, p.dpi, 96), MulDiv(std::max(1, width), p.dpi, 96),
                   MulDiv(std::max(1, height), p.dpi, 96), TRUE);
    };
    place(SafetyLabel, 12, 8, w - 230, 46);
    place(Refresh, w - 208, 8, 196, 27);
    place(Partitions, 12, 61, w - 24, 34);
    SendMessageW(item(p, Partitions), TCM_SETITEMSIZE, 0,
                 MAKELPARAM(MulDiv(122, static_cast<int>(p.dpi), 96), MulDiv(28, static_cast<int>(p.dpi), 96)));
    place(Up, 12, 104, 65, 26);
    place(Location, 84, 104, w - 397, 26);
    place(FilterLabel, w - 305, 107, 47, 22);
    place(Filter, w - 256, 104, 244, 26);
    int bottom = h - 161;
    place(Entries, 12, 138, w - 24, bottom - 180);
    place(ImportFile, 12, bottom - 34, 108, 26);
    place(ImportFolder, 127, bottom - 34, 120, 26);
    place(NewFolder, 254, bottom - 34, 108, 26);
    place(Export, 369, bottom - 34, 112, 26);
    place(Discard, 488, bottom - 34, 110, 26);
    place(Save, w - 240, bottom - 34, 228, 26);
    place(Status, 12, bottom + 1, w - 24, 22);
    place(Progress, 12, bottom + 27, w - 24, 12);
    place(Log, 12, bottom + 45, w - 24, 66);
    place(OpenOutput, 12, h - 38, 151, 27);
    place(OpenBackup, 171, h - 38, 141, 27);
    place(Cancel, w - 116, h - 38, 104, 27);
}
void fonts(Page& p) {
    HFONT old = p.font;
    p.font = CreateFontW(-MulDiv(10, p.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    for (int id = Refresh; id < Last; ++id)
        SendMessageW(item(p, id), WM_SETFONT, reinterpret_cast<WPARAM>(p.font), TRUE);
    if (old)
        DeleteObject(old);
}
void populate(Page& p) {
    p.populating = true;
    HWND list = item(p, Entries);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    p.rows.clear();
    if (p.editor.loaded() && !p.source_changed) {
        try {
            hdd::lookup(p.editor.volumes(), p.current);
        } catch (const Error&) {
            p.current.components.clear();
        }
        const auto& directory = hdd::lookup(p.editor.volumes(), p.current);
        std::string filter = ascii_lower(narrow(text(p, Filter)));
        std::function<bool(const Node&)> matches = [&](const Node& n) {
            if (filter.empty() || ascii_lower(n.name).find(filter) != std::string::npos)
                return true;
            return std::any_of(n.children.begin(), n.children.end(), [&](const Node& child) { return matches(child); });
        };
        std::function<void(const Node&, const hdd::Path&, unsigned)> append = [&](const Node& dir,
                                                                                  const hdd::Path& parent,
                                                                                  unsigned depth) {
            std::vector<const Node*> children;
            for (const auto& n : dir.children)
                if (matches(n))
                    children.push_back(&n);
            std::sort(children.begin(), children.end(), [](auto* a, auto* b) {
                return a->directory() != b->directory() ? a->directory() : ascii_lower(a->name) < ascii_lower(b->name);
            });
            for (const auto* n : children) {
                int row = static_cast<int>(p.rows.size());
                const auto path = parent.child(n->name);
                p.rows.push_back(path);
                const bool expanded = p.expanded.count(ascii_lower(path.str())) != 0;
                std::wstring name = std::wstring(depth * 3, L' ') +
                                    (n->directory() ? (expanded ? L"[-] " : L"[+] ") : L"    ") + wide(n->name);
                LVITEMW cell{};
                cell.mask = LVIF_TEXT;
                cell.iItem = row;
                cell.pszText = name.data();
                ListView_InsertItem(list, &cell);
                std::vector<std::wstring> values = {
                    n->directory() ? L"Folder" : L"File", n->directory() ? L"--" : std::to_wstring(n->length),
                    n->replacement.empty() ? (n->chain.empty() ? L"--" : std::to_wstring(n->chain.front()))
                                           : L"New file",
                    wide(hdd::modified_time(*n)), wide(hdd::attributes(n->attributes))};
                for (size_t col = 0; col < values.size(); ++col)
                    ListView_SetItemText(list, row, static_cast<int>(col + 1), values[col].data());
                if (n->directory() && (expanded || !filter.empty()))
                    append(*n, path, depth + 1);
            }
        };
        append(directory, p.current, 0);
        set(p, Location, wide(p.current.str()));
    } else
        set(p, Location, L"Load a source first");
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    p.populating = false;
    update(p);
}
void show_properties(Page& p, const hdd::Path& path) {
    const auto& n = hdd::lookup(p.editor.volumes(), path);
    std::wstring s = wide(path.str()) + L"\r\n" + (n.directory() ? L"Directory" : L"File") + L"\r\nBytes: " +
                     std::to_wstring(n.length) + L"\r\nModified: " + wide(hdd::modified_time(n)) + L"\r\nAttributes: " +
                     wide(hdd::attributes(n.attributes));
    if (!n.directory())
        s += L"\r\nSHA-256: " + wide(n.digest);
    if (!n.replacement.empty())
        s += L"\r\nPending host file: " + n.replacement.wstring();
    MessageBoxW(p.window, s.c_str(), L"FATX properties (source is read-only)", MB_OK | MB_ICONINFORMATION);
}
void copy_path(Page& p, const hdd::Path& path) {
    auto text = wide(path.str());
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    require(OpenClipboard(p.window) != FALSE, "Cannot open the Windows clipboard.");
    struct Close {
        ~Close() { CloseClipboard(); }
    } close;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    require(mem != nullptr, "Cannot allocate clipboard text.");
    void* dest = GlobalLock(mem);
    if (!dest) {
        GlobalFree(mem);
        throw Error("Cannot lock clipboard text.");
    }
    memcpy(dest, text.c_str(), bytes);
    GlobalUnlock(mem);
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        throw Error("Cannot copy FATX path.");
    }
}
Options source_options(Page& p) {
    auto o = ui::disk_options(p.shared->state.settings(), false);
    require(!o.source.empty(), "Choose the global Source first.");
    return o;
}
void job(Page& p, Action action, const std::vector<fs::path>& paths = {},
         std::optional<hdd::Path> explicit_target = {}) {
    require(!p.busy && !p.shared->state.busy(), "An HDD operation is active.");
    if (action == Load && !discard_prompt(p))
        return;
    auto options = source_options(p);
    auto dest = explicit_target ? *explicit_target : target(p);
    hdd::SaveOptions save;
    fs::path export_dest;
    if (action != Load)
        require(p.editor.loaded() && !p.source_changed, "REFRESH HDD before operating on this source.");
    if (action == Write) {
        require(p.editor.dirty(), "No pending changes.");
        save.disk = options;
        save.folder_output = p.shared->state.settings().output_format == ui::Format::Folder;
        require(!save.disk.destination.empty(), "Choose a NEW output path first.");
        std::wstring message =
            L"Rebuild all pending HDD changes to a NEW output?\r\n\r\n" + save.disk.destination.wstring() +
            L"\r\n\r\nOriginal HDD remains unchanged. Changed/deleted originals go in Backups\\HDD-<transaction>.";
        if (!ConfirmWorkspaceOperation(*p.shared, save.disk, L"Save HDD Directory edits", true, message))
            return;
        p.backup_location = fs::absolute(save.disk.destination).parent_path() / L"Backups";
    }
    if (action == ExportItems) {
        require(!p.editor.dirty(), "Save or discard pending edits before exporting the source.");
        auto parent = choose_folder(p.window, L"Choose parent for a NEW export folder");
        if (parent.empty())
            return;
        export_dest = parent / (L"HDD-Export-" + wide(random_token()));
    }
    if (action != Write) {
        std::wstring caption = action == Load          ? L"Refresh HDD Directory"
                               : action == ExportItems ? L"Export HDD files"
                               : action == Replace     ? L"Queue file replacement"
                                                       : L"Queue file import";
        std::wstring detail = action == ExportItems
                                  ? L"Export destination: " + export_dest.wstring()
                                  : L"Original disk is read-only. Pending file changes require a separate SAVE.";
        if (!ConfirmWorkspaceOperation(*p.shared, options, caption, false, detail))
            return;
    }
    if (p.worker.joinable())
        p.worker.join();
    p.context = std::make_shared<Context>();
    auto context = p.context;
    HWND window = p.window;
    context->log = [window](const std::string& s) { post_log(window, s); };
    auto editor = p.editor;
    auto ticket = BeginWorkspaceJob(*p.shared);
    p.job_id = ticket.id;
    ConnectSnapshotQuestion(*p.shared, save.disk, context, p.job_id);
    p.busy = true;
    update(p);
    SetOperationProgress(item(p, Progress), true);
    set(p, Status, L"Working offline. Source remains read-only...");
    try {
        p.worker = std::thread([window, context, options, save, export_dest, paths, dest, action,
                                editor = std::move(editor)]() mutable {
            auto done = std::make_unique<Completion>();
            done->action = action;
            try {
                if (action == Load)
                    editor.load(hdd::read_catalog(options, *context));
                else if (action == Import)
                    editor.import_paths(dest, paths, *context);
                else if (action == Replace) {
                    require(paths.size() == 1, "Select one replacement file.");
                    editor.replace_file(dest, paths.front(), *context);
                } else if (action == Write)
                    done->saved = hdd::save(editor, save, *context);
                else if (action == ExportItems)
                    done->exported = hdd::export_items(editor.original(), options, {dest}, export_dest, *context);
                done->editor = std::move(editor);
                done->success = true;
            } catch (const std::exception& e) {
                done->error = e.what();
            } catch (...) {
                done->error = "Unexpected HDD operation failure. Original retained; inspect reported recovery paths.";
            }
            if (PostMessageW(window, DoneMessage, 0, reinterpret_cast<LPARAM>(done.get())))
                done.release();
        });
    } catch (...) {
        p.busy = false;
        p.context.reset();
        FinishWorkspaceJob(*p.shared, p.job_id);
        SetOperationProgress(item(p, Progress), false);
        update(p);
        throw;
    }
}
void command(Page& p, int id) {
    if (id == Cancel) {
        if (p.context)
            p.context->cancelled.store(true);
        set(p, Status, L"Cancelling safely; uncertain work is retained.");
        return;
    }
    if (p.busy || p.shared->state.busy())
        return;
    auto selection = selected(p);
    switch (id) {
    case Refresh:
        job(p, Load);
        break;
    case Save:
        job(p, Write);
        break;
    case Discard:
        if (discard_prompt(p)) {
            p.editor.discard();
            p.clipboard.reset();
            populate(p);
            set(p, Status, L"Pending edits discarded. Original source unchanged.");
        }
        break;
    case Up:
        p.current = p.current.parent();
        populate(p);
        break;
    case ImportFile: {
        auto v = choose_files(p.window, false);
        if (!v.empty())
            job(p, Import, v);
        break;
    }
    case ImportFolder: {
        auto v = choose_folder(p.window, L"Import a folder with its hierarchy");
        if (!v.empty())
            job(p, Import, {v});
        break;
    }
    case ReplaceAction: {
        require(selection.has_value(), "Select a file to replace.");
        auto v = choose_files(p.window, false);
        if (v.empty())
            break;
        if (MessageBoxW(p.window,
                        L"Replace this file in the pending output? Its old contents will be backed up when you SAVE.",
                        L"Replace file", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
            job(p, Replace, v, selection);
        break;
    }
    case Export:
        job(p, ExportItems, {}, selection ? selection : std::optional<hdd::Path>(p.current));
        break;
    case ExportCurrent:
        job(p, ExportItems, {}, p.current);
        break;
    case NewFolder: {
        auto name = prompt(p, L"New Folder", L"New FATX folder");
        if (name) {
            p.editor.new_folder(target(p), narrow(*name));
            populate(p);
            set(p, Status, L"New folder pending. SAVE to a new output to apply.");
        }
        break;
    }
    case Rename: {
        require(selection.has_value(), "Select an item to rename.");
        auto name = prompt(p, wide(hdd::lookup(p.editor.volumes(), *selection).name), L"Rename FATX item");
        if (name) {
            p.editor.rename(*selection, narrow(*name));
            p.clipboard.reset();
            populate(p);
            set(p, Status, L"Rename pending. Source unchanged.");
        }
        break;
    }
    case Delete: {
        require(selection.has_value(), "Select an item to delete.");
        auto message =
            L"Delete " + wide(selection->str()) +
            L" from the NEW output?\r\nDirectories include their descendants. Nothing is deleted from the source.";
        if (MessageBoxW(p.window, message.c_str(), L"Confirm pending delete",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
            p.editor.remove(*selection);
            p.clipboard.reset();
            populate(p);
            set(p, Status, L"Deletion pending. SAVE creates a new clean output and retains backups.");
        }
        break;
    }
    case CopyPath:
        copy_path(p, selection ? *selection : p.current);
        break;
    case Properties:
        show_properties(p, selection ? *selection : p.current);
        break;
    case OpenFolder:
        if (selection && hdd::lookup(p.editor.volumes(), *selection).directory()) {
            p.current = *selection;
            set(p, Filter, L"");
            populate(p);
        }
        break;
    case SelectCopy:
    case SelectMove:
        require(selection.has_value(), "Select an item first.");
        p.clipboard = selection;
        p.move = id == SelectMove;
        set(p, Status, (p.move ? L"Selected for move: " : L"Selected for copy: ") + wide(selection->str()));
        break;
    case Paste:
        require(p.clipboard.has_value(), "Select an item for copy/move first.");
        p.editor.copy_move(*p.clipboard, target(p), p.move);
        if (p.move)
            p.clipboard.reset();
        populate(p);
        set(p, Status, L"Copy/move pending. Source unchanged.");
        break;
    case OpenOutput:
        open_path(p.window, p.last_export.empty() ? p.last.disk.destination : p.last_export);
        break;
    case OpenBackup:
        open_path(p.window, p.backup_location);
        break;
    }
    update(p);
}
void context_menu(Page& p) {
    if (p.busy || p.source_changed || !p.editor.loaded())
        return;
    HWND list = item(p, Entries);
    POINT screen{};
    GetCursorPos(&screen);
    POINT local = screen;
    ScreenToClient(list, &local);
    LVHITTESTINFO hit{};
    hit.pt = local;
    int row = ListView_HitTest(list, &hit);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
    if (row >= 0) {
        ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    auto path = selected(p);
    bool file = path && !hdd::lookup(p.editor.volumes(), *path).directory();
    HMENU menu = CreatePopupMenu();
    require(menu != nullptr, "Cannot create HDD context menu.");
    auto add = [&](int id, const wchar_t* name, bool enabled = true) {
        AppendMenuW(menu, MF_STRING | (enabled ? 0 : MF_GRAYED), static_cast<UINT_PTR>(id), name);
    };
    add(OpenFolder, L"Open folder", path && !file);
    add(Export, L"Export selected...", path && !p.editor.dirty());
    add(ExportCurrent, L"Export this directory...", !p.editor.dirty());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(ImportFile, L"Import File...");
    add(ImportFolder, L"Import Folder...");
    add(NewFolder, L"New Folder...");
    add(ReplaceAction, L"Replace File...", file);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(Rename, L"Rename...", path.has_value());
    add(SelectCopy, L"Select for Copy", path.has_value());
    add(SelectMove, L"Select for Move", path.has_value());
    add(Paste, p.move ? L"Move Selected Here" : L"Copy Selected Here", p.clipboard.has_value());
    add(Delete, L"Delete from NEW output...", path.has_value());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(CopyPath, L"Copy FATX Path");
    add(Properties, L"Properties / SHA-256");
    int action = static_cast<int>(TrackGuiThemePopup(menu, TPM_RETURNCMD | TPM_NONOTIFY, static_cast<int>(screen.x),
                                                     static_cast<int>(screen.y), p.window));
    DestroyMenu(menu);
    if (action)
        command(p, action);
}
void controls(Page& p) {
    auto add = [&](int id, const wchar_t* cls, const wchar_t* caption, DWORD style = 0, DWORD ex = 0) {
        require(CreateWindowExW(ex, cls, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, p.window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
                                nullptr) != nullptr,
                "Cannot create HDD Directory control.");
    };
    add(Refresh, L"BUTTON", L"REFRESH HDD", WS_TABSTOP);
    add(SafetyLabel, L"STATIC",
        L"Edits are pending until SAVE TO NEW OUTPUT. Backups: <output parent>\\Backups\\HDD-<transaction>.\r\nFile operations do not update ST.DB: use the CSB tab for soundtrack changes. Copy/move: same partition.",
        SS_NOPREFIX);
    add(Partitions, WC_TABCONTROLW, L"", WS_TABSTOP | TCS_FIXEDWIDTH);
    TCITEMW tab{};
    tab.mask = TCIF_TEXT;
    int i = 0;
    for (const wchar_t* name : {L"C: System", L"E: Data", L"X: Cache", L"Y: Cache", L"Z: Cache"}) {
        tab.pszText = const_cast<wchar_t*>(name);
        SendMessageW(item(p, Partitions), TCM_INSERTITEMW, static_cast<WPARAM>(i++), reinterpret_cast<LPARAM>(&tab));
    }
    SendMessageW(item(p, Partitions), TCM_SETCURSEL, 1, 0);
    add(Up, L"BUTTON", L"Up", WS_TABSTOP);
    add(Location, L"EDIT", L"Load a source first", ES_AUTOHSCROLL | ES_READONLY, WS_EX_CLIENTEDGE);
    add(FilterLabel, L"STATIC", L"Filter", SS_NOPREFIX);
    add(Filter, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE);
    add(Entries, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(item(p, Entries), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    const wchar_t* labels[] = {L"Name", L"Type", L"Size (bytes)", L"Cluster", L"Modified", L"Attr"};
    int widths[] = {370, 86, 128, 90, 168, 70};
    for (int col = 0; col < 6; ++col) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.iSubItem = col;
        column.cx = MulDiv(widths[col], p.dpi, 96);
        column.pszText = const_cast<wchar_t*>(labels[col]);
        ListView_InsertColumn(item(p, Entries), col, &column);
    }
    add(ImportFile, L"BUTTON", L"Import File...", WS_TABSTOP);
    add(ImportFolder, L"BUTTON", L"Import Folder...", WS_TABSTOP);
    add(NewFolder, L"BUTTON", L"New Folder...", WS_TABSTOP);
    add(Export, L"BUTTON", L"Export...", WS_TABSTOP);
    add(Discard, L"BUTTON", L"Discard edits", WS_TABSTOP);
    add(Save, L"BUTTON", L"SAVE TO NEW OUTPUT", WS_TABSTOP);
    add(Status, L"STATIC", L"Select a source and REFRESH HDD.", SS_NOPREFIX);
    add(Progress, PROGRESS_CLASSW, L"");
    add(Log, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE);
    SendMessageW(item(p, Log), EM_SETLIMITTEXT, 1000000, 0);
    add(OpenOutput, L"BUTTON", L"Open Last Output", WS_TABSTOP);
    add(OpenBackup, L"BUTTON", L"Open Backups", WS_TABSTOP);
    add(Cancel, L"BUTTON", L"Cancel", WS_TABSTOP);
    fonts(p);
    layout(p);
    update(p);
    SetOperationProgress(item(p, Progress), false);
    DragAcceptFiles(p.window, TRUE);
    log_line(
        p,
        L"Offline HDD Directory. Double-click folders to expand/collapse; Enter or Open folder navigates. Right-click for file actions. Drop host files/folders onto a folder row, or blank space to import into the current directory.");
}
LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* p = new (std::nothrow) Page;
        if (!p)
            return FALSE;
        p->shared = static_cast<GuiWorkspace*>(create->lpCreateParams);
        p->dpi = p->shared->dpi;
        p->owner = GetParent(w);
        p->window = w;
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
    }
    auto* p = page_state(w);
    if (!p)
        return DefWindowProcW(w, msg, wp, lp);
    try {
        switch (msg) {
        case WM_CREATE:
            controls(*p);
            return 0;
        case WM_SIZE:
            layout(*p);
            return 0;
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == Filter && HIWORD(wp) == EN_CHANGE && !p->populating) {
                populate(*p);
                return 0;
            }
            if (HIWORD(wp) == BN_CLICKED)
                command(*p, id);
            return 0;
        }
        case WM_NOTIFY: {
            auto* n = reinterpret_cast<NMHDR*>(lp);
            if (n->idFrom == Partitions && n->code == TCN_SELCHANGE && !p->busy && !p->shared->state.busy()) {
                int i = static_cast<int>(SendMessageW(item(*p, Partitions), TCM_GETCURSEL, 0, 0));
                if (i >= 0 && i < 5) {
                    p->current = {"CEXYZ" [i], {}};
                    set(*p, Filter, L"");
                    populate(*p);
                }
                return 0;
            }
            if (n->idFrom == Entries && !p->busy && !p->shared->state.busy() && !p->source_changed) {
                if (n->code == NM_RCLICK)
                    context_menu(*p);
                else if (n->code == NM_DBLCLK) {
                    auto s = selected(*p);
                    if (s) {
                        if (hdd::lookup(p->editor.volumes(), *s).directory()) {
                            auto k = ascii_lower(s->str());
                            if (p->expanded.count(k))
                                p->expanded.erase(k);
                            else
                                p->expanded.insert(k);
                            populate(*p);
                        } else
                            command(*p, Properties);
                    }
                } else if (n->code == LVN_KEYDOWN) {
                    auto* k = reinterpret_cast<NMLVKEYDOWN*>(lp);
                    if (k->wVKey == VK_F2)
                        command(*p, Rename);
                    else if (k->wVKey == VK_DELETE)
                        command(*p, Delete);
                    else if (k->wVKey == VK_RETURN)
                        command(*p, OpenFolder);
                    else if (k->wVKey == VK_BACK)
                        command(*p, Up);
                }
            }
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            struct Finish {
                HDROP d;
                ~Finish() { DragFinish(d); }
            } finish{drop};
            if (p->busy || p->shared->state.busy())
                return 0;
            require(p->editor.loaded() && !p->source_changed, "REFRESH HDD before dropping files.");
            POINT point{};
            DragQueryPoint(drop, &point);
            if (lp == 1)
                MapWindowPoints(p->owner, w, &point, 1);
            ClientToScreen(w, &point);
            RECT rect{};
            GetWindowRect(item(*p, Entries), &rect);
            require(PtInRect(&rect, point) != FALSE,
                    "Drop onto the HDD file table, not the source/output path controls.");
            ScreenToClient(item(*p, Entries), &point);
            LVHITTESTINFO hit{};
            hit.pt = point;
            int row = ListView_HitTest(item(*p, Entries), &hit);
            auto dest = p->current;
            if (row >= 0 && static_cast<size_t>(row) < p->rows.size()) {
                auto candidate = p->rows[static_cast<size_t>(row)];
                require(hdd::lookup(p->editor.volumes(), candidate).directory(),
                        "Drop onto a folder row or blank table area, not onto a file.");
                dest = candidate;
            }
            UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
            require(count > 0 && count <= 4096, "Drop 1 to 4096 top-level items at once.");
            std::vector<fs::path> paths;
            for (UINT i = 0; i < count; ++i) {
                UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring value(static_cast<size_t>(len) + 1, L'\0');
                DragQueryFileW(drop, i, value.data(), len + 1);
                value.resize(len);
                paths.emplace_back(value);
            }
            auto text = L"Import " + std::to_wstring(count) + L" item(s) into " + wide(dest.str()) +
                        L"?\r\nThis only changes the pending output; SAVE is required.";
            if (MessageBoxW(w, text.c_str(), L"Confirm drop destination", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) ==
                IDYES)
                job(*p, Import, paths, dest);
            return 0;
        }
        case LogMessage: {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
            log_line(*p, *text);
            return 0;
        }
        case DoneMessage: {
            std::unique_ptr<Completion> done(reinterpret_cast<Completion*>(lp));
            if (p->worker.joinable())
                p->worker.join();
            p->busy = false;
            p->context.reset();
            FinishWorkspaceJob(*p->shared, p->job_id);
            SetOperationProgress(item(*p, Progress), false);
            if (done->success) {
                if (done->action == Write) {
                    p->last = done->saved;
                    p->last_export.clear();
                    p->backup_location = done->saved.backup;
                    p->editor.load({});
                    p->source_changed = true;
                    set(*p, Status,
                        L"Verified NEW output saved. Source selection unchanged. Refresh the original, or select the new output above.");
                } else if (done->action == ExportItems) {
                    p->last_export = done->exported;
                    set(*p, Status, (L"Verified export: " + p->last_export.wstring()));
                } else {
                    p->editor = std::move(done->editor);
                    if (done->action == Load) {
                        p->source_changed = false;
                        p->clipboard.reset();
                        p->expanded.clear();
                        set(*p, Status, L"HDD loaded. Browse C/E/X/Y/Z or queue file changes.");
                    } else
                        set(*p, Status,
                            L"File changes pending. SAVE TO NEW OUTPUT to apply; original source unchanged.");
                }
            } else {
                log_line(*p, wide(done->error));
                set(*p, Status, L"Not completed. Original retained; see exact error/recovery paths below.");
                if (!p->closing)
                    MessageBoxW(w, wide(done->error).c_str(), L"HDD operation not completed", MB_OK | MB_ICONWARNING);
            }
            populate(*p);
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
            FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
            EndPaint(w, &paint);
            return 0;
        }
        case WM_NCDESTROY: {
            if (p->context)
                p->context->cancelled.store(true);
            if (p->worker.joinable())
                p->worker.join();
            DragAcceptFiles(w, FALSE);
            if (p->font)
                DeleteObject(p->font);
            SetWindowLongPtrW(w, GWLP_USERDATA, 0);
            delete p;
            break;
        }
        }
    } catch (const std::exception& e) {
        MessageBoxW(w, wide(e.what()).c_str(), L"HDD Directory", MB_OK | MB_ICONWARNING);
        if (msg == WM_CREATE)
            return -1;
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
}
HWND CreateHddPage(GuiWorkspace& shared) {
    HWND owner = shared.owner;
    WNDCLASSEXW c{};
    c.cbSize = sizeof(c);
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = L"XhcHddDirectoryPage";
    c.lpfnWndProc = proc;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    xhc::require(RegisterClassExW(&c) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                 "Cannot register HDD Directory page.");
    HWND w = CreateWindowExW(WS_EX_CONTROLPARENT, c.lpszClassName, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 100, 100,
                             owner, nullptr, c.hInstance, &shared);
    xhc::require(w != nullptr, "Cannot create HDD Directory tab.");
    return w;
}
bool HddPageBusy(HWND w) {
    auto* p = page_state(w);
    return p && p->busy;
}
bool HddPageCanClose(HWND w) {
    auto* p = page_state(w);
    if (!p)
        return true;
    if (p->busy) {
        if (!p->closing && MessageBoxW(w, L"Cancel the HDD operation and close after safe shutdown?",
                                       L"HDD operation active", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            p->closing = true;
            p->discard_on_close = true;
            if (p->context)
                p->context->cancelled.store(true);
            update(*p);
        }
        return false;
    }
    return p->discard_on_close || discard_prompt(*p);
}
void HddPageDpi(HWND w, unsigned dpi) {
    if (auto* p = page_state(w)) {
        p->dpi = dpi;
        fonts(*p);
        layout(*p);
    }
}

bool HddPageDirty(HWND w) {
    auto* p = page_state(w);
    return p && p->editor.dirty();
}
void HddPageInvalidate(HWND w) {
    if (auto* p = page_state(w)) {
        require(!p->busy, "HDD Directory is busy.");
        p->editor.load({});
        p->source_changed = true;
        p->clipboard.reset();
        p->expanded.clear();
        p->current = {'E', {}};
        populate(*p);
        set(*p, Status, L"Global source changed. REFRESH HDD to load it.");
    }
}
void HddPageOpenOutput(HWND w, bool backups) {
    if (auto* p = page_state(w))
        if (!p->busy && !p->shared->state.busy())
            command(*p, backups ? OpenBackup : OpenOutput);
}

void HddPageKeepOpen(HWND w) {
    if (auto* p = page_state(w))
        if (!p->busy) {
            p->closing = false;
            p->discard_on_close = false;
            update(*p);
        }
}
