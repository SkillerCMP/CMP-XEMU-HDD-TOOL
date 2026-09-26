// SPDX-License-Identifier: GPL-2.0-or-later
// Global workspace shell. All disk work stays on cancellable workers.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include "xhc/converter.hpp"
#include "gui_workspace_win32.hpp"
#include "gui_csb_win32.hpp"
#include "gui_hdd_win32.hpp"
#include "gui_progress_win32.hpp"
#include "gui_theme_win32.hpp"
#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

namespace {
constexpr UINT kLogMessage = WM_APP + 1, kDoneMessage = WM_APP + 2;
constexpr int kAppIconResource = 101;
constexpr int kTabs = 800;
enum Control : int {
    SourceLabel = 100,
    SourceQcow,
    SourceFolder,
    SourceRaw,
    SourceEdit,
    SourceBrowse,
    OutputLabel,
    OutputQcow,
    OutputFolder,
    OutputRaw,
    OutputEdit,
    OutputBrowse,
    ModeLabel,
    ConvertRadio,
    VerifyRadio,
    ScopeLabel,
    StatusLabel,
    Progress,
    LogEdit,
    Start,
    Cancel,
    OpenResult
};
enum Menu : int {
    BrowseSourceMenu = 6000,
    BrowseOutputMenu,
    AddAudioMenu,
    ClearCsbMenu,
    OpenLastMenu,
    OpenBackupMenu,
    ExitMenu,
    HelpersMenu,
    ResetPanesMenu,
    ThemeDefaultMenu,
    ThemeDarkMenu,
    ThemeXboxMenu
};
struct Completion {
    bool success = false;
    std::wstring message;
    xhc::Result result;
};
struct App {
    HWND window = nullptr, tabs = nullptr, csb_page = nullptr, hdd_page = nullptr;
    HMENU menu = nullptr, theme_menu = nullptr;
    int active_tab = 0;
    HFONT font = nullptr;
    unsigned dpi = 96;
    bool busy = false, close_pending = false, verify = false, populating = false;
    uint64_t job = 0;
    GuiWorkspace shared;
    std::shared_ptr<xhc::Context> context;
    std::thread worker;
    xhc::Result last;
};
std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0)
        return L"Unable to display a diagnostic message.";
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
HWND control(App& app, int id) { return GetDlgItem(app.window, id); }
std::wstring text(App& app, int id) {
    HWND window = control(app, id);
    int length = GetWindowTextLengthW(window);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    int got = GetWindowTextW(window, value.data(), length + 1);
    value.resize(got > 0 ? static_cast<size_t>(got) : 0);
    return value;
}
void set_text(App& app, int id, const std::wstring& value) { SetWindowTextW(control(app, id), value.c_str()); }
void append_log(App& app, const std::wstring& line) {
    HWND log = control(app, LogEdit);
    // Keep UI storage bounded; full conversion verification is in the JSON report.
    if (GetWindowTextLengthW(log) > 900000)
        SetWindowTextW(log, L"[Earlier display messages discarded; output verification report is retained.]\r\n");
    SendMessageW(log, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    std::wstring entry = line;
    size_t pos = 0;
    while ((pos = entry.find(L'\n', pos)) != std::wstring::npos) {
        if (pos == 0 || entry[pos - 1] != L'\r') {
            entry.insert(pos, 1, L'\r');
            ++pos;
        }
        ++pos;
    }
    entry += L"\r\n";
    SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(entry.c_str()));
    SendMessageW(log, EM_SCROLLCARET, 0, 0);
}
void post_log(HWND window, const std::string& line) {
    auto message = std::make_unique<std::wstring>(wide(line));
    if (PostMessageW(window, kLogMessage, 0, reinterpret_cast<LPARAM>(message.get())))
        message.release();
}
void update_enabled(App& app) {
    const bool idle = !app.shared.state.busy();
    for (int id = SourceLabel; id <= SourceBrowse; ++id)
        EnableWindow(control(app, id), idle);
    const bool output = xhc::ui::output_enabled(!idle, static_cast<unsigned>(app.active_tab), app.verify);
    for (int id = OutputLabel; id <= OutputBrowse; ++id)
        EnableWindow(control(app, id), output);
    for (int id : {ConvertRadio, VerifyRadio, Start})
        EnableWindow(control(app, id), idle);
    EnableWindow(control(app, Cancel), app.busy && !app.close_pending);
    EnableWindow(control(app, OpenResult), idle && !app.last.destination.empty());
    EnableWindow(app.tabs, idle);
    set_text(app, Start, app.verify ? L"Verify source" : L"Convert");
    SendMessageW(control(app, ConvertRadio), BM_SETCHECK, app.verify ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessageW(control(app, VerifyRadio), BM_SETCHECK, app.verify ? BST_CHECKED : BST_UNCHECKED, 0);
    set_text(
        app, ScopeLabel,
        app.verify
            ? L"Verify checks the selected source without creating an output. The global Output is retained for later use."
            : L"Convert uses the global Source and Output formats. Every output is a verified clean live-file rebuild; the original is never overwritten.\r\nLive recovery/cache files and required system metadata remain preserved.");
    if (app.menu) {
        for (int id : {BrowseSourceMenu, BrowseOutputMenu, HelpersMenu, AddAudioMenu, ClearCsbMenu, OpenLastMenu,
                       OpenBackupMenu, ResetPanesMenu}) {
            bool enabled = idle;
            if (id == BrowseOutputMenu)
                enabled = output;
            if (id == AddAudioMenu || id == ClearCsbMenu)
                enabled = idle && app.active_tab == 1;
            if (id == OpenBackupMenu)
                enabled = idle && app.active_tab != 0;
            EnableMenuItem(app.menu, static_cast<UINT>(id), MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
        }
        if (app.theme_menu)
            CheckMenuRadioItem(app.theme_menu, ThemeDefaultMenu, ThemeXboxMenu,
                               ThemeDefaultMenu + static_cast<UINT>(CurrentGuiTheme()), MF_BYCOMMAND);
        DrawMenuBar(app.window);
    }
}
void source_fields(App& app) {
    const auto& s = app.shared.state.settings();
    app.populating = true;
    set_text(app, SourceEdit, s.source.wstring());
    for (auto pair : {std::pair<int, xhc::ui::Format>{SourceQcow, xhc::ui::Format::Qcow2},
                      {SourceFolder, xhc::ui::Format::Folder},
                      {SourceRaw, xhc::ui::Format::Raw}})
        SendMessageW(control(app, pair.first), BM_SETCHECK,
                     s.source_format == pair.second ? BST_CHECKED : BST_UNCHECKED, 0);
    app.populating = false;
}
void output_fields(App& app) {
    const auto& s = app.shared.state.settings();
    app.populating = true;
    set_text(app, OutputEdit, s.output.wstring());
    for (auto pair : {std::pair<int, xhc::ui::Format>{OutputQcow, xhc::ui::Format::Qcow2},
                      {OutputFolder, xhc::ui::Format::Folder},
                      {OutputRaw, xhc::ui::Format::Raw}})
        SendMessageW(control(app, pair.first), BM_SETCHECK,
                     s.output_format == pair.second ? BST_CHECKED : BST_UNCHECKED, 0);
    app.populating = false;
}
bool change_source(App& app, const xhc::fs::path& path, xhc::ui::Format format) {
    const bool csb_dirty = CsbPageDirty(app.csb_page), hdd_dirty = HddPageDirty(app.hdd_page);
    auto result = app.shared.state.source(path, format, csb_dirty || hdd_dirty, false);
    if (result == xhc::ui::SourceChange::NeedsDiscard) {
        std::wstring names = csb_dirty ? L"Custom Soundtrack Builder" : L"";
        if (hdd_dirty)
            names += (names.empty() ? L"" : L" and ") + std::wstring(L"HDD Directory");
        const auto message =
            L"There are pending edits in " + names +
            L".\r\n\r\nDiscard those changes and select another source?\r\nChoose No to Keep Editing. The source is unchanged.";
        if (MessageBoxW(app.window, message.c_str(), L"Change global source - pending edits",
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            source_fields(app);
            return false;
        }
        result = app.shared.state.source(path, format, true, true);
    }
    if (result == xhc::ui::SourceChange::Busy) {
        source_fields(app);
        return false;
    }
    if (result == xhc::ui::SourceChange::Applied) {
        CsbPageInvalidate(app.csb_page);
        HddPageInvalidate(app.hdd_page);
    }
    return true;
}
void browse_source(App& app) {
    if (app.shared.state.busy())
        return;
    const auto kind = app.shared.state.settings().source_format;
    auto path = kind == xhc::ui::Format::Folder ? PickWorkspaceFolder(app.window, false)
                                                : PickWorkspaceFile(app.window, kind, false);
    if (!path.empty() && change_source(app, path, kind))
        source_fields(app);
}
void browse_output(App& app) {
    if (!xhc::ui::output_enabled(app.shared.state.busy(), static_cast<unsigned>(app.active_tab), app.verify))
        return;
    const auto kind = app.shared.state.settings().output_format;
    auto path = kind == xhc::ui::Format::Folder ? PickWorkspaceFolder(app.window, true)
                                                : PickWorkspaceFile(app.window, kind, true);
    if (!path.empty() && app.shared.state.output(path, kind))
        output_fields(app);
}
void layout(App& app) {
    RECT r{};
    GetClientRect(app.window, &r);
    const int width = MulDiv(static_cast<int>(r.right), 96, static_cast<int>(app.dpi));
    const int height = MulDiv(static_cast<int>(r.bottom), 96, static_cast<int>(app.dpi));
    auto place = [&](int id, int x, int y, int w, int h) {
        MoveWindow(control(app, id), MulDiv(x, app.dpi, 96), MulDiv(y, app.dpi, 96),
                   MulDiv(std::max(1, w), app.dpi, 96), MulDiv(std::max(1, h), app.dpi, 96), TRUE);
    };
    place(SourceLabel, 12, 12, 90, 22);
    place(SourceQcow, 115, 9, 94, 24);
    place(SourceFolder, 220, 9, 116, 24);
    place(SourceRaw, 345, 9, 82, 24);
    place(SourceEdit, 12, 39, width - 143, 27);
    place(SourceBrowse, width - 119, 39, 107, 27);
    place(OutputLabel, 12, 80, 90, 22);
    place(OutputQcow, 115, 77, 94, 24);
    place(OutputFolder, 220, 77, 116, 24);
    place(OutputRaw, 345, 77, 82, 24);
    place(OutputEdit, 12, 107, width - 143, 27);
    place(OutputBrowse, width - 119, 107, 107, 27);
    const int tab_y = 150, page_y = 197;
    if (app.tabs)
        MoveWindow(app.tabs, MulDiv(12, app.dpi, 96), MulDiv(tab_y, app.dpi, 96), MulDiv(width - 24, app.dpi, 96),
                   MulDiv(37, app.dpi, 96), TRUE);
    const int offset = MulDiv(page_y, app.dpi, 96), ph = std::max(1, static_cast<int>(r.bottom) - offset);
    if (app.csb_page) {
        MoveWindow(app.csb_page, 0, offset, static_cast<int>(r.right), ph, TRUE);
        ShowWindow(app.csb_page, app.active_tab == 1 ? SW_SHOW : SW_HIDE);
    }
    if (app.hdd_page) {
        MoveWindow(app.hdd_page, 0, offset, static_cast<int>(r.right), ph, TRUE);
        ShowWindow(app.hdd_page, app.active_tab == 2 ? SW_SHOW : SW_HIDE);
    }
    for (int id = ModeLabel; id <= OpenResult; ++id)
        ShowWindow(control(app, id), app.active_tab == 0 ? SW_SHOW : SW_HIDE);
    if (app.active_tab != 0)
        return;
    place(ModeLabel, 18, page_y + 18, 89, 23);
    place(ConvertRadio, 118, page_y + 15, 110, 26);
    place(VerifyRadio, 240, page_y + 15, 110, 26);
    place(ScopeLabel, 18, page_y + 57, width - 36, 63);
    place(StatusLabel, 18, page_y + 137, width - 36, 24);
    place(Progress, 18, page_y + 168, width - 36, 14);
    place(LogEdit, 18, page_y + 200, width - 36, height - page_y - 260);
    place(OpenResult, 18, height - 43, 160, 28);
    place(Start, width - 276, height - 43, 144, 28);
    place(Cancel, width - 116, height - 43, 98, 28);
}
void set_font(App& app) {
    HFONT old = app.font;
    app.font = CreateFontW(-MulDiv(10, static_cast<int>(app.dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                           L"Segoe UI");
    app.shared.font = app.font;
    app.shared.dpi = app.dpi;
    for (int id = SourceLabel; id <= OpenResult; ++id)
        SendMessageW(control(app, id), WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    if (app.tabs) {
        SendMessageW(app.tabs, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
        SendMessageW(app.tabs, TCM_SETITEMSIZE, 0,
                     MAKELPARAM(MulDiv(220, static_cast<int>(app.dpi), 96), MulDiv(31, static_cast<int>(app.dpi), 96)));
    }
    if (app.csb_page)
        CsbPageDpi(app.csb_page, app.dpi);
    if (app.hdd_page)
        HddPageDpi(app.hdd_page, app.dpi);
    if (old)
        DeleteObject(old);
}
void create_controls(App& app) {
    app.shared.owner = app.window;
    app.shared.dpi = app.dpi;
    const auto tools = xhc::executable_directory() / L"tools";
    app.shared.state.helpers(tools / L"qemu-img.exe", tools / L"ffmpeg.exe");
    auto add = [&](int id, const wchar_t* cls, const wchar_t* name, DWORD style, DWORD ex = 0) {
        xhc::require(CreateWindowExW(ex, cls, name, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, app.window,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
                                     nullptr) != nullptr,
                     "Cannot create workspace control.");
    };
    add(SourceLabel, L"STATIC", L"Source", SS_NOPREFIX);
    add(SourceQcow, L"BUTTON", L"QCOW2", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP);
    add(SourceFolder, L"BUTTON", L"Folder-HDD", BS_AUTORADIOBUTTON);
    add(SourceRaw, L"BUTTON", L"RAW", BS_AUTORADIOBUTTON);
    add(SourceEdit, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP | WS_GROUP, WS_EX_CLIENTEDGE);
    add(SourceBrowse, L"BUTTON", L"Browse...", WS_TABSTOP);
    add(OutputLabel, L"STATIC", L"New output", SS_NOPREFIX | WS_GROUP);
    add(OutputQcow, L"BUTTON", L"QCOW2", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP);
    add(OutputFolder, L"BUTTON", L"Folder-HDD", BS_AUTORADIOBUTTON);
    add(OutputRaw, L"BUTTON", L"RAW", BS_AUTORADIOBUTTON);
    add(OutputEdit, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP | WS_GROUP, WS_EX_CLIENTEDGE);
    add(OutputBrowse, L"BUTTON", L"Browse...", WS_TABSTOP);
    app.tabs =
        CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | TCS_FIXEDWIDTH, 0, 0,
                        10, 10, app.window, reinterpret_cast<HMENU>(kTabs), GetModuleHandleW(nullptr), nullptr);
    xhc::require(app.tabs != nullptr, "Cannot create tab selector.");
    TCITEMW tab{};
    tab.mask = TCIF_TEXT;
    int index = 0;
    for (const wchar_t* name : {L"HDD Converter", L"Custom Soundtrack Builder", L"HDD Directory"}) {
        tab.pszText = const_cast<wchar_t*>(name);
        SendMessageW(app.tabs, TCM_INSERTITEMW, static_cast<WPARAM>(index++), reinterpret_cast<LPARAM>(&tab));
    }
    add(ModeLabel, L"STATIC", L"Operation", SS_NOPREFIX | WS_GROUP);
    add(ConvertRadio, L"BUTTON", L"Convert", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP);
    add(VerifyRadio, L"BUTTON", L"Verify", BS_AUTORADIOBUTTON);
    add(ScopeLabel, L"STATIC", L"", SS_NOPREFIX | WS_GROUP);
    add(StatusLabel, L"STATIC", L"Ready. Select the global source and new output.", SS_NOPREFIX);
    add(Progress, PROGRESS_CLASSW, L"", 0);
    add(LogEdit, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE);
    SendMessageW(control(app, LogEdit), EM_SETLIMITTEXT, 1000000, 0);
    add(Start, L"BUTTON", L"Convert", BS_PUSHBUTTON | WS_TABSTOP);
    add(Cancel, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP);
    add(OpenResult, L"BUTTON", L"Open output location", BS_PUSHBUTTON | WS_TABSTOP);
    app.csb_page = CreateCsbPage(app.shared);
    app.hdd_page = CreateHddPage(app.shared);
    app.menu = CreateMenu();
    HMENU file = CreatePopupMenu(), options = CreatePopupMenu();
    xhc::require(app.menu && file && options, "Cannot create global menu bar.");
    AppendMenuW(file, MF_STRING, BrowseSourceMenu, L"Browse source...");
    AppendMenuW(file, MF_STRING, BrowseOutputMenu, L"Choose new output...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, AddAudioMenu, L"CSB: Add audio files...");
    AppendMenuW(file, MF_STRING, ClearCsbMenu, L"CSB: Clear editor");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, OpenLastMenu, L"Open last output (active tab)");
    AppendMenuW(file, MF_STRING, OpenBackupMenu, L"Open backups (active tab)");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ExitMenu, L"Exit");
    AppendMenuW(options, MF_STRING, HelpersMenu, L"Helper locations...");
    AppendMenuW(options, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(options, MF_STRING, ResetPanesMenu, L"CSB: Reset pane widths");
    app.theme_menu = CreatePopupMenu();
    xhc::require(app.theme_menu != nullptr, "Cannot create theme selector.");
    AppendMenuW(app.theme_menu, MF_STRING, ThemeDefaultMenu, L"&Default (White)");
    AppendMenuW(app.theme_menu, MF_STRING, ThemeDarkMenu, L"D&ark Mode");
    AppendMenuW(app.theme_menu, MF_STRING, ThemeXboxMenu, L"&Xbox Mode");
    AppendMenuW(options, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(options, MF_POPUP, reinterpret_cast<UINT_PTR>(app.theme_menu), L"&Theme");
    AppendMenuW(app.menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    AppendMenuW(app.menu, MF_POPUP, reinterpret_cast<UINT_PTR>(options), L"&Options");
    SetMenu(app.window, app.menu);
    set_font(app);
    source_fields(app);
    output_fields(app);
    update_enabled(app);
    layout(app);
    SetOperationProgress(control(app, Progress), false);
    append_log(
        app,
        L"Global Source / New output apply to all three tabs. Helper locations are in Options.\r\nEvery operation requires a fresh offline confirmation; source files are never overwritten.");
    ApplyGuiTheme(app.window);
    DragAcceptFiles(app.window, TRUE);
}
void begin(App& app) {
    xhc::require(!app.shared.state.busy(), "Another operation is active.");
    auto options = xhc::ui::disk_options(app.shared.state.settings(), app.verify);
    if (!ConfirmWorkspaceOperation(
            app.shared, options, app.verify ? L"Verify source" : L"Convert and verify new output", !app.verify,
            app.verify ? L"Read-only filesystem verification. No output will be created."
                       : L"Clean live-file rebuild. Existing files, caches and recovery folders are preserved."))
        return;
    if (app.worker.joinable())
        app.worker.join();
    auto context = std::make_shared<xhc::Context>();
    const HWND window = app.window;
    context->log = [window](const std::string& line) { post_log(window, line); };
    auto ticket = BeginWorkspaceJob(app.shared);
    app.job = ticket.id;
    ConnectSnapshotQuestion(app.shared, options, context, app.job);
    app.context = context;
    app.busy = true;
    app.close_pending = false;
    app.last = {};
    set_text(app, LogEdit, L"");
    set_text(app, StatusLabel, L"Validating source...");
    update_enabled(app);
    SetOperationProgress(control(app, Progress), true);
    try {
        app.worker = std::thread([window, options, context] {
            auto done = std::make_unique<Completion>();
            try {
                done->result = xhc::convert(options, *context);
                done->success = true;
                done->message =
                    options.mode == xhc::Mode::Analyze
                        ? L"Source verification completed. No output created."
                        : L"Conversion and verification completed. Original and global source selection retained.";
            } catch (const std::exception& e) {
                done->message = wide(e.what());
            } catch (...) {
                done->message = L"Unexpected failure. Original retained; inspect reported recovery paths.";
            }
            if (PostMessageW(window, kDoneMessage, 0, reinterpret_cast<LPARAM>(done.get())))
                done.release();
        });
    } catch (...) {
        app.busy = false;
        app.context.reset();
        FinishWorkspaceJob(app.shared, app.job);
        SetOperationProgress(control(app, Progress), false);
        update_enabled(app);
        throw;
    }
}
void cancel(App& app) {
    if (!app.busy || !app.context)
        return;
    app.context->cancelled.store(true);
    set_text(app, StatusLabel, L"Cancelling safely; waiting for the disk/helper operation...");
    EnableWindow(control(app, Cancel), FALSE);
}
void menu_open(App& app, bool backups) {
    if (app.shared.state.busy())
        return;
    if (app.active_tab == 1)
        CsbPageAction(app.csb_page, backups ? CsbUiAction::OpenBackups : CsbUiAction::OpenOutput);
    else if (app.active_tab == 2)
        HddPageOpenOutput(app.hdd_page, backups);
    else if (!backups && !app.last.destination.empty())
        ShellExecuteW(app.window, L"open", app.last.destination.parent_path().wstring().c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCCREATE) {
        auto* app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!app)
        return DefWindowProcW(window, message, wp, lp);
    try {
        switch (message) {
        case WM_CREATE:
            create_controls(*app);
            return 0;
        case WM_SIZE:
            layout(*app);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* l = reinterpret_cast<MINMAXINFO*>(lp);
            l->ptMinTrackSize.x = MulDiv(1120, app->dpi, 96);
            l->ptMinTrackSize.y = MulDiv(795, app->dpi, 96);
            return 0;
        }
        case WM_DPICHANGED: {
            app->dpi = HIWORD(wp);
            set_font(*app);
            const auto* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout(*app);
            return 0;
        }
        case WorkspaceChangedMessage:
            update_enabled(*app);
            return 0;
        case SnapshotQuestionMessage:
            return app->close_pending ? FALSE
                                      : AnswerSnapshotQuestion(app->shared, *reinterpret_cast<SnapshotQuestion*>(lp));
        case WM_NOTIFY: {
            auto* h = reinterpret_cast<NMHDR*>(lp);
            if (h->idFrom == kTabs && h->code == TCN_SELCHANGING)
                return app->shared.state.busy() ? TRUE : FALSE;
            if (h->idFrom == kTabs && h->code == TCN_SELCHANGE) {
                app->active_tab = static_cast<int>(SendMessageW(app->tabs, TCM_GETCURSEL, 0, 0));
                layout(*app);
                update_enabled(*app);
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == IDCANCEL && app->active_tab == 1) {
                SendMessageW(app->csb_page, WM_KEYDOWN, VK_ESCAPE, 0);
                return 0;
            }
            if (app->populating)
                return 0;
            if (id == Cancel) {
                cancel(*app);
                return 0;
            }
            if (id == ExitMenu) {
                SendMessageW(window, WM_CLOSE, 0, 0);
                return 0;
            }
            if (id >= ThemeDefaultMenu && id <= ThemeXboxMenu) {
                const auto mode = static_cast<xhc::ui::Theme>(id - ThemeDefaultMenu);
                const bool saved = SelectGuiTheme(window, mode);
                update_enabled(*app);
                if (!saved)
                    MessageBoxW(window,
                                L"The theme was applied for this session, but Windows could not save the preference.",
                                L"Theme preference", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            if (app->shared.state.busy())
                return 0;
            if (id == SourceEdit && HIWORD(wp) == EN_CHANGE) {
                change_source(*app, xhc::fs::path(text(*app, SourceEdit)), app->shared.state.settings().source_format);
                return 0;
            }
            if (id == OutputEdit && HIWORD(wp) == EN_CHANGE) {
                if (xhc::ui::output_enabled(false, static_cast<unsigned>(app->active_tab), app->verify))
                    app->shared.state.output(xhc::fs::path(text(*app, OutputEdit)),
                                             app->shared.state.settings().output_format);
                return 0;
            }
            if (id == SourceQcow || id == SourceFolder || id == SourceRaw) {
                auto kind = id == SourceQcow     ? xhc::ui::Format::Qcow2
                            : id == SourceFolder ? xhc::ui::Format::Folder
                                                 : xhc::ui::Format::Raw;
                auto path = app->shared.state.settings().source;
                if ((kind == xhc::ui::Format::Folder) !=
                    (app->shared.state.settings().source_format == xhc::ui::Format::Folder))
                    path.clear();
                change_source(*app, path, kind);
                source_fields(*app);
            } else if (id == OutputQcow || id == OutputFolder || id == OutputRaw) {
                if (!xhc::ui::output_enabled(false, static_cast<unsigned>(app->active_tab), app->verify))
                    return 0;
                auto kind = id == OutputQcow     ? xhc::ui::Format::Qcow2
                            : id == OutputFolder ? xhc::ui::Format::Folder
                                                 : xhc::ui::Format::Raw;
                auto old = app->shared.state.settings();
                auto path = old.output;
                if (kind != old.output_format)
                    path.clear();
                app->shared.state.output(path, kind);
                output_fields(*app);
            } else if (id == SourceBrowse || id == BrowseSourceMenu)
                browse_source(*app);
            else if (id == OutputBrowse || id == BrowseOutputMenu)
                browse_output(*app);
            else if (id == ConvertRadio || id == VerifyRadio) {
                app->verify = id == VerifyRadio;
                update_enabled(*app);
            } else if (id == HelpersMenu)
                EditWorkspaceHelpers(app->shared);
            else if (id == ResetPanesMenu)
                CsbPageAction(app->csb_page, CsbUiAction::ResetPanes);
            else if (id == AddAudioMenu && app->active_tab == 1)
                CsbPageAction(app->csb_page, CsbUiAction::AddFiles);
            else if (id == ClearCsbMenu && app->active_tab == 1)
                CsbPageAction(app->csb_page, CsbUiAction::Clear);
            else if (id == OpenLastMenu || id == OpenResult)
                menu_open(*app, false);
            else if (id == OpenBackupMenu)
                menu_open(*app, true);
            else if (id == Start && app->active_tab == 0)
                begin(*app);
            return 0;
        }
        case WM_DROPFILES: {
            if (app->shared.state.busy()) {
                DragFinish(reinterpret_cast<HDROP>(wp));
                return 0;
            }
            if (app->active_tab == 1)
                return SendMessageW(app->csb_page, WM_DROPFILES, wp, 1);
            if (app->active_tab == 2)
                return SendMessageW(app->hdd_page, WM_DROPFILES, wp, 1);
            HDROP drop = reinterpret_cast<HDROP>(wp);
            struct Finish {
                HDROP d;
                ~Finish() { DragFinish(d); }
            } finish{drop};
            // Source selection is explicit and type-aware; converter-page drops never guess a format.
            MessageBoxW(
                window,
                L"Select the source format and use the global Browse button. File imports belong on the CSB or HDD Directory table.",
                L"Choose a source", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case kLogMessage: {
            std::unique_ptr<std::wstring> line(reinterpret_cast<std::wstring*>(lp));
            append_log(*app, *line);
            if (!app->close_pending)
                set_text(*app, StatusLabel, *line);
            return 0;
        }
        case kDoneMessage: {
            std::unique_ptr<Completion> done(reinterpret_cast<Completion*>(lp));
            if (app->worker.joinable())
                app->worker.join();
            app->busy = false;
            app->context.reset();
            app->last = done->result;
            FinishWorkspaceJob(app->shared, app->job);
            SetOperationProgress(control(*app, Progress), false);
            append_log(*app, done->message);
            set_text(*app, StatusLabel,
                     done->success ? done->message
                                   : L"Not completed. Original retained; see error/recovery paths below.");
            update_enabled(*app);
            if (app->close_pending) {
                DestroyWindow(window);
                return 0;
            }
            if (!done->success)
                MessageBoxW(window, done->message.c_str(), L"Operation not completed", MB_OK | MB_ICONWARNING);
            return 0;
        }
        case WM_CLOSE:
            if (!CsbPageCanClose(app->csb_page) || !HddPageCanClose(app->hdd_page)) {
                // A different dirty tab may veto closing after a worker was cancelled.
                // Do not leave an earlier close/discard permission armed for later edits.
                if (!app->shared.state.busy()) {
                    CsbPageKeepOpen(app->csb_page);
                    HddPageKeepOpen(app->hdd_page);
                }
                return 0;
            }
            if (app->busy) {
                if (MessageBoxW(
                        window,
                        L"Cancel and close after safe shutdown? Original and uncertain recovery files are retained.",
                        L"Operation active", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                    app->close_pending = true;
                    cancel(*app);
                }
            } else
                DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            DragAcceptFiles(window, FALSE);
            if (app->font) {
                DeleteObject(app->font);
                app->font = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception& e) {
        MessageBoxW(window, wide(e.what()).c_str(), L"Xemu HDD Tools", MB_OK | MB_ICONWARNING);
        if (message == WM_CREATE)
            return -1;
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com)) {
        MessageBoxW(nullptr, L"Cannot initialize Windows dialogs.", L"Xemu HDD Tools", MB_OK | MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&common);
    InitializeGuiTheme();
    App app;
    HDC screen = GetDC(nullptr);
    if (screen) {
        app.dpi = static_cast<unsigned>(GetDeviceCaps(screen, LOGPIXELSX));
        ReleaseDC(nullptr, screen);
    }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.lpfnWndProc = window_proc;
    cls.lpszClassName = L"XemuHddConverterWindow";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconResource));
    cls.hIconSm = cls.hIcon;
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&cls)) {
        CoUninitialize();
        return 1;
    }
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"Xemu HDD Tools 0.6b", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1280, app.dpi, 96), MulDiv(880, app.dpi, 96),
                                  nullptr, nullptr, instance, &app);
    if (!window) {
        CoUninitialize();
        return 1;
    }
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    int result = 0;
    while ((result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (app.context)
        app.context->cancelled.store(true);
    if (app.worker.joinable())
        app.worker.join();
    ShutdownGuiTheme();
    CoUninitialize();
    return result < 0 ? 1 : 0;
}
