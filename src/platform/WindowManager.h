#pragma once

#include "core/Models.h"
#include "platform/MonitorManager.h"
#include <memory>
#include <vector>

namespace flowspace::platform
{
    struct WindowDiscoveryOptions
    {
        bool includeUnmanageableInReport{
            true}; // Include windows that cannot be managed by FlowSpace in the discovery report
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
        // Finds top-level windows and collects their metadata.
        [[nodiscard]] virtual std::vector<core::WindowInfo>
        DiscoverWindows(const WindowDiscoveryOptions &options = {}) const = 0;
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

        [[nodiscard]] std::vector<core::WindowInfo>
        DiscoverWindows(const WindowDiscoveryOptions &options = {}) const override;

        [[nodiscard]] bool IsManageableWindow(HWND hwnd, std::string *outReason = nullptr) const override;

        // Captures the window's current visibility state. Call before HideWindowFast.
        bool CaptureRestoreState(HWND hwnd, WindowRestoreState &outState) const;

        // Hides a window without waiting for it; synchronous calls can hang on some apps.
        // The app keeps running, but its taskbar button and Alt+Tab entry disappear.
        bool HideWindowFast(HWND hwnd, const WindowRestoreState &state,
                            core::WindowVisibilityState *outNewState = nullptr);

        // Minimize-based alternative to HideWindowFast for applications that misbehave when
        // hidden. Keeps the taskbar button, which makes the window discoverable.
        bool MinimizeWindowFast(HWND hwnd, core::WindowVisibilityState *outNewState = nullptr);

        // Restores the window without taking focus, preserving its previous minimized state.
        bool ShowWindowFast(HWND hwnd, const WindowRestoreState &state,
                            core::WindowVisibilityState *outNewState = nullptr);

        // Snapshot of the foreground window (or an empty snapshot with hwnd == nullptr).
        [[nodiscard]] core::WindowInfo GetForegroundWindow() const;

        // Topmost manageable window containing the cursor (Z-order aware). Preferred over
        // the foreground window for cursor-relative actions, which can otherwise act on a
        // stale window left over from a previous monitor.
        [[nodiscard]] core::WindowInfo GetWindowUnderCursor() const;

        // Re-resolves the monitor stable id for a window (used after the user drags it).
        [[nodiscard]] std::wstring GetMonitorIdForWindow(HWND hwnd) const;

        // Moves a window's top-left to the center of the given monitor without activation.
        bool MoveWindowToMonitor(HWND hwnd, const std::wstring &monitorStableId) const;

        // True when the window is still alive and belongs to the expected process identity.
        [[nodiscard]] bool IsWindowFromProcess(HWND hwnd, DWORD pid, const std::wstring &exePath,
                                               uint64_t creationTime) const;

      private:
        // Enumerates top-level windows. The context is an EnumWindowsContext* passed through LPARAM.
        static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
        // Finds the first manageable window containing the cursor (EnumWindows is Z-ordered).
        static BOOL CALLBACK CursorWindowProc(HWND hwnd, LPARAM lParam);
        // Returns true when Windows has hidden the window from the desktop.
        [[nodiscard]] static bool IsWindowCloaked(HWND hwnd, DWORD *outCloakReason = nullptr);
        // Returns true for system windows such as the taskbar or Start menu.
        [[nodiscard]] static bool IsSystemOrShellWindow(HWND hwnd, const std::wstring &className);
        [[nodiscard]] static std::wstring GetWindowTitle(HWND hwnd);
        [[nodiscard]] static std::wstring GetWindowClassName(HWND hwnd);
        const IMonitorManager &monitorManager_;
    };

}
