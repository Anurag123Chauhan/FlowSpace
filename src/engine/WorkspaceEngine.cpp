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
            return state == core::WindowVisibilityState::Visible || state == core::WindowVisibilityState::Maximized ||
                   state == core::WindowVisibilityState::Minimized;
        }

        std::string Narrow(const std::wstring &w)
        {
            return core::WideToUtf8(w);
        }
    }

    WorkspaceEngine::WorkspaceEngine(platform::MonitorManager &monitors, platform::WindowManager &windows,
                                     core::WorkspaceManager &workspaces, core::ConfigService &config,
                                     core::RecoveryManager &recovery, platform::HotkeyManager &hotkeys)
        : monitors_(monitors), windows_(windows), workspaces_(workspaces), config_(config), recovery_(recovery),
          hotkeys_(hotkeys)
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
        // Restore the persisted per-monitor workspace lists before reconciling display state.
        if (!config_.Get().workspaces.empty())
        {
            workspaces_.LoadWorkspaces(config_.Get().workspaces);
        }

        const auto detected = monitors_.EnumerateMonitors();
        workspaces_.ReconcileMonitors(detected);

        auto &config = config_.Get();

        // Apply persisted per-monitor active workspace state. ReconcileMonitors already gives
        // every monitor a usable default, so a stale/cross-monitor reference simply falls back.
        for (const auto &mon : detected)
        {
            auto it = config.monitorState.find(mon.stableId);
            if (it != config.monitorState.end())
            {
                workspaces_.SetActiveWorkspace(mon.stableId, it->second);
            }
            if (auto active = workspaces_.GetActiveWorkspaceId(mon.stableId); active.has_value())
            {
                config.monitorState[mon.stableId] = *active;
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
            if (!windows_.IsWindowFromProcess(entry.hwnd, entry.processId, entry.executablePath,
                                              entry.processCreationTime))
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

        ApplyHotkeys();
        ApplyRulesToOpenWindows();
        PersistWorkspacesToConfig();
        config_.Save();
    }

    void WorkspaceEngine::ApplyRulesToOpenWindows()
    {
        const auto &config = config_.Get();
        if (config.appRules.empty())
        {
            return;
        }

        // Discover all currently manageable windows and sync them into the tracker.
        const auto discovered =
            windows_.DiscoverWindows(platform::WindowDiscoveryOptions{.includeUnmanageableInReport = false});
        workspaces_.SyncDiscoveredWindows(discovered);

        const auto tracked = workspaces_.GetTrackedWindows();
        if (tracked.empty())
        {
            return;
        }

        int moved = 0;
        for (const auto &win : tracked)
        {
            if (!win.isManageable || win.hwnd == nullptr || !IsWindow(win.hwnd))
            {
                continue;
            }

            const core::AppRule *matchedRule = nullptr;
            const std::string assignment = ResolveAssignmentForNewWindow(win, &matchedRule);
            if (assignment.empty() || matchedRule == nullptr)
            {
                continue;
            }

            if (win.assignedWorkspaceId == assignment)
            {
                continue;
            }

            const auto targetWs = workspaces_.GetWorkspace(assignment);
            if (!targetWs.has_value())
            {
                continue;
            }

            const std::wstring targetMonitor =
                targetWs->monitorStableId.empty() ? win.monitorStableId : targetWs->monitorStableId;

            // If the rule has a Specific monitor preference, use that monitor.
            std::wstring finalMonitor = targetMonitor;
            if (matchedRule->monitorPreference == core::MonitorPreference::Specific &&
                !matchedRule->monitorStableId.empty())
            {
                finalMonitor = matchedRule->monitorStableId;
            }

            // If the window is already on the target workspace on the correct monitor, skip.
            if (win.monitorStableId == finalMonitor && win.assignedWorkspaceId == assignment)
            {
                continue;
            }

            // Move the window to the target monitor if needed.
            if (finalMonitor != win.monitorStableId)
            {
                spdlog::info("Rule startup: moving HWND=0x{:08X} ('{}') to monitor '{}'",
                             reinterpret_cast<uintptr_t>(win.hwnd), std::string(win.title.begin(), win.title.end()),
                             Narrow(finalMonitor));
                windows_.MoveWindowToMonitor(win.hwnd, finalMonitor);
            }

            // Assign the window to the target workspace.
            workspaces_.AssignWindowToWorkspace(win.hwnd, assignment, finalMonitor);

            // Switch the target monitor to the target workspace so the window becomes visible.
            SwitchMonitorToWorkspace(finalMonitor, assignment);

            ++moved;
        }

        if (moved > 0)
        {
            spdlog::info("Applied app rules to {} open window(s) at startup", moved);
            PersistWorkspacesToConfig();
            config_.Save();
            if (OnStateChanged)
            {
                OnStateChanged();
            }
        }
    }

    void WorkspaceEngine::Shutdown()
    {
        hotkeys_.UnregisterAll();
        RevealAllWindows();
        PersistWorkspacesToConfig();
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

    std::string WorkspaceEngine::ResolveAdjacentWorkspace(const std::wstring &monitorStableId,
                                                          SwitchDirection direction) const
    {
        const auto resolved = workspaces_.ResolveAdjacentWorkspaceId(
            monitorStableId, direction == SwitchDirection::Next ? core::WorkspaceSwitchDirection::Next
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
            return std::any_of(monitors.begin(), monitors.end(),
                               [&](const core::MonitorInfo &mon) { return mon.stableId == stableId; });
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
                spdlog::debug("Move: window monitor '{}' unavailable; using cursor monitor '{}'",
                              Narrow(window.monitorStableId), Narrow(*cursorId));
                return *cursorId;
            }
        }

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
            spdlog::debug("Move: using window under cursor (HWND=0x{:08X}, '{}')",
                          reinterpret_cast<uintptr_t>(underCursor.hwnd), Narrow(underCursor.title));
            return underCursor;
        }
        return windows_.GetForegroundWindow();
    }

    SwitchResult WorkspaceEngine::SwitchMonitorToWorkspace(const std::wstring &monitorStableId,
                                                           const std::string &workspaceId)
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
        if (!ws->monitorStableId.empty() && ws->monitorStableId != monitorStableId)
        {
            result.error = "workspace belongs to another monitor";
            spdlog::warn("Switch rejected: workspace '{}' does not belong to monitor '{}'", workspaceId,
                         Narrow(monitorStableId));
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
        const auto all = workspaces_.GetWorkspacesForMonitor(monitorStableId);
        SwitchResult result;
        result.monitorStableId = monitorStableId;
        if (numberOneBased < 1 || numberOneBased > static_cast<int>(all.size()))
        {
            result.error = "no workspace with number " + std::to_string(numberOneBased);
            return result;
        }
        return SwitchMonitorToWorkspace(monitorStableId, all[static_cast<size_t>(numberOneBased - 1)].id);
    }

    bool WorkspaceEngine::ApplySwitch(const std::wstring &monitorStableId, const std::string &workspaceId,
                                      SwitchResult &result)
    {
        switchInProgress_ = true;
        struct Reset
        {
            bool &flag;
            ~Reset() { flag = false; }
        } reset{switchInProgress_};

        const auto ws = workspaces_.GetWorkspace(workspaceId);
        const auto activeId = workspaces_.GetActiveWorkspaceId(monitorStableId);
        (void)activeId;

        // Discover windows and assign untracked ones to their monitor's active workspace.
        const auto discovered =
            windows_.DiscoverWindows(platform::WindowDiscoveryOptions{.includeUnmanageableInReport = false});
        workspaces_.SyncDiscoveredWindows(discovered);

        // Remove tracker entries for windows that no longer exist.
        for (const auto &tracked : workspaces_.GetTrackedWindows())
        {
            if (tracked.visibilityState == core::WindowVisibilityState::HiddenByFlowSpace && !IsWindow(tracked.hwnd))
            {
                workspaces_.ForgetWindow(tracked.hwnd);
                recovery_.Forget(tracked.hwnd);
            }
        }

        const bool paused = config_.Get().settings.paused;

        // Only change windows on this monitor; other displays remain untouched.
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

            if (tracked.visibilityState == core::WindowVisibilityState::HiddenByFlowSpace)
            {
                continue;
            }
            if (!IsEffectivelyVisible(tracked.visibilityState))
            {
                continue;
            }
            if (tracked.visibilityState == core::WindowVisibilityState::Minimized)
            {
                // Keep minimized windows visible in the taskbar so users can still find them.
                continue;
            }
            if (paused)
            {
                continue;
            }
            HideWindowInternal(tracked, workspaceId);
            ++result.windowsHidden;
        }

        // Save the new active workspace for this monitor.
        workspaces_.SetActiveWorkspace(monitorStableId, workspaceId);
        config_.Get().monitorState[monitorStableId] = workspaceId;

        if (OnOverlayShow && ws.has_value())
        {
            OnOverlayShow(*ws, monitorStableId);
        }

        // Save workspace settings; window visibility is runtime state.
        PersistWorkspacesToConfig();
        config_.Save();
        if (OnStateChanged)
        {
            OnStateChanged();
        }

        spdlog::info("Switch: monitor '{}' -> '{}' (hidden={}, shown={}, errors={})", Narrow(monitorStableId),
                     workspaceId, result.windowsHidden, result.windowsShown, result.windowErrors);
        return true;
    }

    void WorkspaceEngine::HideWindowInternal(const core::WindowInfo &window, const std::string &workspaceId)
    {
        platform::WindowRestoreState state;
        if (!windows_.CaptureRestoreState(window.hwnd, state))
        {
            spdlog::warn("Switch: window vanished before hide (HWND=0x{:08X})",
                         reinterpret_cast<uintptr_t>(window.hwnd));
            workspaces_.ForgetWindow(window.hwnd);
            return;
        }

        // Save the recovery record before hiding, so a crash cannot lose the window.
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
            spdlog::warn("Switch: failed to hide HWND=0x{:08X} ('{}')", reinterpret_cast<uintptr_t>(window.hwnd),
                         Narrow(window.title));
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
            workspaces_.SetWindowState(hwnd, state.wasMinimized
                                                 ? core::WindowVisibilityState::Minimized
                                                 : (state.wasMaximized ? core::WindowVisibilityState::Maximized
                                                                       : core::WindowVisibilityState::Visible));
        }
    }

    bool WorkspaceEngine::MoveActiveWindowToWorkspace(const std::string &workspaceId,
                                                      const std::wstring &monitorStableId,
                                                      const core::WindowInfo *window)
    {
        const auto fg = window ? *window : ResolveMoveWindow();
        if (fg.hwnd == nullptr)
        {
            return false;
        }
        const auto targetWorkspace = workspaces_.GetWorkspace(workspaceId);
        if (!targetWorkspace.has_value())
        {
            return false;
        }

        // Add this window to the tracker before assigning it to a workspace.
        // Otherwise a move could hide a window that FlowSpace cannot restore.
        workspaces_.SyncDiscoveredWindows({fg});

        const auto targetMonitor = monitorStableId.empty() ? fg.monitorStableId : monitorStableId;
        if (!targetWorkspace->monitorStableId.empty() && targetWorkspace->monitorStableId != targetMonitor)
        {
            spdlog::warn("Move: workspace '{}' belongs to monitor '{}', not '{}'", workspaceId,
                         Narrow(targetWorkspace->monitorStableId), Narrow(targetMonitor));
            return false;
        }
        if (!workspaces_.AssignWindowToWorkspace(fg.hwnd, workspaceId, targetMonitor))
        {
            spdlog::warn("Move: refused to hide HWND=0x{:08X} ('{}'); window is not tracked",
                         reinterpret_cast<uintptr_t>(fg.hwnd), Narrow(fg.title));
            return false;
        }

        // Assign the window, then hide it: it belongs to the target workspace now, and the
        // current monitor is showing a different one. The application keeps running.
        HideWindowInternal(fg, workspaceId);
        spdlog::info("Move: HWND=0x{:08X} ('{}') -> '{}' on monitor '{}'", reinterpret_cast<uintptr_t>(fg.hwnd),
                     Narrow(fg.title), workspaceId, Narrow(targetMonitor));
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
        const auto all = workspaces_.GetWorkspacesForMonitor(monitorId);
        if (numberOneBased < 1 || numberOneBased > static_cast<int>(all.size()))
        {
            return false;
        }
        return MoveActiveWindowToWorkspace(all[static_cast<size_t>(numberOneBased - 1)].id, monitorId, &fg);
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
            if (!windows_.IsWindowFromProcess(entry.hwnd, entry.processId, entry.executablePath,
                                              entry.processCreationTime))
            {
                spdlog::warn("Reveal: HWND 0x{:08X} identity mismatch; skipping",
                             reinterpret_cast<uintptr_t>(entry.hwnd));
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
                    workspaces_.SetWindowState(
                        entry.hwnd, state.wasMinimized ? core::WindowVisibilityState::Minimized
                                                       : (state.wasMaximized ? core::WindowVisibilityState::Maximized
                                                                             : core::WindowVisibilityState::Visible));
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
        for (const auto &mon : detected)
        {
            auto it = config.monitorState.find(mon.stableId);
            if (it != config.monitorState.end())
            {
                workspaces_.SetActiveWorkspace(mon.stableId, it->second);
            }
            // ReconcileMonitors guarantees a valid default for new monitors.
            if (auto active = workspaces_.GetActiveWorkspaceId(mon.stableId); active.has_value())
            {
                config.monitorState[mon.stableId] = *active;
            }
        }

        // Windows that were on a now-disconnected monitor: Windows itself relocates their
        // geometry; we refresh each tracked window's monitor id on the next enumeration.
        // Persisted workspace configuration is intentionally preserved for reconnects.
        PersistWorkspacesToConfig();
        config_.Save();
        if (OnStateChanged)
        {
            OnStateChanged();
        }
        spdlog::info("Display change: {} monitor(s) detected", detected.size());
    }

    bool WorkspaceEngine::MatchesRule(const core::WindowInfo &window, const core::AppRule &rule) const
    {
        // A rule with no match criteria (empty exeName, exePath, titleContains) would match
        // every window, so reject it outright.
        if (rule.exeName.empty() && rule.exePath.empty() && rule.titleContains.empty())
        {
            return false;
        }
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
        // A Specific monitor preference rule only matches windows on that monitor.
        if (rule.monitorPreference == core::MonitorPreference::Specific && !rule.monitorStableId.empty())
        {
            if (window.monitorStableId != rule.monitorStableId)
            {
                return false;
            }
        }
        return true;
    }

    std::string WorkspaceEngine::ResolveAssignmentForNewWindow(const core::WindowInfo &window,
                                                               const core::AppRule **matchedRule) const
    {
        if (matchedRule)
        {
            *matchedRule = nullptr;
        }
        const auto &config = config_.Get();
        const std::wstring &monitorId = window.monitorStableId;

        // A workspace is only a valid target for a window when it belongs to the monitor the
        // window appeared on; otherwise the window would be hidden on a display that can never
        // show it. Rules/group inheritance that point elsewhere fall through to the monitor.
        auto validTarget = [&](const std::string &workspaceId)
        {
            if (workspaceId.empty())
            {
                return false;
            }
            const auto ws = workspaces_.GetWorkspace(workspaceId);
            return ws.has_value() && (ws->monitorStableId.empty() || ws->monitorStableId == monitorId);
        };

        // 1. Explicit application rules (first enabled match wins).
        for (const auto &rule : config.appRules)
        {
            if (MatchesRule(window, rule) && validTarget(rule.targetWorkspaceId))
            {
                if (matchedRule)
                {
                    *matchedRule = &rule;
                }
                return rule.targetWorkspaceId;
            }
        }

        // 2. Group by application: inherit the assignment of existing windows of the same exe
        //    on the same monitor.
        if (config.settings.groupWindowsByApplication && !window.executableName.empty())
        {
            for (const auto &tracked : workspaces_.GetTrackedWindows())
            {
                if (tracked.hwnd != window.hwnd && tracked.monitorStableId == monitorId &&
                    _wcsicmp(tracked.executableName.c_str(), window.executableName.c_str()) == 0 &&
                    validTarget(tracked.assignedWorkspaceId))
                {
                    return tracked.assignedWorkspaceId;
                }
            }
        }

        // 3. Fallback: the active workspace of the monitor the window appeared on.
        if (auto active = workspaces_.GetActiveWorkspaceId(monitorId))
        {
            if (validTarget(*active))
            {
                return *active;
            }
        }
        const auto monitorWorkspaces = workspaces_.GetWorkspacesForMonitor(monitorId);
        if (!monitorWorkspaces.empty())
        {
            return monitorWorkspaces.front().id;
        }
        const auto all = workspaces_.GetWorkspaces();
        return all.empty() ? std::string{} : all.front().id;
    }

    void WorkspaceEngine::ProcessNewWindow(HWND hwnd)
    {
        const auto discovered =
            windows_.DiscoverWindows(platform::WindowDiscoveryOptions{.includeUnmanageableInReport = false});
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
            if (rule != nullptr && rule->monitorPreference == core::MonitorPreference::Specific &&
                !rule->monitorStableId.empty() && rule->monitorStableId != window.monitorStableId)
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
        const auto discovered =
            windows_.DiscoverWindows(platform::WindowDiscoveryOptions{.includeUnmanageableInReport = false});
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
        const auto discovered =
            windows_.DiscoverWindows(platform::WindowDiscoveryOptions{.includeUnmanageableInReport = false});
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
                // Nothing to do: hidden windows remain hidden.
                break;
            case WindowEventType::Foreground:
                // Track the last focused manageable window so "+ Add rule" can capture it
                // even though clicking the button makes FlowSpace Settings the foreground window.
                {
                    const auto discovered = windows_.DiscoverWindows(
                        platform::WindowDiscoveryOptions{.includeUnmanageableInReport = false});
                    for (const auto &win : discovered)
                    {
                        if (win.hwnd == hwnd)
                        {
                            lastFocusedWindowInfo_ = win;
                            break;
                        }
                    }
                }
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

    std::vector<core::Workspace> WorkspaceEngine::GetWorkspaces() const
    {
        return workspaces_.GetWorkspaces();
    }

    std::vector<core::Workspace> WorkspaceEngine::GetWorkspacesForMonitor(const std::wstring &monitorStableId) const
    {
        return workspaces_.GetWorkspacesForMonitor(monitorStableId);
    }

    void WorkspaceEngine::PersistWorkspacesToConfig()
    {
        config_.Get().workspaces = workspaces_.GetWorkspaces();
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

    std::optional<core::WindowInfo> WorkspaceEngine::GetLastFocusedWindowInfo() const
    {
        return lastFocusedWindowInfo_;
    }

    std::optional<std::string> WorkspaceEngine::GetActiveWorkspaceId(const std::wstring &monitorStableId) const
    {
        return workspaces_.GetActiveWorkspaceId(monitorStableId);
    }

    void WorkspaceEngine::CaptureForegroundWindowInfo()
    {
        lastFocusedWindowInfo_ = windows_.GetForegroundWindow();
        spdlog::info(
            "Captured foreground for rule: hwnd=0x{:08X} exe='{}'",
            reinterpret_cast<uintptr_t>(lastFocusedWindowInfo_->hwnd),
            std::string(lastFocusedWindowInfo_->executableName.begin(), lastFocusedWindowInfo_->executableName.end()));
    }

    std::string WorkspaceEngine::CreateWorkspace(const std::string &name, const std::wstring &monitorStableId)
    {
        auto created = workspaces_.CreateWorkspace(name.empty() ? "Workspace" : name, monitorStableId);
        if (OnStateChanged)
        {
            OnStateChanged();
        }
        PersistWorkspacesToConfig();
        config_.Save();
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
        PersistWorkspacesToConfig();
        config_.Save();
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
        PersistWorkspacesToConfig();
        config_.Save();
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
        PersistWorkspacesToConfig();
        config_.Save();
    }

    void WorkspaceEngine::DeleteWorkspaceIndex(int index)
    {
        const auto all = workspaces_.GetWorkspaces();
        if (index < 0 || index >= static_cast<int>(all.size()))
        {
            return;
        }
        const std::string deletedId = all[static_cast<size_t>(index)].id;
        const std::wstring ownerMonitor = all[static_cast<size_t>(index)].monitorStableId;
        const auto siblings = workspaces_.GetWorkspacesForMonitor(ownerMonitor);
        if (siblings.size() <= 1)
        {
            return; // never delete a monitor's last workspace
        }
        std::string fallback;
        for (const auto &ws : siblings)
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
        PersistWorkspacesToConfig();
        config_.Save();
    }

    void WorkspaceEngine::SwitchMonitorToWorkspaceIndex(int monitorIndex, int workspaceIndex)
    {
        const auto monitors = monitors_.EnumerateMonitors();
        if (monitorIndex < 0 || monitorIndex >= static_cast<int>(monitors.size()))
        {
            return;
        }
        const auto workspaces = workspaces_.GetWorkspacesForMonitor(monitors[monitorIndex].stableId);
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
            if (!IsWindow(hwnd) ||
                !windows_.IsWindowFromProcess(hwnd, entry.processId, entry.executablePath, entry.processCreationTime))
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
                workspaces_.SetWindowState(hwnd, state.wasMinimized
                                                     ? core::WindowVisibilityState::Minimized
                                                     : (state.wasMaximized ? core::WindowVisibilityState::Maximized
                                                                           : core::WindowVisibilityState::Visible));
            }
            recovery_.RecordShow(hwnd);
            if (OnStateChanged)
            {
                OnStateChanged();
            }
            return;
        }
    }
}
