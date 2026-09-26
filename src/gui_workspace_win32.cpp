// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#include "gui_workspace_win32.hpp"
#include "gui_theme_win32.hpp"
#include <algorithm>
#include <vector>

namespace {
using namespace xhc;
enum Id { Summary = 5200, Offline, Snapshots, Note, QLabel, QPath, QBrowse, FLabel, FPath, FBrowse, Defaults };
struct Dialog {
    GuiWorkspace* shared = nullptr;
    bool helpers = false, writes = false;
    std::wstring caption, summary, qemu, ffmpeg;
    bool offline = false, exclude = false;
    std::string error;
};
std::wstring read_control(HWND w, int id) {
    HWND c = GetDlgItem(w, id);
    int n = GetWindowTextLengthW(c);
    std::wstring value(static_cast<size_t>(n) + 1, L'\0');
    int got = GetWindowTextW(c, value.data(), n + 1);
    value.resize(static_cast<size_t>(std::max(0, got)));
    return value;
}
fs::path pick_file(HWND owner, const wchar_t* filter, const wchar_t* title, bool output, const wchar_t* ext) {
    std::vector<wchar_t> buf(65536, L'\0');
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = owner;
    o.lpstrFile = buf.data();
    o.nMaxFile = static_cast<DWORD>(buf.size());
    o.lpstrFilter = filter;
    o.lpstrTitle = title;
    o.lpstrDefExt = ext;
    o.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (output ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(output ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) {
        require(CommDlgExtendedError() == 0, "Windows file picker failed.");
        return {};
    }
    fs::path p(buf.data());
    // The save dialog may confirm an existing file; this application never replaces it.
    if (output)
        require(!fs::exists(fs::symlink_status(p)), "Choose a NEW output. Existing files are never overwritten.");
    return p;
}
void initialize(HWND w, Dialog& d) {
    const int dpi = static_cast<int>(d.shared->dpi);
    auto px = [&](int n) { return MulDiv(n, dpi, 96); };
    const int width = px(750), height = px(d.helpers ? 305 : 470);
    RECT owner{};
    GetWindowRect(d.shared->owner, &owner);
    SetWindowPos(w, nullptr,
                 std::max(0, static_cast<int>(owner.left) + (static_cast<int>(owner.right - owner.left) - width) / 2),
                 std::max(0, static_cast<int>(owner.top) + (static_cast<int>(owner.bottom - owner.top) - height) / 2),
                 width, height, SWP_NOZORDER);
    SetWindowTextW(w, d.caption.c_str());
    RECT r{};
    GetClientRect(w, &r);
    const int cw = static_cast<int>(r.right), ch = static_cast<int>(r.bottom);
    auto add = [&](int id, const wchar_t* cls, const std::wstring& text, DWORD style, int x, int y, int wid, int hei,
                   DWORD ex = 0) {
        HWND c = CreateWindowExW(ex, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, px(x), px(y), wid, px(hei), w,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        require(c != nullptr, "Cannot create confirmation/settings control.");
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(d.shared->font), TRUE);
        return c;
    };
    if (d.helpers) {
        add(QLabel, L"STATIC", L"qemu-img executable (shared by all tabs)", SS_NOPREFIX, 16, 12, cw - px(32), 22);
        add(QPath, L"EDIT", d.qemu, ES_AUTOHSCROLL | WS_TABSTOP, 16, 38, cw - px(148), 27, WS_EX_CLIENTEDGE);
        add(QBrowse, L"BUTTON", L"Browse...", WS_TABSTOP, 0, 38, px(98), 27);
        MoveWindow(GetDlgItem(w, QBrowse), cw - px(114), px(38), px(98), px(27), TRUE);
        add(FLabel, L"STATIC", L"FFmpeg executable (new audio in CSB)", SS_NOPREFIX, 16, 79, cw - px(32), 22);
        add(FPath, L"EDIT", d.ffmpeg, ES_AUTOHSCROLL | WS_TABSTOP, 16, 105, cw - px(148), 27, WS_EX_CLIENTEDGE);
        add(FBrowse, L"BUTTON", L"Browse...", WS_TABSTOP, 0, 105, px(98), 27);
        MoveWindow(GetDlgItem(w, FBrowse), cw - px(114), px(105), px(98), px(27), TRUE);
        add(Note, L"STATIC",
            L"Settings apply to all tabs for this session. Explicit helper paths never silently fall back to another executable.",
            SS_NOPREFIX, 16, 148, cw - px(32), 42);
        add(Defaults, L"BUTTON", L"Restore tools-folder defaults", WS_TABSTOP, 16, 205, px(248), 28);
    } else {
        add(Summary, L"EDIT", d.summary, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 16, 12,
            cw - px(32), 194, WS_EX_CLIENTEDGE);
        add(Offline, L"BUTTON", L"XEMU and all other programs using this source are closed.",
            BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, 16, 218, cw - px(32), 44);
        if (d.writes) {
            add(Snapshots, L"BUTTON",
                L"Export current disk files only; exclude any emulator snapshots from the new output.",
                BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, 16, 269, cw - px(32), 48);
            add(Note, L"STATIC",
                L"Optional: without this authorization, a separate prompt appears only if snapshots are detected. The original is retained; snapshot history is not migrated.",
                SS_NOPREFIX, 16, 326, cw - px(32), 54);
        } else
            add(Note, L"STATIC",
                L"This operation does not export emulator snapshot history or modify the original source.", SS_NOPREFIX,
                16, 278, cw - px(32), 54);
    }
    HWND ok = add(IDOK, L"BUTTON", d.helpers ? L"Save" : L"Proceed", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, px(100), 29);
    HWND cancel = add(IDCANCEL, L"BUTTON", L"Cancel", WS_TABSTOP, 0, 0, px(100), 29);
    MoveWindow(ok, cw - px(232), ch - px(45), px(100), px(29), TRUE);
    MoveWindow(cancel, cw - px(116), ch - px(45), px(100), px(29), TRUE);
    EnableWindow(ok, d.helpers ? TRUE : FALSE);
    ApplyGuiTheme(w);
    SetFocus(GetDlgItem(w, d.helpers ? QPath : Offline));
}
INT_PTR CALLBACK dialog_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(w, DWLP_USER));
    try {
        if (msg == WM_INITDIALOG) {
            d = reinterpret_cast<Dialog*>(lp);
            SetWindowLongPtrW(w, DWLP_USER, lp);
            // A new operation never inherits another operation's consent.
            d->offline = false;
            d->exclude = false;
            initialize(w, *d);
            return FALSE;
        }
        if (msg == WM_CLOSE) {
            EndDialog(w, IDCANCEL);
            return TRUE;
        }
        if (msg != WM_COMMAND || !d)
            return FALSE;
        const int id = LOWORD(wp);
        if (id == IDCANCEL) {
            EndDialog(w, IDCANCEL);
            return TRUE;
        }
        if (id == Offline && !d->helpers) {
            EnableWindow(GetDlgItem(w, IDOK), SendMessageW(GetDlgItem(w, Offline), BM_GETCHECK, 0, 0) == BST_CHECKED);
            return TRUE;
        }
        if (d->helpers && (id == QBrowse || id == FBrowse)) {
            auto p = pick_file(w, L"Windows executables\0*.exe\0All files\0*.*\0",
                               id == QBrowse ? L"Select qemu-img.exe" : L"Select ffmpeg.exe", false, L"exe");
            if (!p.empty())
                SetWindowTextW(GetDlgItem(w, id == QBrowse ? QPath : FPath), p.wstring().c_str());
            return TRUE;
        }
        if (d->helpers && id == Defaults) {
            auto base = executable_directory() / L"tools";
            SetWindowTextW(GetDlgItem(w, QPath), (base / L"qemu-img.exe").wstring().c_str());
            SetWindowTextW(GetDlgItem(w, FPath), (base / L"ffmpeg.exe").wstring().c_str());
            return TRUE;
        }
        if (id == IDOK) {
            if (d->helpers) {
                d->qemu = read_control(w, QPath);
                d->ffmpeg = read_control(w, FPath);
            } else {
                d->offline = SendMessageW(GetDlgItem(w, Offline), BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (!d->offline)
                    return TRUE; // Also rejects Enter/synthetic commands while disabled.
                d->exclude = d->writes && SendMessageW(GetDlgItem(w, Snapshots), BM_GETCHECK, 0, 0) == BST_CHECKED;
            }
            EndDialog(w, IDOK);
            return TRUE;
        }
    } catch (const std::exception& e) {
        if (d)
            d->error = e.what();
        EndDialog(w, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
bool show(Dialog& d) {
    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog;
        WORD menu = 0, window_class = 0, title = 0;
    } t{};
    t.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    t.dialog.cx = 450;
    t.dialog.cy = 260;
    auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &t.dialog, d.shared->owner, dialog_proc,
                                          reinterpret_cast<LPARAM>(&d));
    require(d.error.empty(), d.error);
    require(result != -1, "Cannot open confirmation/options dialog.");
    return result == IDOK;
}
}
xhc::fs::path PickWorkspaceFile(HWND owner, xhc::ui::Format format, bool output) {
    const bool qcow = format == xhc::ui::Format::Qcow2;
    return pick_file(owner,
                     qcow ? L"QCOW2 disk image\0*.qcow2;*.qcow\0All files\0*.*\0"
                          : L"RAW disk image\0*.raw;*.img;*.bin\0All files\0*.*\0",
                     output ? L"Choose a NEW output file (source is never overwritten)" : L"Select HDD source", output,
                     qcow ? L"qcow2" : L"raw");
}
xhc::fs::path PickWorkspaceFolder(HWND owner, bool output) {
    IFileOpenDialog* d = nullptr;
    xhc::require(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                            reinterpret_cast<void**>(&d))),
                 "Cannot create folder picker.");
    struct Release {
        IFileOpenDialog* d;
        ~Release() { d->Release(); }
    } release{d};
    FILEOPENDIALOGOPTIONS flags{};
    HRESULT hr = d->GetOptions(&flags);
    if (SUCCEEDED(hr))
        hr = d->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    if (SUCCEEDED(hr))
        hr = d->SetTitle(output ? L"Choose parent for a NEW Folder-HDD directory"
                                : L"Choose synchronized Folder-HDD source");
    if (SUCCEEDED(hr))
        hr = d->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    xhc::require(SUCCEEDED(hr), "Folder picker failed.");
    IShellItem* selected = nullptr;
    xhc::require(SUCCEEDED(d->GetResult(&selected)), "Cannot read selected folder.");
    PWSTR raw = nullptr;
    hr = selected->GetDisplayName(SIGDN_FILESYSPATH, &raw);
    selected->Release();
    xhc::require(SUCCEEDED(hr), "Selected folder is not a filesystem path.");
    xhc::fs::path p(raw);
    CoTaskMemFree(raw);
    if (output) {
        auto candidate = p / L"XEMU-HDD-New";
        for (unsigned n = 2; xhc::fs::exists(candidate); ++n)
            candidate = p / (L"XEMU-HDD-New-" + std::to_wstring(n));
        return candidate;
    }
    return p;
}
bool ConfirmWorkspaceOperation(GuiWorkspace& s, xhc::Options& o, const std::wstring& op, bool writes,
                               const std::wstring& detail) {
    xhc::require(!s.state.busy(), "Another tab is using the HDD workspace.");
    xhc::require(!o.source.empty(), "Choose the global Source first.");
    if (writes)
        xhc::require(!o.destination.empty(), "Choose a NEW global Output first.");
    Dialog d;
    d.shared = &s;
    d.writes = writes;
    d.caption = L"Confirm - " + op;
    d.summary = op + L"\r\n\r\nSource:\r\n" + o.source.wstring();
    if (writes)
        d.summary += L"\r\n\r\nNEW output:\r\n" + o.destination.wstring();
    d.summary += L"\r\n\r\n" + detail;
    if (writes)
        d.summary += L"\r\nThe original is never overwritten. Output contains live files, not deleted remnants.";
    o.offline_confirmed = false;
    o.acknowledge_snapshot_exclusion = false;
    if (!show(d))
        return false;
    o.offline_confirmed = d.offline;
    o.acknowledge_snapshot_exclusion = d.exclude;
    return d.offline;
}
bool EditWorkspaceHelpers(GuiWorkspace& s) {
    if (s.state.busy())
        return false;
    Dialog d;
    d.shared = &s;
    d.helpers = true;
    d.caption = L"Global Options - Helper Locations";
    d.qemu = s.state.settings().qemu_img.wstring();
    d.ffmpeg = s.state.settings().ffmpeg.wstring();
    if (!show(d))
        return false;
    return s.state.helpers(xhc::fs::path(d.qemu), xhc::fs::path(d.ffmpeg));
}
xhc::ui::Ticket BeginWorkspaceJob(GuiWorkspace& s) {
    auto ticket = s.state.begin();
    SendMessageW(s.owner, WorkspaceChangedMessage, 0, 0);
    return ticket;
}
void FinishWorkspaceJob(GuiWorkspace& s, uint64_t job) {
    if (s.state.finish(job))
        SendMessageW(s.owner, WorkspaceChangedMessage, 0, 0);
}
void ConnectSnapshotQuestion(GuiWorkspace& s, xhc::Options& o, const std::shared_ptr<xhc::Context>& ctx, uint64_t job) {
    const HWND owner = s.owner;
    const auto source = o.source.wstring();
    o.confirm_snapshot_exclusion = [owner, source, ctx, job](uint64_t count) {
        ctx->check();
        SnapshotQuestion q{job, count, source, ctx.get()};
        const bool answer = SendMessageW(owner, SnapshotQuestionMessage, 0, reinterpret_cast<LPARAM>(&q)) != 0;
        ctx->check();
        return answer;
    };
}
bool AnswerSnapshotQuestion(GuiWorkspace& s, const SnapshotQuestion& q) {
    if (!q.context || q.context->cancelled.load() || !q.count || s.state.active_job() != q.job)
        return false;
    auto text =
        L"Detected " + std::to_wstring(q.count) + L" emulator snapshot(s) in:\r\n" + q.source +
        L"\r\n\r\nThis program does not migrate snapshot history. Export current disk files WITHOUT those snapshots?\r\nThe original, including its snapshots, remains untouched.\r\n\r\nNo cancels this operation; it does not discard your pending edits.";
    bool yes = MessageBoxW(s.owner, text.c_str(), L"Snapshots detected - authorize exclusion",
                           MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
    return yes && !q.context->cancelled.load() && s.state.active_job() == q.job;
}
