#include "platform/WindowManager.h"

#include "platform/ProcessInfo.h"
#include <dwmapi.h>
#include <spdlog/spdlog.h>
#include <array>
#include <filesystem>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace flowspace::platform
{
    namespace
    {
        struct EnumWindowsContext
        {
            const WindowManager *manager{nullptr};
            const WindowDiscoveryOptions *options{nullptr};
            std::vector<core::WindowInfo> *results{nullptr};
        };

        struct CursorWindowContext
        {
            const WindowManager *manager{nullptr};
            POINT point{};
            HWND found{nullptr};
        };

        // Minimal RAII owner for Win32 HANDLEs. Replaces wil::unique_handle, which is
        // MSVC-oriented and not shipped with the MinGW toolchain.
        struct HandleCloser
        {
            void operator()(HANDLE handle) const noexcept
            {
                if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle);
                }
            }
        };
        using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;
    } // namespace

    WindowManager::WindowManager(const IMonitorManager &monitorManager)
        : monitorManager_(monitorManager)
    {
    }

    // Must be a real __stdcall function: a lambda cannot be converted to WNDENUMPROC
    // on 32-bit Windows, where the callback convention is stdcall.
    BOOL CALLBACK WindowManager::EnumWindowsProc(HWND hwnd, LPARAM lParam)
    {
        auto *context = reinterpret_cast<EnumWindowsContext *>(lParam);
        const auto *self = context->manager;

        std::string exclusionReason;
        const bool manageable = self->IsManageableWindow(hwnd, &exclusionReason);

        if (!manageable && !context->options->includeUnmanageableInReport)
        {
            return TRUE; // Skip this window, keep enumerating
        }

        core::WindowInfo info{};
        info.hwnd = hwnd;
        info.isManageable = manageable;
        info.exclusionReason = std::move(exclusionReason);
        info.title = GetWindowTitle(hwnd);
        info.className = GetWindowClassName(hwnd);

        DWORD pid = 0;
        info.threadId = GetWindowThreadProcessId(hwnd, &pid);
        info.processId = pid;

        if (pid != 0)
        {
            (void)flowspace::platform::ProcessInfo::QueryDetails(pid, info.executablePath, info.executableName, info.processCreationTime);
        }

        // Determine the granular visibility state
        DWORD cloakReason = 0;
        if (IsWindowCloaked(hwnd, &cloakReason))
        {
            info.visibilityState = core::WindowVisibilityState::CloakedByDwm;
        }
        else if (!IsWindowVisible(hwnd))
        {
            info.visibilityState = core::WindowVisibilityState::HiddenBySystem;
        }
        else if (IsIconic(hwnd))
        {
            info.visibilityState = core::WindowVisibilityState::Minimized;
        }
        else if (IsZoomed(hwnd))
        {
            info.visibilityState = core::WindowVisibilityState::Maximized;
        }
        else
        {
            info.visibilityState = core::WindowVisibilityState::Visible;
        }

        if (auto monitorId = self->monitorManager_.GetMonitorIdForWindow(hwnd); monitorId.has_value())
        {
            info.monitorStableId = std::move(*monitorId);
        }

        context->results->push_back(std::move(info));
        return TRUE;
    }

    // Returns true if the window is DWM-cloaked (eg: minimized UWP app, another virtual desktop) and optionally provides the cloak reason
    std::vector<core::WindowInfo> WindowManager::DiscoverWindows(const WindowDiscoveryOptions &options) const
    {
        std::vector<core::WindowInfo> windows;
        EnumWindowsContext context{this, &options, &windows};

        if (!EnumWindows(&WindowManager::EnumWindowsProc, reinterpret_cast<LPARAM>(&context)))
        {
            spdlog::error("EnumWindows failed with error {}", GetLastError());
        }

        return windows;
    }

    bool WindowManager::IsManageableWindow(HWND hwnd, std::string *outReason) const
    {
        auto setReason = [&](const char *reason)
        {
            if (outReason)
            {
                *outReason = reason;
            }
        };

        if (!IsWindow(hwnd))
        {
            setReason("Invalid HWND");
            return false;
        }

        // Never touch FlowSpace's own process windows
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId())
        {
            setReason("Owned by FlowSpace process");
            return false;
        }

        if (!IsWindowVisible(hwnd))
        {
            setReason("Not visible (WS_VISIBLE unset)");
            return false;
        }

        // DWM cloaking catches suspended UWP apps, hidden Windows 11 shell overlays,
        // and windows on other native Windows Virtual Desktops.
        DWORD cloakReason = 0;
        if (IsWindowCloaked(hwnd, &cloakReason))
        {
            setReason("DWM cloaked window");
            return false;
        }

        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

        if ((style & WS_CHILD) != 0)
        {
            setReason("Child window (WS_CHILD)");
            return false;
        }

        if ((exStyle & WS_EX_NOACTIVATE) != 0 && (exStyle & WS_EX_APPWINDOW) == 0)
        {
            setReason("Non-activating overlay (WS_EX_NOACTIVATE)");
            return false;
        }

        // Tool windows (floating palettes, tooltips) are excluded unless explicitly flagged WS_EX_APPWINDOW
        if ((exStyle & WS_EX_TOOLWINDOW) != 0 && (exStyle & WS_EX_APPWINDOW) == 0)
        {
            setReason("Tool window (WS_EX_TOOLWINDOW)");
            return false;
        }

        // Owned popups without WS_EX_APPWINDOW belong to an owner window and should follow their owner
        HWND owner = GetWindow(hwnd, GW_OWNER);
        if (owner != nullptr && (exStyle & WS_EX_APPWINDOW) == 0)
        {
            setReason("Owned secondary/dialog window");
            return false;
        }

        const std::wstring className = GetWindowClassName(hwnd);
        if (IsSystemOrShellWindow(hwnd, className))
        {
            setReason("Windows shell/system window");
            return false;
        }

        // Guard against zero-dimension helper windows (unless currently minimized)
        if (!IsIconic(hwnd))
        {
            RECT rect{};
            if (GetWindowRect(hwnd, &rect))
            {
                if ((rect.right - rect.left) <= 0 || (rect.bottom - rect.top) <= 0)
                {
                    setReason("Zero-area window");
                    return false;
                }
            }
        }

        return true;
    }

    bool WindowManager::IsWindowCloaked(HWND hwnd, DWORD *outCloakReason)
    {
        DWORD cloaked = 0;
        const HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (SUCCEEDED(hr) && cloaked != 0)
        {
            if (outCloakReason)
            {
                *outCloakReason = cloaked;
            }
            return true;
        }
        return false;
    }

    bool WindowManager::IsSystemOrShellWindow(HWND hwnd, const std::wstring &className)
    {
        if (hwnd == GetShellWindow() || hwnd == GetDesktopWindow())
        {
            return true;
        }

        //
        static constexpr std::array<std::wstring_view, 9> kExcludedClasses = {
            L"Progman",
            L"WorkerW",
            L"Shell_TrayWnd",
            L"Shell_SecondaryTrayWnd",
            L"Windows.UI.Core.CoreWindow",
            L"ApplicationFrameWindow", // Handled specially if empty/cloaked; basic shell host check
            L"ForegroundStaging",
            L"MultitaskingViewFrame",
            L"TaskListThumbnailWnd",
        };

        const std::wstring_view classNameView = className;
        for (const auto &excluded : kExcludedClasses)
        {
            if (classNameView != excluded)
            {
                continue;
            }

            // Note: UWP apps hosted inside ApplicationFrameWindow that are uncloaked and visible
            // have a child CoreWindow; allow uncloaked ApplicationFrameWindow in Phase 2 refinement.
            return classNameView != L"ApplicationFrameWindow";
        }

        return false;
    }

    std::wstring WindowManager::GetWindowTitle(HWND hwnd)
    {
        const int length = GetWindowTextLengthW(hwnd);
        if (length <= 0)
        {
            return {};
        }

        std::wstring title(static_cast<size_t>(length) + 1, L'\0');
        const int copied = GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
        if (copied > 0)
        {
            title.resize(static_cast<size_t>(copied));
        }
        else
        {
            title.clear();
        }
        return title;
    }

    std::wstring WindowManager::GetWindowClassName(HWND hwnd)
    {
        std::array<wchar_t, 256> buffer{};
        const int length = GetClassNameW(hwnd, buffer.data(), static_cast<int>(buffer.size()));
        if (length > 0)
        {
            return std::wstring(buffer.data(), static_cast<size_t>(length));
        }
        return {};
    }

    // --- Visibility control ---

    bool WindowManager::CaptureRestoreState(HWND hwnd, WindowRestoreState &outState) const
    {
        outState = WindowRestoreState{};
        if (!IsWindow(hwnd))
        {
            return false;
        }
        outState.wasMinimized = IsIconic(hwnd) != FALSE;
        outState.wasMaximized = IsZoomed(hwnd) != FALSE;

        // WINDOWPLACEMENT carries the restored (normal) position even when zoomed/minimized.
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(hwnd, &placement))
        {
            outState.restoreRect = placement.rcNormalPosition;
            if (!outState.wasMinimized && !outState.wasMaximized)
            {
                // For snapped windows, rcNormalPosition can contain the pre-snap bounds.
                // The current window rectangle is the layout the user expects restored.
                (void)GetWindowRect(hwnd, &outState.restoreRect);
            }
        }
        else
        {
            GetWindowRect(hwnd, &outState.restoreRect);
        }
        return true;
    }

    bool WindowManager::HideWindowFast(HWND hwnd, const WindowRestoreState &state, core::WindowVisibilityState *outNewState)
    {
        if (!IsWindow(hwnd))
        {
            return false;
        }
        (void)state; // Kept for symmetry and future restore-time decisions.
        const BOOL ok = ShowWindowAsync(hwnd, SW_HIDE);
        if (outNewState)
        {
            *outNewState = core::WindowVisibilityState::HiddenByFlowSpace;
        }
        if (!ok)
        {
            spdlog::debug("ShowWindowAsync(SW_HIDE) returned FALSE for HWND=0x{:08X}", reinterpret_cast<uintptr_t>(hwnd));
        }
        return ok != FALSE;
    }

    bool WindowManager::MinimizeWindowFast(HWND hwnd, core::WindowVisibilityState *outNewState)
    {
        if (!IsWindow(hwnd))
        {
            return false;
        }
        const BOOL ok = ShowWindowAsync(hwnd, SW_MINIMIZE);
        if (outNewState)
        {
            *outNewState = core::WindowVisibilityState::Minimized;
        }
        return ok != FALSE;
    }

    bool WindowManager::ShowWindowFast(HWND hwnd, const WindowRestoreState &state, core::WindowVisibilityState *outNewState)
    {
        if (!IsWindow(hwnd))
        {
            return false;
        }

        // The user may have minimized the window from the taskbar while it was hidden; respect
        // that state instead of popping it open.
        const bool minimizedNow = IsIconic(hwnd) != FALSE;
        const bool restoreMinimized = state.wasMinimized || minimizedNow;
        const UINT showCmd = restoreMinimized
                                 ? SW_SHOWMINNOACTIVE
                                 : (state.wasMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE);

        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (state.restoreRect.right > state.restoreRect.left && state.restoreRect.bottom > state.restoreRect.top)
        {
            if (!GetWindowPlacement(hwnd, &placement))
            {
                spdlog::warn("GetWindowPlacement failed for HWND=0x{:08X}", reinterpret_cast<uintptr_t>(hwnd));
            }
            else
            {
                placement.rcNormalPosition = state.restoreRect;
                placement.showCmd = showCmd;
                if (!SetWindowPlacement(hwnd, &placement))
                {
                    spdlog::warn("SetWindowPlacement failed for HWND=0x{:08X}", reinterpret_cast<uintptr_t>(hwnd));
                }
            }
        }

        const BOOL ok = ShowWindowAsync(hwnd, showCmd);
        if (outNewState)
        {
            *outNewState = restoreMinimized
                               ? core::WindowVisibilityState::Minimized
                               : (state.wasMaximized ? core::WindowVisibilityState::Maximized
                                                     : core::WindowVisibilityState::Visible);
        }
        return ok != FALSE;
    }

    // returns a snapshot of the foreground window, or an empty snapshot with hwnd == nullptr if no foreground window is present or it is unmanageable.
    // foreground window = the window that is currently receiving keyboard input (or the last one that did, if none are active)
    core::WindowInfo WindowManager::GetForegroundWindow() const
    {
        const HWND hwnd = ::GetForegroundWindow();
        if (!hwnd)
        {
            return {};
        }
        if (!IsManageableWindow(hwnd))
        {
            return {};
        }

        for (const auto &win : DiscoverWindows(WindowDiscoveryOptions{.includeUnmanageableInReport = false}))
        {
            if (win.hwnd == hwnd)
            {
                return win;
            }
        }
        return {};
    }

    BOOL CALLBACK WindowManager::CursorWindowProc(HWND hwnd, LPARAM lParam)
    {
        auto *context = reinterpret_cast<CursorWindowContext *>(lParam);
        if (!context->manager->IsManageableWindow(hwnd))
        {
            return TRUE;
        }

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect))
        {
            return TRUE;
        }
        if (context->point.x < rect.left || context->point.x >= rect.right || context->point.y < rect.top || context->point.y >= rect.bottom)
        {
            return TRUE;
        }

        context->found = hwnd;
        return FALSE; // EnumWindows walks topmost-first, so the first hit is the one the user sees
    }

    core::WindowInfo WindowManager::GetWindowUnderCursor() const
    {
        POINT point{};
        if (!GetCursorPos(&point))
        {
            return {};
        }

        CursorWindowContext context{this, point, nullptr};
        EnumWindows(&WindowManager::CursorWindowProc, reinterpret_cast<LPARAM>(&context));
        if (context.found == nullptr)
        {
            return {};
        }

        for (const auto &win : DiscoverWindows(WindowDiscoveryOptions{.includeUnmanageableInReport = false}))
        {
            if (win.hwnd == context.found)
            {
                return win;
            }
        }
        return {};
    }

    std::wstring WindowManager::GetMonitorIdForWindow(HWND hwnd) const
    {
        return monitorManager_.GetMonitorIdForWindow(hwnd).value_or(std::wstring{});
    }

    bool WindowManager::MoveWindowToMonitor(HWND hwnd, const std::wstring &monitorStableId) const
    {
        const auto mon = monitorManager_.GetMonitor(monitorStableId);
        if (!mon.has_value() || !IsWindow(hwnd))
        {
            return false;
        }

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect))
        {
            return false;
        }

        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        const int x = mon->bounds.left + (mon->bounds.width() - width) / 2;
        const int y = mon->bounds.top + (mon->bounds.height() - height) / 2;

        return SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
    }

    bool WindowManager::IsWindowFromProcess(HWND hwnd, DWORD pid, const std::wstring &exePath, uint64_t creationTime) const
    {
        if (!IsWindow(hwnd))
        {
            return false;
        }
        DWORD actualPid = 0;
        GetWindowThreadProcessId(hwnd, &actualPid);
        if (actualPid != pid)
        {
            return false;
        }
        if (exePath.empty() && creationTime == 0)
        {
            // Only the PID matched. Reused HWNDs after a process restart share nothing else, so
            // this is a weak match; callers decide whether a weak match is acceptable.
            return true;
        }
        return flowspace::platform::ProcessInfo::IdentityMatches(pid, exePath, creationTime);
    }
}
