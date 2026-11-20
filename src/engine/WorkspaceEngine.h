#pragma once

#include "core/ConfigService.h"
#include "core/Models.h"
#include "core/RecoveryManager.h"
#include "core/WorkspaceManager.h"
#include "platform/HotkeyManager.h"
#include "platform/MonitorManager.h"
#include "platform/ProcessInfo.h"
#include "platform/WindowManager.h"

#include <Windows.h>
#include <dwmapi.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace flowspace::engine
{
    enum class SwitchDirection
    {
        Previous,
        Next,
    };

    enum class WindowEventType
    {
        Created,
        Shown,
        Hidden,
        Destroyed,
        MoveSizeEnd,
        Foreground,
    };

    struct SwitchResult
    {
        bool success{false};
        std::wstring monitorStableId;
        std::string workspaceId;
        int windowsHidden{0};
        int windowsShown{0};
        int windowErrors{0};
        std::string error; // engine-level failure reason, empty on success
    };

    /// Handles workspace switching and window events on the app's main message loop.
    /// It coordinates window visibility, recovery journaling, wallpapers, and UI updates.
    class WorkspaceEngine
    {
      public:
        WorkspaceEngine(platform::MonitorManager &monitors, platform::WindowManager &windows,
                        core::WorkspaceManager &workspaces, core::ConfigService &config,
                        core::RecoveryManager &recovery, platform::HotkeyManager &hotkeys);

        void Attach(HWND messageWindow);

        // Startup sequence: reconcile monitors, apply persisted state, restore journal-hidden
        // windows from a previous crash, register hotkeys.
        void Initialize();
        void Shutdown();

        SwitchResult SwitchMonitorToWorkspace(const std::wstring &monitorStableId, const std::string &workspaceId);
        SwitchResult SwitchRelative(const std::wstring &monitorStableId, SwitchDirection direction);
        SwitchResult SwitchByNumber(const std::wstring &monitorStableId, int numberOneBased);
        bool MoveActiveWindowToWorkspace(const std::string &workspaceId, const std::wstring &monitorStableId = {},
                                         const core::WindowInfo *window = nullptr);
        bool MoveActiveWindowRelative(SwitchDirection direction);
        bool MoveActiveWindowByNumber(int numberOneBased);
        void RevealAllWindows(); // emergency reveal-all; also used on clean shutdown

        // Reconciles engine state against the current display topology (WM_DISPLAYCHANGE).
        void HandleDisplayChange();

        // Handle a window event on the app's main thread.
        void HandleWindowEvent(WindowEventType type, HWND hwnd);

        // Hotkey handling from WM_HOTKEY. Returns true when the id was recognized.
        bool HandleHotkey(int hotkeyId);

        // Monitor targeted by monitor-relative actions (hotkeys, switcher).
        std::wstring ResolveActionMonitor() const;

        // Re-applies hotkeys after shortcut changes; returns conflicts for the UI.
        std::vector<std::wstring> ApplyHotkeys();

        [[nodiscard]] std::vector<core::Workspace> GetWorkspaces() const; // all workspaces, grouped by monitor
        [[nodiscard]] std::vector<core::Workspace> GetWorkspacesForMonitor(const std::wstring &monitorStableId) const;
        [[nodiscard]] core::Workspace &WorkspaceAt(size_t index); // global index into GetWorkspaces()
        [[nodiscard]] std::vector<core::MonitorInfo> GetMonitors() const;
        [[nodiscard]] core::WindowInfo GetForegroundWindowInfo() const;
        [[nodiscard]] std::optional<core::WindowInfo> GetLastFocusedWindowInfo() const;
        [[nodiscard]] std::optional<std::string> GetActiveWorkspaceId(const std::wstring &monitorStableId) const;
        // Capture the current foreground window for use by "+ Add rule" (called when Settings opens).
        void CaptureForegroundWindowInfo();
        std::string CreateWorkspace(const std::string &name, const std::wstring &monitorStableId);
        void RenameWorkspaceIndex(int index, const std::string &name);
        void DuplicateWorkspaceIndex(int index);
        void ResetWorkspaceIndex(int index);
        void DeleteWorkspaceIndex(int index);
        void SwitchMonitorToWorkspaceIndex(int monitorIndex, int workspaceIndex);
        void RevealSingleWindow(HWND hwnd);
        // Copies the workspace manager's state into the config so the next Save persists it.
        void PersistWorkspacesToConfig();

        // Applies app rules to all currently open windows, moving each matching window
        // to its assigned workspace on the correct monitor. Called at startup so existing
        // windows are placed according to rules.
        void ApplyRulesToOpenWindows();

        // UI callback: show the switch overlay for a workspace on a monitor.
        std::function<void(const core::Workspace &, const std::wstring &monitorStableId)> OnOverlayShow;
        // UI callback: engine state changed; the UI should refresh.
        std::function<void()> OnStateChanged;

        bool IsBusy() const { return switchInProgress_; }

      private:
        bool ApplySwitch(const std::wstring &monitorStableId, const std::string &workspaceId, SwitchResult &result);
        std::string ResolveAdjacentWorkspace(const std::wstring &monitorStableId, SwitchDirection direction) const;
        // Resolves a monitor the workspace manager actually knows about, using the
        // window's own monitor, then the monitor under the cursor, then the primary.
        std::wstring ResolveWindowActionMonitor(const core::WindowInfo &window) const;
        // Cursor-relative first; falls back to the foreground snapshot only when no
        // manageable window is under the pointer.
        core::WindowInfo ResolveMoveWindow() const;
        void HideWindowInternal(const core::WindowInfo &window, const std::string &workspaceId);
        void ShowWindowInternal(HWND hwnd, bool wasMinimized);

        void ProcessNewWindow(HWND hwnd);
        void ProcessShownWindow(HWND hwnd);
        void ProcessMovedWindow(HWND hwnd);
        bool MatchesRule(const core::WindowInfo &window, const core::AppRule &rule) const;
        std::string ResolveAssignmentForNewWindow(const core::WindowInfo &window,
                                                  const core::AppRule **matchedRule) const;

        platform::MonitorManager &monitors_;
        platform::WindowManager &windows_;
        core::WorkspaceManager &workspaces_;
        core::ConfigService &config_;
        core::RecoveryManager &recovery_;
        platform::HotkeyManager &hotkeys_;
        HWND messageWindow_ = nullptr;
        bool switchInProgress_ = false;
        // Last focused manageable (non-FlowSpace) window — used by the "+ Add rule" UI
        // because clicking the button makes FlowSpace Settings the foreground window.
        std::optional<core::WindowInfo> lastFocusedWindowInfo_;
    };
}
