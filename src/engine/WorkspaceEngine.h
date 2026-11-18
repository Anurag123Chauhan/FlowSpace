#pragma once

#include "core/ConfigService.h"
#include "core/Models.h"
#include "core/RecoveryManager.h"
#include "core/WorkspaceManager.h"
#include "platform/HotkeyManager.h"
#include "platform/MonitorManager.h"
#include "platform/ProcessInfo.h"
#include "platform/WindowManager.h"
#include "platform/WallpaperManager.h"

#include <Windows.h>
#include <dwmapi.h>
#include <functional>
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

    /// <summary>
    /// The workspace engine. Owns the hidden message window that receives WM_HOTKEY,
    /// WM_DISPLAYCHANGE and WinEvent hook callbacks, and orchestrates workspace switching:
    /// window discovery, assignment resolution, hide/show with recovery journaling, wallpaper
    /// application and overlay display. All state-changing operations run on the main thread's
    /// message loop, which serializes them by construction — no overlapping switches.
    /// </summary>
    class WorkspaceEngine
    {
    public:
        WorkspaceEngine(platform::MonitorManager &monitors,
                        platform::WindowManager &windows,
                        core::WorkspaceManager &workspaces,
                        core::ConfigService &config,
                        core::RecoveryManager &recovery,
                        platform::WallpaperManager &wallpaper,
                        platform::HotkeyManager &hotkeys);
        ~WorkspaceEngine();

        // Creates nothing; the message window is created by the app and passed here.
        void Attach(HWND messageWindow);

        // Startup sequence: reconcile monitors, apply persisted state, capture wallpapers on
        // first run, restore journal-hidden windows from a previous crash, register hotkeys.
        void Initialize();
        void Shutdown(bool restoringWallpaper);

        // --- Actions (called from hotkeys, tray, and the settings UI) ---
        SwitchResult SwitchMonitorToWorkspace(const std::wstring &monitorStableId, const std::string &workspaceId);
        SwitchResult SwitchRelative(const std::wstring &monitorStableId, SwitchDirection direction);
        SwitchResult SwitchByNumber(const std::wstring &monitorStableId, int numberOneBased);
        bool MoveActiveWindowToWorkspace(const std::string &workspaceId);
        bool MoveWindowToWorkspace(HWND hwnd, const std::string &workspaceId);
        bool MoveActiveWindowRelative(SwitchDirection direction);
        bool MoveActiveWindowByNumber(int numberOneBased);
        void RevealAllWindows(); // emergency reveal-all; also used on clean shutdown

        // Reconciles engine state against the current display topology (WM_DISPLAYCHANGE).
        void HandleDisplayChange();

        // WinEvent hook callback. eventThread-safe: must be called on the engine thread.
        void HandleWindowEvent(WindowEventType type, HWND hwnd);

        // Hotkey handling from WM_HOTKEY. Returns true when the id was recognized.
        bool HandleHotkey(int hotkeyId);

        // Monitor targeted by monitor-relative actions (hotkeys, switcher).
        std::wstring ResolveActionMonitor() const;

        // Re-applies hotkeys after shortcut changes; returns conflicts for the UI.
        std::vector<std::wstring> ApplyHotkeys();

        // Re-applies the wallpaper of the given monitor from its active workspace immediately.
        void ApplyWallpaperForMonitor(const std::wstring &monitorStableId);

        // --- UI facade (settings window convenience accessors) ---
        [[nodiscard]] std::vector<core::Workspace> GetWorkspaces() const; // sorted by orderIndex
        [[nodiscard]] core::Workspace &WorkspaceAt(size_t index);        // mutable reference for editors
        [[nodiscard]] std::vector<core::MonitorInfo> GetMonitors() const;
        [[nodiscard]] core::WindowInfo GetForegroundWindowInfo() const;
        // Last foreground window that belonged to another app (FlowSpace's own windows ignored).
        [[nodiscard]] core::WindowInfo GetLastExternalForegroundWindow() const;
        std::string CreateWorkspace(const std::string &name);
        void RenameWorkspaceIndex(int index, const std::string &name);
        void DuplicateWorkspaceIndex(int index);
        void ResetWorkspaceIndex(int index);
        void DeleteWorkspaceIndex(int index);
        void SetWorkspaceWallpaperIndex(int index, const std::wstring &imagePath);
        void SwitchMonitorToWorkspaceIndex(int monitorIndex, int workspaceIndex);
        void RevealSingleWindow(HWND hwnd);
        void RestoreOriginalWallpaperForMonitorIndex(int monitorIndex);

        // Captures the user's current wallpapers once, on first activation.
        void CaptureOriginalWallpapers();

        // UI callback: show the switch overlay for a workspace on a monitor.
        std::function<void(const core::Workspace &, const std::wstring &monitorStableId)> OnOverlayShow;
        // UI callback: engine state changed; the UI should refresh.
        std::function<void()> OnStateChanged;

        bool IsBusy() const { return switchInProgress_; }

        // Test seam: when set, only windows accepted by the filter are ever managed.
        void SetWindowFilter(std::function<bool(HWND)> filter) { windowFilter_ = std::move(filter); }

    private:
        [[nodiscard]] std::vector<core::WindowInfo> DiscoverManagedWindows() const;
        [[nodiscard]] bool PassesFilter(HWND hwnd) const { return !windowFilter_ || windowFilter_(hwnd); }
        [[nodiscard]] std::optional<core::WindowInfo> FindManagedWindow(HWND hwnd) const;
        // --- switching internals ---
        bool ApplySwitch(const std::wstring &monitorStableId, const std::string &workspaceId, SwitchResult &result);
        std::string ResolveAdjacentWorkspace(const std::wstring &monitorStableId, SwitchDirection direction) const;
        void HideWindowInternal(const core::WindowInfo &window, const std::string &workspaceId);
        void ShowWindowInternal(HWND hwnd, bool wasMinimized);

        // --- window event internals ---
        void ProcessNewWindow(HWND hwnd);
        void AdoptNewWindow(const core::WindowInfo &window);
        void ProcessShownWindow(HWND hwnd);
        void ProcessMovedWindow(HWND hwnd);
        bool MatchesRule(const core::WindowInfo &window, const core::AppRule &rule) const;
        std::string ResolveAssignmentForNewWindow(const core::WindowInfo &window, const core::AppRule **matchedRule) const;

        platform::MonitorManager &monitors_;
        platform::WindowManager &windows_;
        core::WorkspaceManager &workspaces_;
        core::ConfigService &config_;
        core::RecoveryManager &recovery_;
        platform::WallpaperManager &wallpaper_;
        platform::HotkeyManager &hotkeys_;
        HWND messageWindow_ = nullptr;
        bool switchInProgress_ = false;
        std::function<bool(HWND)> windowFilter_;
        HWND lastExternalForeground_ = nullptr;
    };
}
