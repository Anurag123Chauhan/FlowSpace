#include "engine/WorkspaceEngine.h"
#include "core/StringUtils.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

namespace flowspace::engine
{
    namespace
    {
        bool IsEffectivelyVisible(const core::WindowVisibilityState state)
        {
            return state == core::WindowVisibilityState::Visible || state == core::WindowVisibilityState::Maximized || state == core::WindowVisibilityState::Minimized;
        }

        std::string Narrow(const std::wstring &w) { return core::WideToUtf8(w); }
    }

    WorkspaceEngine::WorkspaceEngine(platform::MonitorManager &monitors,
                                     platform::WindowManager &windows,
                                     core::WorkspaceManager &workspaces,
                                     core::ConfigService &config,
                                     core::RecoveryManager &recovery,
                                     platform::WallpaperManager &wallpaper,
                                     platform::HotkeyManager &hotkeys)
        : monitors_(monitors), windows_(windows), workspaces_(workspaces), config_(config),
          recovery_(recovery), wallpaper_(wallpaper), hotkeys_(hotkeys)
    {
    }

    void WorkspaceEngine::Attach(HWND messageWindow)
    {
        messageWindow_ = messageWindow;
    }

    std::vector<std::wstring> WorkspaceEngine::ApplyHotkeys()
    {
        auto conflicts = hotkeys_.RegisterAll(messageWindow_, config_.Get().hotkeys);
        return conflicts;
    }

    void WorkspaceEngine::Initialize()
    {
        const auto detected = monitors_.EnumerateMonitors();
        workspaces_.ReconcileMonitors(detected);

        auto &config = config_.Get();

        // Apply persisted per-monitor active workspace state.
        const auto defaultWs = workspaces_.GetWorkspaces().empty()
                                   ? std::string{}
                                   : workspaces_.GetWorkspaces().front().id;
        for (const auto &mon : detected)
        {
            auto it = config.monitorState.find(mon.stableId);
            if (it != config.monitorState.end())
            {
                workspaces_.SetActiveWorkspace(mon.stableId, it->second);
            }
            else if (!defaultWs.empty())
            {
                workspaces_.SetActiveWorkspace(mon.stableId, defaultWs);
            }
        }

        // Reconcile stale hidden windows: drop tracker entries whose HWNDs no longer exist.
        for (const auto &win : workspaces_.GetTrackedWindows())
        {
            if (win.visibilityState == core::WindowVisibilityState::HiddenByFlowSpace && !IsWindow(win.hwnd))
            {
                workspaces_.ForgetWindow(win.hwnd);
            }
        }

        // Restore windows hidden by a previous (crashed) FlowSpace session. Every entry is
        // validated against the live process identity before any restoration is attempted.
        int restored = 0;
        int dropped = 0;
        auto entries = recovery_.Entries(); // copy; we mutate while iterating
        for (const auto &entry : entries)
        {
            if (!IsWindow(entry.hwnd))
            {
                ++dropped;
                recovery_.Forget(entry.hwnd);
                continue;
            }
            if (!windows_.IsWindowFromProcess(entry.hwnd, entry.processId, entry.executablePath, entry.processCreationTime))
            {
                // HWND reused by a different process — never act on it.
                spdlog::warn("Recovery: HWND 0x{:08X} no longer belongs to expected process {}; dropping",
                             reinterpret_cast<uintptr_t>(entry.hwnd), entry.processId);
                ++dropped;
                recovery_.Forget(entry.hwnd);
                continue;
            }
            if (IsWindowVisible(entry.hwnd))
            {
                ++dropped; // already visible; nothing to restore
                recovery_.Forget(entry.hwnd);
                continue;
            }

            platform::WindowRestoreState state;
            state.wasMinimized = entry.wasMinimized;
            state.wasMaximized = entry.wasMaximized;
            state.restoreRect = entry.restoreRect;
            if (windows_.ShowWindowFast(entry.hwnd, state))
            {
                ++restored;
                recovery_.RecordShow(entry.hwnd);
            }
        }
        if (restored > 0 || dropped > 0)
        {
            spdlog::info("Recovery: restored {} window(s), dropped {} stale entr(ies)", restored, dropped);
        }

        CaptureOriginalWallpapers();
        ApplyHotkeys();
        config_.Save();
    }

    void WorkspaceEngine::CaptureOriginalWallpapers()
    {
        auto &config = config_.Get();
        if (config.settings.wallpaperCaptureInitialized || !config.settings.wallpaperManagementEnabled)
        {
            return;
        }
        if (!wallpaper_.IsAvailable())
        {
            return;
        }
        const auto captured = wallpaper_.CaptureAll();
        config.originalWallpapers.clear();
        for (const auto &c : captured)
        {
            core::OriginalWallpaper w;
            w.monitorDevicePath = c.monitorDevicePath;
            w.imagePath = c.imagePath;
            w.backgroundColor = static_cast<uint32_t>(c.backgroundColor);
            w.fit = static_cast<int32_t>(c.position);
            config.originalWallpapers.push_back(w);
        }
        config.settings.wallpaperCaptureInitialized = true;
        spdlog::info("Captured original wallpaper state for {} monitor(s)", config.originalWallpapers.size());
    }

    void WorkspaceEngine::Shutdown(bool restoringWallpaper)
    {
        hotkeys_.UnregisterAll();
        RevealAllWindows();
        if (restoringWallpaper && wallpaper_.IsAvailable())
        {
            std::vector<platform::CapturedWallpaper> captured;
            for (const auto &w : config_.Get().originalWallpapers)
            {
                platform::CapturedWallpaper c;
                c.monitorDevicePath = w.monitorDevicePath;
                c.imagePath = w.imagePath;
                c.backgroundColor = static_cast<COLORREF>(w.backgroundColor);
                c.position = static_cast<platform::WallpaperPosition>(w.fit);
                captured.push_back(c);
            }
            wallpaper_.RestoreAll(captured);
        }
        config_.Save();
    }

    std::wstring WorkspaceEngine::ResolveActionMonitor() const
    {
        const bool byMouse = config_.Get().settings.switchByMouseMonitor;
        if (byMouse)
        {
            if (auto mon = monitors_.GetMonitorIdUnderCursor())
            {
                return *mon;
            }
        }
        // Primary fallback.
        for (const auto &mon : monitors_.EnumerateMonitors())
        {
            if (mon.isPrimary)
            {
                return mon.stableId;
            }
        }
        const auto all = monitors_.EnumerateMonitors();
        return all.empty() ? std::wstring{} : all.front().stableId;
    }

    std::string WorkspaceEngine::ResolveAdjacentWorkspace(const std::wstring &monitorStableId, SwitchDirection direction) const
    {
        const auto resolved = workspaces_.ResolveAdjacentWorkspaceId(
            monitorStableId, direction == SwitchDirection::Next
                                 ? core::WorkspaceSwitchDirection::Next
                                 : core::WorkspaceSwitchDirection::Previous);
        return resolved.value_or(std::string{});
    }

    std::wstring WorkspaceEngine::ResolveWindowActionMonitor(const core::WindowInfo &window) const
    {
        // Only accept IDs the workspace manager tracks; a stale/empty window monitor ID
        // is what makes ResolveAdjacentWorkspace() silently fail on some hotkey presses.
        auto isKnown = [this](const std::wstring &stableId)
        {
            if (stableId.empty())
            {
                return false;
            }
            const auto monitors = workspaces_.GetMonitors();
            return std::any_of(monitors.begin(), monitors.end(), [&](const core::MonitorInfo &mon)
                               { return mon.stableId == stableId; });
        };

        if (isKnown(window.monitorStableId))
        {
            return window.monitorStableId;
        }

        // The user is interacting with the display under the cursor, so use that monitor.
        if (auto cursorId = monitors_.GetMonitorIdUnderCursor())
        {
            if (isKnown(*cursorId))
            {
                spdlog::debug("Move: window monitor '{}' unavailable; using cursor monitor '{}'", Narrow(window.monitorStableId), Narrow(*cursorId));
                return *cursorId;
            }
        }

        // Last resort: ResolveActionMonitor() falls back primary -> first monitor.
        const auto fallback = ResolveActionMonitor();
        spdlog::warn("Move: window and cursor monitors unavailable; using '{}'", Narrow(fallback));
        return fallback;
    }

    core::WindowInfo WorkspaceEngine::ResolveMoveWindow() const
    {
        // The cursor is the strongest hint about which monitor/window the user means.
        // GetForegroundWindow() can lag behind (e.g. VS Code on another monitor stays
        // "last active"), so only use it if nothing manageable is under the pointer.
        if (auto underCursor = windows_.GetWindowUnderCursor(); underCursor.hwnd != nullptr)
        {
            spdlog::debug("Move: using window under cursor (HWND=0x{:08X}, '{}')", reinterpret_cast<uintptr_t>(underCursor.hwnd), Narrow(underCursor.title));
            return underCursor;
        }
        return windows_.GetForegroundWindow();
    }

    SwitchResult WorkspaceEngine::SwitchMonitorToWorkspace(const std::wstring &monitorStableId, const std::string &workspaceId)
    {
        SwitchResult result;
        result.monitorStableId = monitorStableId;
        result.workspaceId = workspaceId;

        if (switchInProgress_)
        {
            result.error = "a switch is already in progress";
            return result;
        }

        const auto ws = workspaces_.GetWorkspace(workspaceId);
        if (!ws.has_value())
        {
            result.error = "unknown workspace id";
            spdlog::warn("Switch rejected: unknown workspace '{}'", workspaceId);
            return result;
        }

        const auto active = workspaces_.GetActiveWorkspaceId(monitorStableId);
        if (!active.has_value())
        {
            // Unknown monitor (disconnected?). Refresh topology and retry once.
            HandleDisplayChange();
            if (!workspaces_.GetActiveWorkspaceId(monitorStableId).has_value())
            {
                result.error = "monitor is not connected";
                spdlog::warn("Switch rejected: monitor '{}' not found", Narrow(monitorStableId));
                return result;
            }
        }

        result.success = ApplySwitch(monitorStableId, workspaceId, result);
        return result;
    }

    SwitchResult WorkspaceEngine::SwitchRelative(const std::wstring &monitorStableId, SwitchDirection direction)
    {
        const auto target = ResolveAdjacentWorkspace(monitorStableId, direction);
        SwitchResult result;
        result.monitorStableId = monitorStableId;
        if (target.empty())
        {
            result.error = "no workspaces configured";
            return result;
        }
        return SwitchMonitorToWorkspace(monitorStableId, target);
    }

    SwitchResult WorkspaceEngine::SwitchByNumber(const std::wstring &monitorStableId, int numberOneBased)
    {
        const auto all = workspaces_.GetWorkspaces();
        SwitchResult result;
        result.monitorStableId = monitorStableId;
        if (numberOneBased < 1 || numberOneBased > static_cast<int>(all.size()))
        {
            result.error = "no workspace with number " + std::to_string(numberOneBased);
            return result;
        }
        return SwitchMonitorToWorkspace(monitorStableId, all[static_cast<size_t>(numberOneBased - 1)].id);
    }

    bool WorkspaceEngine::ApplySwitch(const std::wstring &monitorStableId, const std::string &workspaceId, SwitchResult &result)
    {
        switchInProgress_ = true;
        struct Reset
        {
            bool &flag;
            ~Reset() { flag = false; }
        } reset{switchInProgress_};

        const auto ws = workspaces_.GetWorkspace(workspaceId);
        const auto activeId = workspaces_.GetActiveWorkspaceId(monitorStableId);

        // 1. Discover manageable windows and sync them into the tracker.
        //    Unknown windows get assigned to the ACTIVE workspace of the monitor they are on,
        //    so switching away from that workspace hides them.
        const auto discovered = windows_.DiscoverWindows(platform::WindowDiscoveryOptions{.includeUnmanageableInReport = false});
        workspaces_.SyncDiscoveredWindows(discovered);

        // 2. Drop stale hidden-window entries whose HWNDs are gone (missed DESTROY events).
        for (const auto &tracked : workspaces_.GetTrackedWindows())
        {
            if (tracked.visibilityState == core::WindowVisibilityState::HiddenByFlowSpace && !IsWindow(tracked.hwnd))
            {
                workspaces_.ForgetWindow(tracked.hwnd);
                recovery_.Forget(tracked.hwnd);
            }
        }

        const bool paused = config_.Get().settings.paused;

        // 3. Hide/show windows that live on this monitor. Windows on other monitors are never
        //    touched here — switching one display must not disturb another.
        for (const auto &tracked : workspaces_.GetTrackedWindows())
        {
            if (tracked.monitorStableId != monitorStableId)
            {
                continue;
            }
            if (tracked.hwnd == nullptr)
            {
                continue;
            }

            if (tracked.assignedWorkspaceId == workspaceId)
            {
                // Belongs to the destination workspace: show it (if hidden by us).
                if (tracked.visibilityState == core::WindowVisibilityState::HiddenByFlowSpace)
                {
                    if (paused || !IsWindow(tracked.hwnd))
                    {
                        continue;
                    }
                    auto entry = recovery_.Entries();
                    bool wasMinimized = false;
                    for (const auto &e : entry)
                    {
                        if (e.hwnd == tracked.hwnd)
                        {
                            wasMinimized = e.wasMinimized;
                        }
                    }
                    ShowWindowInternal(tracked.hwnd, wasMinimized);
                    recovery_.RecordShow(tracked.hwnd);
                    ++result.windowsShown;
                }
                continue;
            }

            // Belongs to another workspace: hide it while the monitor shows the destination.
            if (tracked.visibilityState == core::WindowVisibilityState::HiddenByFlowSpace)
            {
                continue; // already hidden
            }
            if (!IsEffectivelyVisible(tracked.visibilityState))
            {
                continue; // cloaked/system-hidden: never ours to manage
            }
            if (tracked.visibilityState == core::WindowVisibilityState::Minimized)
            {
                // Leave minimized windows alone: they occupy no screen area and hiding them
                // would remove their taskbar buttons, increasing the chance of "lost" windows.
                continue;
            }
            if (paused)
            {
                continue;
            }
            HideWindowInternal(tracked, workspaceId);
            ++result.windowsHidden;
        }

        // 4. Update active workspace state (per monitor — other monitors unaffected).
        workspaces_.SetActiveWorkspace(monitorStableId, workspaceId);
        config_.Get().monitorState[monitorStableId] = workspaceId;

        // 5. Wallpaper for this monitor from the destination workspace.
        ApplyWallpaperForMonitor(monitorStableId);

        // 6. Overlay.
        if (OnOverlayShow && ws.has_value())
        {
            OnOverlayShow(*ws, monitorStableId);
        }

        // 7. Persist monitor state (windows themselves are runtime state, never persisted).
        config_.Save();
        if (OnStateChanged)
        {
            OnStateChanged();
        }

        spdlog::info("Switch: monitor '{}' -> '{}' (hidden={}, shown={}, errors={})",
                     Narrow(monitorStableId), workspaceId, result.windowsHidden, result.windowsShown, result.windowErrors);
        return true;
    }

    void WorkspaceEngine::HideWindowInternal(const core::WindowInfo &window, const std::string &workspaceId)
    {
        platform::WindowRestoreState state;
        if (!windows_.CaptureRestoreState(window.hwnd, state))
        {
            spdlog::warn("Switch: window vanished before hide (HWND=0x{:08X})", reinterpret_cast<uintptr_t>(window.hwnd));
            workspaces_.ForgetWindow(window.hwnd);
            return;
        }

        // Journal BEFORE hiding so a crash between the two leaves a restorable record.
        recovery_.RecordHide(window, workspaceId, state.wasMinimized, state.wasMaximized, state.restoreRect);

        core::WindowVisibilityState newState;
        bool ok = false;
        if (config_.Get().settings.hideMode == core::HideMode::Minimize)
        {
            ok = windows_.MinimizeWindowFast(window.hwnd, &newState);
            // Minimize keeps the window visible-but-minimized; it is not journaled as hidden.
            if (ok)
            {
                recovery_.RecordShow(window.hwnd);
            }
        }
        else
        {
            ok = windows_.HideWindowFast(window.hwnd, state, &newState);
        }

        if (!ok)
        {
            // Do not leave a journal entry pointing at a window that never actually hid.
            recovery_.RecordShow(window.hwnd);
            spdlog::warn("Switch: failed to hide HWND=0x{:08X} ('{}')",
                         reinterpret_cast<uintptr_t>(window.hwnd), Narrow(window.title));
            return;
        }
        workspaces_.SetWindowState(window.hwnd, newState);
    }

    void WorkspaceEngine::ShowWindowInternal(HWND hwnd, bool wasMinimized)
    {
        platform::WindowRestoreState state;
        state.wasMinimized = wasMinimized;
        for (const auto &entry : recovery_.Entries())
        {
            if (entry.hwnd == hwnd)
            {
                state.wasMaximized = entry.wasMaximized;
                state.restoreRect = entry.restoreRect;
                break;
            }
        }
        if (!windows_.ShowWindowFast(hwnd, state))
        {
            spdlog::warn("Switch: failed to show HWND=0x{:08X}", reinterpret_cast<uintptr_t>(hwnd));
        }
        else
        {
            workspaces_.SetWindowState(hwnd, state.wasMinimized ? core::WindowVisibilityState::Minimized : (state.wasMaximized ? core::WindowVisibilityState::Maximized : core::WindowVisibilityState::Visible));
        }
    }

    bool WorkspaceEngine::MoveActiveWindowToWorkspace(const std::string &workspaceId, const std::wstring &monitorStableId, const core::WindowInfo *window)
    {
        const auto fg = window ? *window : ResolveMoveWindow();
        if (fg.hwnd == nullptr)
        {
            return false;
        }
        if (!workspaces_.GetWorkspace(workspaceId).has_value())
        {
            return false;
        }

        // The tracker is populated on switches, not at startup. A window can therefore be
        // perfectly manageable but unknown to WorkspaceManager when a move hotkey fires.
        // Sync it first, otherwise AssignWindowToWorkspace() fails silently and the window
        // gets hidden with no workspace assignment to ever restore it from.
        workspaces_.SyncDiscoveredWindows({fg});

        const auto targetMonitor = monitorStableId.empty() ? fg.monitorStableId : monitorStableId;
        if (!workspaces_.AssignWindowToWorkspace(fg.hwnd, workspaceId, targetMonitor))
        {
            spdlog::warn("Move: refused to hide HWND=0x{:08X} ('{}'); window is not tracked", reinterpret_cast<uintptr_t>(fg.hwnd), Narrow(fg.title));
            return false;
        }

        // Assign the window, then hide it: it belongs to the target workspace now, and the
        // current monitor is showing a different one. The application keeps running.
        HideWindowInternal(fg, workspaceId);
        spdlog::info("Move: HWND=0x{:08X} ('{}') -> '{}' on monitor '{}'", reinterpret_cast<uintptr_t>(fg.hwnd), Narrow(fg.title), workspaceId, Narrow(targetMonitor));
        if (OnStateChanged)
        {
            OnStateChanged();
        }
        return true;
    }

    bool WorkspaceEngine::MoveActiveWindowRelative(SwitchDirection direction)
    {
        const auto fg = ResolveMoveWindow();
        if (fg.hwnd == nullptr)
        {
            return false;
        }
        const auto monitorId = ResolveWindowActionMonitor(fg);
        if (monitorId.empty())
        {
            return false;
        }
        const auto target = ResolveAdjacentWorkspace(monitorId, direction);
        if (target.empty())
        {
            spdlog::warn("Move: no adjacent workspace for monitor '{}'", Narrow(monitorId));
            return false;
        }
        return MoveActiveWindowToWorkspace(target, monitorId, &fg);
    }

    bool WorkspaceEngine::MoveActiveWindowByNumber(int numberOneBased)
    {
        const auto all = workspaces_.GetWorkspaces();
        if (numberOneBased < 1 || numberOneBased > static_cast<int>(all.size()))
        {
            return false;
        }
        const auto fg = ResolveMoveWindow();
        if (fg.hwnd == nullptr)
        {
            return false;
        }
        return MoveActiveWindowToWorkspace(all[static_cast<size_t>(numberOneBased - 1)].id, ResolveWindowActionMonitor(fg), &fg);
    }

    void WorkspaceEngine::RevealAllWindows()
    {
        int revealed = 0;
        int cleaned = 0;
        for (const auto &entry : recovery_.Entries())
        {
            if (!IsWindow(entry.hwnd))
            {
                ++cleaned;
                recovery_.Forget(entry.hwnd);
                continue;
            }
            // Validate identity: an HWND can be reused by another process after a crash.
            if (!windows_.IsWindowFromProcess(entry.hwnd, entry.processId, entry.executablePath, entry.processCreationTime))
            {
                spdlog::warn("Reveal: HWND 0x{:08X} identity mismatch; skipping", reinterpret_cast<uintptr_t>(entry.hwnd));
                ++cleaned;
                recovery_.Forget(entry.hwnd);
                continue;
            }
            if (!IsWindowVisible(entry.hwnd))
            {
                platform::WindowRestoreState state;
                state.wasMinimized = entry.wasMinimized;
                state.wasMaximized = entry.wasMaximized;
                state.restoreRect = entry.restoreRect;
                if (windows_.ShowWindowFast(entry.hwnd, state))
                {
                    ++revealed;
                    workspaces_.SetWindowState(entry.hwnd, state.wasMinimized ? core::WindowVisibilityState::Minimized
                                                                              : (state.wasMaximized ? core::WindowVisibilityState::Maximized : core::WindowVisibilityState::Visible));
                }
            }
            recovery_.Forget(entry.hwnd);
        }
        spdlog::info("Reveal all: {} revealed, {} stale entries cleaned", revealed, cleaned);
        if (OnStateChanged)
        {
            OnStateChanged();
        }
    }

    void WorkspaceEngine::HandleDisplayChange()
    {
        const auto detected = monitors_.EnumerateMonitors();
        workspaces_.ReconcileMonitors(detected);

        auto &config = config_.Get();
        const std::string defaultWs = workspaces_.GetWorkspaces().empty()
                                          ? std::string{}
                                          : workspaces_.GetWorkspaces().front().id;

        for (const auto &mon : detected)
        {
            auto it = config.monitorState.find(mon.stableId);
            if (it != config.monitorState.end())
            {
                workspaces_.SetActiveWorkspace(mon.stableId, it->second);
            }
            else if (!defaultWs.empty())
            {
                workspaces_.SetActiveWorkspace(mon.stableId, defaultWs);
            }
        }

        // Windows that were on a now-disconnected monitor: Windows itself relocates their
        // geometry; we refresh each tracked window's monitor id on the next enumeration.
        // Persisted workspace configuration is intentionally preserved for reconnects.
        config_.Save();
        if (OnStateChanged)
        {
            OnStateChanged();
        }
        spdlog::info("Display change: {} monitor(s) detected", detected.size());
    }

    bool WorkspaceEngine::MatchesRule(const core::WindowInfo &window, const core::AppRule &rule) const
    {
        if (!rule.enabled)
        {
            return false;
        }
        if (!rule.exeName.empty() && _wcsicmp(window.executableName.c_str(), rule.exeName.c_str()) != 0)
        {
            return false;
        }
        if (!rule.exePath.empty() && _wcsicmp(window.executablePath.c_str(), rule.exePath.c_str()) != 0)
        {
            return false;
        }
        if (!rule.titleContains.empty() && window.title.find(rule.titleContains) == std::wstring::npos)
        {
            return false;
        }
        return true;
    }

    std::string WorkspaceEngine::ResolveAssignmentForNewWindow(const core::WindowInfo &window, const core::AppRule **matchedRule) const
    {
        if (matchedRule)
        {
            *matchedRule = nullptr;
        }
        const auto &config = config_.Get();

        // 1. Explicit application rules (first enabled match wins).
        for (const auto &rule : config.appRules)
        {
            if (MatchesRule(window, rule))
            {
                if (matchedRule)
                {
                    *matchedRule = &rule;
                }
                return rule.targetWorkspaceId;
            }
        }

        // 2. Group by application: inherit the assignment of existing windows of the same exe.
        if (config.settings.groupWindowsByApplication && !window.executableName.empty())
        {
            for (const auto &tracked : workspaces_.GetTrackedWindows())
            {
                if (tracked.hwnd != window.hwnd && !tracked.assignedWorkspaceId.empty() && _wcsicmp(tracked.executableName.c_str(), window.executableName.c_str()) == 0)
                {
                    return tracked.assignedWorkspaceId;
                }
            }
        }

        // 3. Fallback: the active workspace of the monitor the window appeared on.
        if (auto active = workspaces_.GetActiveWorkspaceId(window.monitorStableId))
        {
            return *active;
        }
        const auto all = workspaces_.GetWorkspaces();
        return all.empty() ? std::string{} : all.front().id;
    }

    void WorkspaceEngine::ProcessNewWindow(HWND hwnd)
    {
        const auto discovered = windows_.DiscoverWindows(platform::WindowDiscoveryOptions{.includeUnmanageableInReport = false});
        // Track first; AssignWindowToWorkspace() is a no-op for unknown HWNDs.
        workspaces_.SyncDiscoveredWindows(discovered);
        for (const auto &window : discovered)
        {
            if (window.hwnd != hwnd)
            {
                continue;
            }
            const core::AppRule *rule = nullptr;
            const std::string assignment = ResolveAssignmentForNewWindow(window, &rule);
            if (assignment.empty())
            {
                return;
            }
            workspaces_.AssignWindowToWorkspace(hwnd, assignment, window.monitorStableId);

            // Monitor preference: relocate the window when the rule asks for it.
            if (rule != nullptr && rule->monitorPreference == core::MonitorPreference::Specific && !rule->monitorStableId.empty() && rule->monitorStableId != window.monitorStableId)
            {
                windows_.MoveWindowToMonitor(hwnd, rule->monitorStableId);
            }
            else if (rule != nullptr && rule->monitorPreference == core::MonitorPreference::FollowWorkspace)
            {
                // Move to the first monitor whose active workspace is the target workspace.
                for (const auto &mon : workspaces_.GetMonitors())
                {
                    if (mon.activeWorkspaceId == assignment && mon.stableId != window.monitorStableId)
                    {
                        windows_.MoveWindowToMonitor(hwnd, mon.stableId);
                        break;
                    }
                }
            }
            return;
        }
    }

    void WorkspaceEngine::ProcessShownWindow(HWND hwnd)
    {
        if (switchInProgress_)
        {
            return; // switches manage visibility themselves
        }
        const auto discovered = windows_.DiscoverWindows(platform::WindowDiscoveryOptions{.includeUnmanageableInReport = false});
        for (const auto &window : discovered)
        {
            if (window.hwnd != hwnd)
            {
                continue;
            }
            const auto active = workspaces_.GetActiveWorkspaceId(window.monitorStableId);
            const auto tracked = workspaces_.GetWindow(hwnd);
            const std::string assignment = tracked.has_value() ? tracked->assignedWorkspaceId : std::string{};

            if (!active.has_value() || assignment.empty() || assignment == *active)
            {
                return; // not ours to hide
            }
            if (window.visibilityState == core::WindowVisibilityState::Minimized)
            {
                return; // leave minimized windows alone
            }
            // The window became visible while its workspace is not the monitor's active
            // workspace (e.g. the app restored itself, or the user clicked the taskbar).
            HideWindowInternal(window, assignment);
            return;
        }
    }

    void WorkspaceEngine::ProcessMovedWindow(HWND hwnd)
    {
        const auto discovered = windows_.DiscoverWindows(platform::WindowDiscoveryOptions{.includeUnmanageableInReport = false});
        for (const auto &window : discovered)
        {
            if (window.hwnd != hwnd)
            {
                continue;
            }
            const auto tracked = workspaces_.GetWindow(hwnd);
            if (!tracked.has_value())
            {
                return;
            }
            if (tracked->monitorStableId == window.monitorStableId)
            {
                return; // monitor unchanged
            }
            // The user dragged the window to another monitor: keep it visible and assign it to
            // that monitor's active workspace. This matches the intent of a manual move.
            const auto active = workspaces_.GetActiveWorkspaceId(window.monitorStableId);
            if (active.has_value())
            {
                workspaces_.AssignWindowToWorkspace(hwnd, *active, window.monitorStableId);
            }
            if (OnStateChanged)
            {
                OnStateChanged();
            }
            return;
        }
    }

    void WorkspaceEngine::HandleWindowEvent(WindowEventType type, HWND hwnd)
    {
        if (!messageWindow_ || hwnd == nullptr)
        {
            return;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId())
        {
            return; // never manage FlowSpace's own windows (overlays, tray, main window)
        }
        if (!windows_.IsManageableWindow(hwnd))
        {
            return;
        }

        switch (type)
        {
        case WindowEventType::Created:
            ProcessNewWindow(hwnd);
            break;
        case WindowEventType::Shown:
            ProcessShownWindow(hwnd);
            break;
        case WindowEventType::Destroyed:
            workspaces_.ForgetWindow(hwnd);
            recovery_.Forget(hwnd);
            if (OnStateChanged)
            {
                OnStateChanged();
            }
            break;
        case WindowEventType::MoveSizeEnd:
            ProcessMovedWindow(hwnd);
            break;
        case WindowEventType::Hidden:
        case WindowEventType::Foreground:
            // Nothing to do: hidden windows remain hidden; foreground changes don't affect
            // workspace assignments.
            break;
        }
    }

    bool WorkspaceEngine::HandleHotkey(int hotkeyId)
    {
        const auto resolved = hotkeys_.Resolve(hotkeyId);
        if (!resolved.has_value())
        {
            return false;
        }
        const auto [action, number] = *resolved;
        const std::wstring monitor = ResolveActionMonitor();
        if (monitor.empty())
        {
            return true;
        }

        switch (action)
        {
        case core::HotkeyAction::PreviousWorkspace:
            SwitchRelative(monitor, SwitchDirection::Previous);
            break;
        case core::HotkeyAction::NextWorkspace:
            SwitchRelative(monitor, SwitchDirection::Next);
            break;
        case core::HotkeyAction::SwitchToWorkspaceByNumber:
            SwitchByNumber(monitor, number);
            break;
        case core::HotkeyAction::MoveActiveWindowToPreviousWorkspace:
            MoveActiveWindowRelative(SwitchDirection::Previous);
            break;
        case core::HotkeyAction::MoveActiveWindowToNextWorkspace:
            MoveActiveWindowRelative(SwitchDirection::Next);
            break;
        case core::HotkeyAction::MoveActiveWindowToWorkspaceByNumber:
            MoveActiveWindowByNumber(number);
            break;
        case core::HotkeyAction::RevealAllWindows:
            RevealAllWindows();
            break;
        case core::HotkeyAction::OpenWorkspaceSwitcher:
        case core::HotkeyAction::OpenSettings:
            // The app registers its own handlers for these UI actions.
            return false;
        }
        return true;
    }

    // --- UI facade ---------------------------------------------------------------

    std::vector<core::Workspace> WorkspaceEngine::GetWorkspaces() const
    {
        return workspaces_.GetWorkspaces();
    }

    core::Workspace &WorkspaceEngine::WorkspaceAt(size_t index)
    {
        auto all = workspaces_.GetWorkspaces();
        static core::Workspace fallback; // returned only on programming errors
        if (index >= all.size())
        {
            return fallback;
        }
        const std::string id = all[index].id;
        auto *mutableList = workspaces_.MutableWorkspaces();
        for (auto &ws : *mutableList)
        {
            if (ws.id == id)
            {
                return ws;
            }
        }
        return fallback;
    }

    std::vector<core::MonitorInfo> WorkspaceEngine::GetMonitors() const
    {
        // Enumerate currently connected monitors, then merge in the active workspace
        // assignment tracked by WorkspaceManager (monitors_.EnumerateMonitors() leaves
        // activeWorkspaceId empty).
        const auto detected = monitors_.EnumerateMonitors();
        std::vector<core::MonitorInfo> result;
        result.reserve(detected.size());
        for (const auto &mon : detected)
        {
            if (auto info = workspaces_.GetMonitor(mon.stableId))
            {
                result.push_back(*info);
            }
            else
            {
                result.push_back(mon);
            }
        }
        return result;
    }

    core::WindowInfo WorkspaceEngine::GetForegroundWindowInfo() const
    {
        return windows_.GetForegroundWindow();
    }

    std::string WorkspaceEngine::CreateWorkspace(const std::string &name)
    {
        auto created = workspaces_.CreateWorkspace(name.empty() ? "Workspace" : name);
        if (OnStateChanged)
        {
            OnStateChanged();
        }
        return created.id;
    }

    void WorkspaceEngine::RenameWorkspaceIndex(int index, const std::string &name)
    {
        const auto all = workspaces_.GetWorkspaces();
        if (index < 0 || index >= static_cast<int>(all.size()))
        {
            return;
        }
        workspaces_.RenameWorkspace(all[index].id, name);
        if (OnStateChanged)
        {
            OnStateChanged();
        }
    }

    void WorkspaceEngine::DuplicateWorkspaceIndex(int index)
    {
        const auto all = workspaces_.GetWorkspaces();
        if (index < 0 || index >= static_cast<int>(all.size()))
        {
            return;
        }
        workspaces_.DuplicateWorkspace(all[index].id);
        if (OnStateChanged)
        {
            OnStateChanged();
        }
    }

    void WorkspaceEngine::ResetWorkspaceIndex(int index)
    {
        const auto all = workspaces_.GetWorkspaces();
        if (index < 0 || index >= static_cast<int>(all.size()))
        {
            return;
        }
        workspaces_.ResetWorkspace(all[index].id);
        if (OnStateChanged)
        {
            OnStateChanged();
        }
    }

    void WorkspaceEngine::DeleteWorkspaceIndex(int index)
    {
        const auto all = workspaces_.GetWorkspaces();
        if (index < 0 || index >= static_cast<int>(all.size()) || all.size() <= 1)
        {
            return; // never delete the last workspace
        }
        const std::string deletedId = all[index].id;
        std::string fallback;
        for (const auto &ws : all)
        {
            if (ws.id != deletedId)
            {
                fallback = ws.id;
                break;
            }
        }
        workspaces_.DeleteWorkspace(deletedId, fallback);
        if (OnStateChanged)
        {
            OnStateChanged();
        }
    }

    void WorkspaceEngine::SetWorkspaceWallpaperIndex(int index, const std::wstring &imagePath)
    {
        auto &ws = WorkspaceAt(static_cast<size_t>(index));
        if (!ws.id.empty())
        {
            ws.defaultWallpaperPath = imagePath;
            if (OnStateChanged)
            {
                OnStateChanged();
            }
        }
    }

    void WorkspaceEngine::SwitchMonitorToWorkspaceIndex(int monitorIndex, int workspaceIndex)
    {
        const auto monitors = monitors_.EnumerateMonitors();
        const auto workspaces = workspaces_.GetWorkspaces();
        if (monitorIndex < 0 || monitorIndex >= static_cast<int>(monitors.size()))
        {
            return;
        }
        if (workspaceIndex < 0 || workspaceIndex >= static_cast<int>(workspaces.size()))
        {
            return;
        }
        SwitchMonitorToWorkspace(monitors[monitorIndex].stableId, workspaces[workspaceIndex].id);
    }

    void WorkspaceEngine::RevealSingleWindow(HWND hwnd)
    {
        const auto entries = recovery_.Entries();
        for (const auto &entry : entries)
        {
            if (entry.hwnd != hwnd)
            {
                continue;
            }
            if (!IsWindow(hwnd) || !windows_.IsWindowFromProcess(hwnd, entry.processId, entry.executablePath, entry.processCreationTime))
            {
                recovery_.Forget(hwnd);
                return;
            }
            platform::WindowRestoreState state;
            state.wasMinimized = entry.wasMinimized;
            state.wasMaximized = entry.wasMaximized;
            state.restoreRect = entry.restoreRect;
            if (windows_.ShowWindowFast(hwnd, state))
            {
                workspaces_.SetWindowState(hwnd, state.wasMinimized ? core::WindowVisibilityState::Minimized
                                                                    : (state.wasMaximized ? core::WindowVisibilityState::Maximized : core::WindowVisibilityState::Visible));
            }
            recovery_.RecordShow(hwnd);
            if (OnStateChanged)
            {
                OnStateChanged();
            }
            return;
        }
    }

    void WorkspaceEngine::RestoreOriginalWallpaperForMonitorIndex(int monitorIndex)
    {
        const auto monitors = monitors_.EnumerateMonitors();
        if (monitorIndex < 0 || monitorIndex >= static_cast<int>(monitors.size()) || !wallpaper_.IsAvailable())
        {
            return;
        }
        const std::wstring &stableId = monitors[monitorIndex].stableId;
        for (const auto &w : config_.Get().originalWallpapers)
        {
            if (w.monitorDevicePath == stableId)
            {
                wallpaper_.SetWallpaper(stableId, w.imagePath);
                (void)wallpaper_.SetBackgroundColor(static_cast<COLORREF>(w.backgroundColor));
                (void)wallpaper_.SetPosition(static_cast<platform::WallpaperPosition>(w.fit));
                spdlog::info("Restored original wallpaper for monitor index {}", monitorIndex);
                return;
            }
        }
    }

    void WorkspaceEngine::ApplyWallpaperForMonitor(const std::wstring &monitorStableId)
    {
        if (!config_.Get().settings.wallpaperManagementEnabled || config_.Get().settings.paused)
        {
            return;
        }
        if (!wallpaper_.IsAvailable() || monitorStableId.empty())
        {
            return;
        }
        const auto active = workspaces_.GetActiveWorkspaceId(monitorStableId);
        if (!active.has_value())
        {
            return;
        }
        const auto ws = workspaces_.GetWorkspace(*active);
        if (!ws.has_value())
        {
            return;
        }

        // Per-monitor override wins over the workspace default.
        std::wstring imagePath = ws->defaultWallpaperPath;
        int32_t fit = ws->wallpaperFit;
        if (auto it = ws->perMonitorWallpaper.find(monitorStableId); it != ws->perMonitorWallpaper.end())
        {
            imagePath = it->second;
            if (auto fitIt = ws->perMonitorWallpaperFit.find(monitorStableId); fitIt != ws->perMonitorWallpaperFit.end())
            {
                fit = fitIt->second;
            }
        }

        if (imagePath.empty())
        {
            return; // no wallpaper configured for this workspace: keep the current desktop
        }
        if (!fs::exists(fs::path(imagePath)))
        {
            spdlog::warn("Wallpaper file missing: {}", Narrow(imagePath));
            return;
        }

        // Note: IDesktopWallpaper::SetPosition is global (affects every monitor).
        // It is intentionally NOT called here so that applying one monitor's
        // wallpaper does not change another monitor's position. Position is a
        // per-desktop setting, not per-monitor.
        if (wallpaper_.SetWallpaper(monitorStableId, imagePath))
        {
            spdlog::info("Wallpaper applied: monitor '{}' -> '{}' (fit={})",
                         Narrow(monitorStableId), Narrow(imagePath), fit);
        }
    }
}
