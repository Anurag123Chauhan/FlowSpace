#pragma once

#include "core/Models.h"
#include "platform/MonitorManager.h"
#include <memory>
#include <vector>

namespace flowspace::platform
{
    struct WindowDiscoveryOptions
    {
        bool includeUnmanageableInReport{true}; // Include windows that cannot be managed by FlowSpace in the discovery report
    };

    // Visibility state captured immediately before FlowSpace hides a window, used to restore
    // the window to its previous size/position/state on the next switch.
    struct WindowRestoreState
    {
        bool wasMinimized{false};
        bool wasMaximized{false};
        RECT restoreRect{}; // Normal (restored) bounds captured while visible
    };

    class IWindowManager
    {
    public:
        virtual ~IWindowManager() = default;
        // Discovers all top-level windows currently present on the system, along with their associated metadata
        [[nodiscard]] virtual std::vector<core::WindowInfo> DiscoverWindows(const WindowDiscoveryOptions &options = {}) const = 0;
        // Evaluates whether a given HWND qualifies as a user-manageable application window.
        [[nodiscard]] virtual bool IsManageableWindow(HWND hwnd, std::string *outReason = nullptr) const = 0;
    };

    class WindowManager final : public IWindowManager
    {
    public:
        explicit WindowManager(const IMonitorManager &monitorManager);
        ~WindowManager() override = default;

        WindowManager(const WindowManager &) = delete;
        WindowManager &operator=(const WindowManager &) = delete;

        [[nodiscard]] std::vector<core::WindowInfo> DiscoverWindows(const WindowDiscoveryOptions &options = {}) const override;

        [[nodiscard]] bool IsManageableWindow(HWND hwnd, std::string *outReason = nullptr) const override;

        // --- Visibility control ---

        // Captures the window's current visibility state. Call before HideWindowFast.
        bool CaptureRestoreState(HWND hwnd, WindowRestoreState &outState) const;

        // Hides a window with ShowWindowAsync(SW_HIDE). Asynchronous on purpose: some
        // applications deadlock when a foreign thread calls the synchronous ShowWindow.
        // The window is not destroyed and its process keeps running; it simply loses its
        // taskbar button and Alt+Tab entry until shown again.
        bool HideWindowFast(HWND hwnd, const WindowRestoreState &state, core::WindowVisibilityState *outNewState = nullptr);

        // Minimize-based alternative to HideWindowFast for applications that misbehave when
        // hidden. Keeps the taskbar button, which makes the window discoverable.
        bool MinimizeWindowFast(HWND hwnd, core::WindowVisibilityState *outNewState = nullptr);

        // Restores a previously hidden window without stealing activation. Windows that were
        // minimized at hide time come back minimized (SW_SHOWMINNOACTIVE); everything else is
        // shown at its most recent size/position without activation (SW_SHOWNOACTIVATE).
        bool ShowWindowFast(HWND hwnd, const WindowRestoreState &state, core::WindowVisibilityState *outNewState = nullptr);

        // --- Queries ---

        // Snapshot of the foreground window (or an empty snapshot with hwnd == nullptr).
        [[nodiscard]] core::WindowInfo GetForegroundWindow() const;

        // Re-resolves the monitor stable id for a window (used after the user drags it).
        [[nodiscard]] std::wstring GetMonitorIdForWindow(HWND hwnd) const;

        // Moves a window's top-left to the center of the given monitor without activation.
        bool MoveWindowToMonitor(HWND hwnd, const std::wstring &monitorStableId) const;

        // True when the window is still alive and belongs to the expected process identity.
        [[nodiscard]] bool IsWindowFromProcess(HWND hwnd, DWORD pid, const std::wstring &exePath, uint64_t creationTime) const;

    private:
        // Enumerates top-level windows. The context is an EnumWindowsContext* passed through LPARAM.
        static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
        // Returns true if the window is DWM-cloaked (eg: minimized UWP app, another virtual desktop) and optionally provides the cloak reason
        [[nodiscard]] static bool IsWindowCloaked(HWND hwnd, DWORD *outCloakReason = nullptr);
        // Return true if the window is a system or shell window (eg: taskbar, start menu, etc.) and optionally provide a reason code for the exclusion
        [[nodiscard]] static bool IsSystemOrShellWindow(HWND hwnd, const std::wstring &className);
        [[nodiscard]] static std::wstring GetWindowTitle(HWND hwnd);
        [[nodiscard]] static std::wstring GetWindowClassName(HWND hwnd);
        const IMonitorManager &monitorManager_;
    };

}
