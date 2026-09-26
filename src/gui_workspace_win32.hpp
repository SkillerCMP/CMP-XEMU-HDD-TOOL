// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xhc/workspace.hpp"
// Include after guarded Windows headers. This object is used only on the UI thread.
struct GuiWorkspace {
    xhc::ui::Workspace state;
    HWND owner = nullptr;
    HFONT font = nullptr;
    unsigned dpi = 96;
};
constexpr UINT WorkspaceChangedMessage = WM_APP + 71;
constexpr UINT SnapshotQuestionMessage = WM_APP + 72;
struct SnapshotQuestion {
    uint64_t job, count;
    std::wstring source;
    xhc::Context* context;
};
xhc::fs::path PickWorkspaceFile(HWND owner, xhc::ui::Format format, bool output);
xhc::fs::path PickWorkspaceFolder(HWND owner, bool output);
bool ConfirmWorkspaceOperation(GuiWorkspace& shared, xhc::Options& options, const std::wstring& operation, bool writes,
                               const std::wstring& detail = {});
bool EditWorkspaceHelpers(GuiWorkspace& shared);
xhc::ui::Ticket BeginWorkspaceJob(GuiWorkspace& shared);
void FinishWorkspaceJob(GuiWorkspace& shared, uint64_t job);
void ConnectSnapshotQuestion(GuiWorkspace& shared, xhc::Options& options, const std::shared_ptr<xhc::Context>& ctx,
                             uint64_t job);
bool AnswerSnapshotQuestion(GuiWorkspace& shared, const SnapshotQuestion& question);
