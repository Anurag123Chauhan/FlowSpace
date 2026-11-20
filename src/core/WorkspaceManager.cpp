#include "core/WorkspaceManager.h"

#include <algorithm>
#include <atomic>
#include <format>

namespace flowspace::core
{

    namespace
    {

        std::vector<Workspace *> SortedWorkspacesForMonitor(std::vector<Workspace> &list,
                                                            const std::wstring &monitorStableId)
        {
            std::vector<Workspace *> result;
            for (auto &ws : list)
            {
                if (ws.monitorStableId == monitorStableId)
                {
                    result.push_back(&ws);
                }
            }
            std::stable_sort(result.begin(), result.end(),
                             [](const Workspace *a, const Workspace *b) { return a->orderIndex < b->orderIndex; });
            return result;
        }

        std::vector<Workspace> CopyWorkspacesForMonitor(const std::vector<Workspace> &list,
                                                        const std::wstring &monitorStableId)
        {
            std::vector<Workspace> result;
            for (const auto &ws : list)
            {
                if (ws.monitorStableId == monitorStableId)
                {
                    result.push_back(ws);
                }
            }
            std::stable_sort(result.begin(), result.end(),
                             [](const Workspace &a, const Workspace &b) { return a.orderIndex < b.orderIndex; });
            return result;
        }

    } // namespace

    WorkspaceManager::WorkspaceManager()
    {
        InitializeDefaultWorkspaces();
    }

    void WorkspaceManager::InitializeDefaultWorkspaces()
    {
        std::scoped_lock lock(mutex_);
        auto make = [](std::string id, std::string name, std::string icon, std::string accentColor, int32_t orderIndex)
        {
            Workspace ws;
            ws.id = std::move(id);
            ws.name = std::move(name);
            ws.icon = std::move(icon);
            ws.accentColor = std::move(accentColor);
            ws.orderIndex = orderIndex;
            return ws;
        };

        // Unassigned on purpose: ReconcileMonitors adopts them for the first display and
        // clones the set for every additional monitor.
        workspaces_ = {make("ws-1", "Default WS", "grid", "#A87E5A", 0)};
    }

    void WorkspaceManager::LoadWorkspaces(std::vector<Workspace> workspaces)
    {
        std::scoped_lock lock(mutex_);
        workspaces_ = std::move(workspaces);
    }

    void WorkspaceManager::ReconcileMonitors(const std::vector<MonitorInfo> &detectedMonitors)
    {
        std::scoped_lock lock(mutex_);

        // Mark all previously known monitors as disconnected until matched.
        for (auto &[id, mon] : monitorsByStableId_)
        {
            mon.isConnected = false;
            mon.runtimeHandle = nullptr;
        }

        for (const auto &detected : detectedMonitors)
        {
            auto it = monitorsByStableId_.find(detected.stableId);
            if (it != monitorsByStableId_.end())
            {
                // Reconnected or refreshed monitor: preserve its active workspace.
                std::string preservedWorkspaceId = it->second.activeWorkspaceId;
                it->second = detected;
                it->second.activeWorkspaceId = std::move(preservedWorkspaceId);
                it->second.isConnected = true;
            }
            else
            {
                MonitorInfo newMon = detected;
                newMon.isConnected = true;
                monitorsByStableId_.emplace(newMon.stableId, std::move(newMon));
            }
        }

        // Every connected monitor must own at least one workspace. Unassigned workspaces
        // (legacy config or a fresh install) go to the first monitor that needs a list;
        // additional monitors receive a clone of an existing list so their numbering starts at 1.
        for (const auto &detected : detectedMonitors)
        {
            const std::wstring &monitorId = detected.stableId;
            auto &mon = monitorsByStableId_[monitorId];

            auto existing = SortedWorkspacesForMonitor(workspaces_, monitorId);
            if (existing.empty())
            {
                // Adopt all unassigned workspaces, preserving their relative order.
                std::vector<Workspace *> unassigned;
                for (auto &ws : workspaces_)
                {
                    if (ws.monitorStableId.empty())
                    {
                        unassigned.push_back(&ws);
                    }
                }
                std::stable_sort(unassigned.begin(), unassigned.end(),
                                 [](const Workspace *a, const Workspace *b) { return a->orderIndex < b->orderIndex; });
                int32_t nextOrder = 0;
                for (auto *ws : unassigned)
                {
                    ws->monitorStableId = monitorId;
                    ws->orderIndex = nextOrder++;
                }

                existing = SortedWorkspacesForMonitor(workspaces_, monitorId);
                if (existing.empty())
                {
                    // Clone a connected display's set (prefer the primary) so new monitors
                    // start with the same familiar workspaces.
                    const MonitorInfo *source = nullptr;
                    for (const auto &det : detectedMonitors)
                    {
                        if (!det.isPrimary)
                        {
                            continue;
                        }
                        if (std::any_of(workspaces_.begin(), workspaces_.end(),
                                        [&](const Workspace &ws) { return ws.monitorStableId == det.stableId; }))
                        {
                            source = &det;
                            break;
                        }
                    }
                    if (!source)
                    {
                        for (const auto &det : detectedMonitors)
                        {
                            if (det.stableId == monitorId)
                            {
                                continue;
                            }
                            if (std::any_of(workspaces_.begin(), workspaces_.end(),
                                            [&](const Workspace &ws) { return ws.monitorStableId == det.stableId; }))
                            {
                                source = &det;
                                break;
                            }
                        }
                    }

                    if (source)
                    {
                        const auto sourceActive = monitorsByStableId_[source->stableId].activeWorkspaceId;
                        const std::string cloneActive =
                            CloneWorkspaceSetUnlocked(source->stableId, monitorId, sourceActive);
                        if (!cloneActive.empty())
                        {
                            mon.activeWorkspaceId = cloneActive;
                        }
                    }
                    else
                    {
                        Workspace ws;
                        ws.id = GenerateWorkspaceIdUnlocked();
                        ws.name = "Workspace 1";
                        ws.monitorStableId = monitorId;
                        ws.orderIndex = 0;
                        workspaces_.push_back(std::move(ws));
                    }
                }
            }

            RenumberMonitorWorkspacesUnlocked(monitorId);

            if (mon.activeWorkspaceId.empty() || !WorkspaceBelongsToMonitorUnlocked(mon.activeWorkspaceId, monitorId))
            {
                mon.activeWorkspaceId = FirstWorkspaceIdForMonitorUnlocked(monitorId);
            }
        }
    }

    std::vector<MonitorInfo> WorkspaceManager::GetMonitors() const
    {
        std::scoped_lock lock(mutex_);
        std::vector<MonitorInfo> result;
        result.reserve(monitorsByStableId_.size());
        for (const auto &[_, mon] : monitorsByStableId_)
        {
            result.push_back(mon);
        }
        return result;
    }

    std::optional<MonitorInfo> WorkspaceManager::GetMonitor(const std::wstring &stableId) const
    {
        std::scoped_lock lock(mutex_);
        if (auto it = monitorsByStableId_.find(stableId); it != monitorsByStableId_.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    std::vector<Workspace> WorkspaceManager::GetWorkspaces() const
    {
        std::scoped_lock lock(mutex_);
        std::vector<Workspace> result = workspaces_;
        // Group by owning monitor; a monitor's own orderIndex decides its internal order.
        std::stable_sort(result.begin(), result.end(),
                         [](const Workspace &a, const Workspace &b)
                         {
                             if (a.monitorStableId != b.monitorStableId)
                             {
                                 return a.monitorStableId < b.monitorStableId;
                             }
                             return a.orderIndex < b.orderIndex;
                         });
        return result;
    }

    std::vector<Workspace> WorkspaceManager::GetWorkspacesForMonitor(const std::wstring &monitorStableId) const
    {
        std::scoped_lock lock(mutex_);
        return CopyWorkspacesForMonitor(workspaces_, monitorStableId);
    }

    std::vector<Workspace> *WorkspaceManager::MutableWorkspaces()
    {
        // Deliberately no lock: the settings UI edits on the same thread that owns the
        // manager (single-threaded engine design), and the returned pointer must remain
        // valid for in-place editing across multiple field updates.
        return &workspaces_;
    }

    std::optional<Workspace> WorkspaceManager::GetWorkspace(const std::string &workspaceId) const
    {
        std::scoped_lock lock(mutex_);
        auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
                               [&](const Workspace &ws) { return ws.id == workspaceId; });
        if (it != workspaces_.end())
        {
            return *it;
        }
        return std::nullopt;
    }

    std::optional<int> WorkspaceManager::GetWorkspaceNumber(const std::wstring &monitorStableId,
                                                            const std::string &workspaceId) const
    {
        std::scoped_lock lock(mutex_);
        const auto list = CopyWorkspacesForMonitor(workspaces_, monitorStableId);
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i].id == workspaceId)
            {
                return static_cast<int>(i) + 1;
            }
        }
        return std::nullopt;
    }

    Workspace WorkspaceManager::CreateWorkspace(std::string name, std::wstring monitorStableId, std::string icon,
                                                std::string accentColor)
    {
        std::scoped_lock lock(mutex_);
        const auto siblings = SortedWorkspacesForMonitor(workspaces_, monitorStableId);
        Workspace ws;
        ws.id = GenerateWorkspaceIdUnlocked();
        ws.name = std::move(name);
        ws.icon = std::move(icon);
        ws.accentColor = std::move(accentColor);
        ws.orderIndex = static_cast<int32_t>(siblings.size());
        ws.monitorStableId = std::move(monitorStableId);
        workspaces_.push_back(ws);
        return ws;
    }

    bool WorkspaceManager::RenameWorkspace(const std::string &workspaceId, std::string newName)
    {
        if (newName.empty())
        {
            return false;
        }

        std::scoped_lock lock(mutex_);
        auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
                               [&](const Workspace &ws) { return ws.id == workspaceId; });
        if (it == workspaces_.end())
        {
            return false;
        }

        it->name = std::move(newName);
        return true;
    }

    bool WorkspaceManager::ReorderWorkspace(const std::string &workspaceId, int32_t newOrderIndex)
    {
        std::scoped_lock lock(mutex_);
        auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
                               [&](const Workspace &ws) { return ws.id == workspaceId; });
        if (it == workspaces_.end())
        {
            return false;
        }

        auto siblings = SortedWorkspacesForMonitor(workspaces_, it->monitorStableId);
        if (siblings.size() < 2)
        {
            return true;
        }

        const int32_t clampedIndex = std::clamp(newOrderIndex, 0, static_cast<int32_t>(siblings.size() - 1));
        const auto fromIt = std::find(siblings.begin(), siblings.end(), &*it);
        if (fromIt == siblings.end())
        {
            return false;
        }
        Workspace *moved = *fromIt;
        siblings.erase(fromIt);
        siblings.insert(siblings.begin() + clampedIndex, moved);
        for (size_t i = 0; i < siblings.size(); ++i)
        {
            siblings[i]->orderIndex = static_cast<int32_t>(i);
        }
        return true;
    }

    bool WorkspaceManager::DeleteWorkspace(const std::string &workspaceId, const std::string &fallbackWorkspaceId)
    {
        std::scoped_lock lock(mutex_);
        if (workspaceId == fallbackWorkspaceId)
        {
            return false;
        }

        const auto targetIt = std::find_if(workspaces_.begin(), workspaces_.end(),
                                           [&](const Workspace &ws) { return ws.id == workspaceId; });
        if (targetIt == workspaces_.end())
        {
            return false;
        }

        const std::wstring ownerMonitor = targetIt->monitorStableId;
        auto siblings = SortedWorkspacesForMonitor(workspaces_, ownerMonitor);
        if (siblings.size() <= 1)
        {
            return false; // never delete a monitor's last workspace
        }
        const auto fallbackIt = std::find_if(workspaces_.begin(), workspaces_.end(),
                                             [&](const Workspace &ws) { return ws.id == fallbackWorkspaceId; });
        if (fallbackIt == workspaces_.end() || fallbackIt->monitorStableId != ownerMonitor)
        {
            return false;
        }

        // Reassign all windows currently bound to the deleted workspace.
        for (auto &[_, win] : trackedWindowsByHwnd_)
        {
            if (win.assignedWorkspaceId == workspaceId)
            {
                win.assignedWorkspaceId = fallbackWorkspaceId;
            }
        }

        // Update the owning monitor if it is currently viewing the deleted workspace.
        for (auto &[_, mon] : monitorsByStableId_)
        {
            if (mon.activeWorkspaceId == workspaceId)
            {
                mon.activeWorkspaceId = fallbackWorkspaceId;
            }
        }

        workspaces_.erase(targetIt);
        RenumberMonitorWorkspacesUnlocked(ownerMonitor);
        return true;
    }

    std::optional<std::string> WorkspaceManager::DuplicateWorkspace(const std::string &workspaceId)
    {
        std::scoped_lock lock(mutex_);
        auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
                               [&](const Workspace &ws) { return ws.id == workspaceId; });
        if (it == workspaces_.end())
        {
            return std::nullopt;
        }

        Workspace copy = *it;
        const std::wstring ownerMonitor = it->monitorStableId;
        const std::string copyId = GenerateWorkspaceIdUnlocked();
        copy.id = copyId;
        copy.name += " (copy)";
        copy.orderIndex = it->orderIndex + 1;
        for (auto &ws : workspaces_)
        {
            if (ws.monitorStableId == copy.monitorStableId && ws.orderIndex >= copy.orderIndex)
            {
                ++ws.orderIndex;
            }
        }
        workspaces_.push_back(std::move(copy));
        RenumberMonitorWorkspacesUnlocked(ownerMonitor);

        return copyId;
    }

    bool WorkspaceManager::ResetWorkspace(const std::string &workspaceId)
    {
        std::scoped_lock lock(mutex_);
        auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
                               [&](const Workspace &ws) { return ws.id == workspaceId; });
        if (it == workspaces_.end())
        {
            return false;
        }
        it->overlayBackgroundColor.clear();
        it->overlayPattern = L"none";
        it->overlayPatternOpacity = 0.10;
        it->overlayAnimation = "slide";
        it->overlayAnimationMs = 180;
        return true;
    }

    std::optional<std::string> WorkspaceManager::GetActiveWorkspaceId(const std::wstring &monitorStableId) const
    {
        std::scoped_lock lock(mutex_);
        if (auto it = monitorsByStableId_.find(monitorStableId); it != monitorsByStableId_.end())
        {
            return it->second.activeWorkspaceId;
        }
        return std::nullopt;
    }

    bool WorkspaceManager::SetActiveWorkspace(const std::wstring &monitorStableId, const std::string &workspaceId)
    {
        std::scoped_lock lock(mutex_);
        auto monIt = monitorsByStableId_.find(monitorStableId);
        if (monIt == monitorsByStableId_.end())
        {
            return false;
        }

        if (!WorkspaceBelongsToMonitorUnlocked(workspaceId, monitorStableId))
        {
            return false;
        }

        monIt->second.activeWorkspaceId = workspaceId;
        return true;
    }

    std::optional<std::string> WorkspaceManager::ResolveAdjacentWorkspaceId(const std::wstring &monitorStableId,
                                                                            WorkspaceSwitchDirection direction) const
    {
        std::scoped_lock lock(mutex_);

        const auto list = CopyWorkspacesForMonitor(workspaces_, monitorStableId);
        if (list.empty())
        {
            return std::nullopt;
        }

        auto monIt = monitorsByStableId_.find(monitorStableId);
        if (monIt == monitorsByStableId_.end())
        {
            return std::nullopt;
        }

        size_t currentIdx = 0;
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i].id == monIt->second.activeWorkspaceId)
            {
                currentIdx = i;
                break;
            }
        }

        const size_t count = list.size();
        const size_t nextIdx =
            (direction == WorkspaceSwitchDirection::Next) ? (currentIdx + 1) % count : (currentIdx + count - 1) % count;

        return list[nextIdx].id;
    }

    void WorkspaceManager::SyncDiscoveredWindows(const std::vector<WindowInfo> &discoveredWindows)
    {
        std::scoped_lock lock(mutex_);

        for (const auto &win : discoveredWindows)
        {
            if (!win.isManageable)
            {
                continue;
            }

            auto existingIt = trackedWindowsByHwnd_.find(win.hwnd);
            if (existingIt != trackedWindowsByHwnd_.end() &&
                existingIt->second.processCreationTime == win.processCreationTime)
            {
                // Update dynamic metadata while preserving the workspace assignment, unless
                // the window moved to another monitor (its old workspace is not valid there).
                existingIt->second.title = win.title;
                existingIt->second.monitorStableId = win.monitorStableId;
                existingIt->second.visibilityState = win.visibilityState;
                if (!win.monitorStableId.empty() &&
                    !WorkspaceBelongsToMonitorUnlocked(existingIt->second.assignedWorkspaceId, win.monitorStableId))
                {
                    existingIt->second.assignedWorkspaceId = ActiveOrDefaultWorkspaceIdUnlocked(win.monitorStableId);
                }
            }
            else
            {
                // New window: assign to the active workspace of the monitor where it appeared.
                WindowInfo tracked = win;
                tracked.assignedWorkspaceId = ActiveOrDefaultWorkspaceIdUnlocked(win.monitorStableId);
                trackedWindowsByHwnd_[win.hwnd] = std::move(tracked);
            }
        }
    }

    bool WorkspaceManager::AssignWindowToWorkspace(HWND hwnd, const std::string &workspaceId,
                                                   const std::wstring &monitorStableId)
    {
        std::scoped_lock lock(mutex_);
        auto winIt = trackedWindowsByHwnd_.find(hwnd);
        if (winIt == trackedWindowsByHwnd_.end())
        {
            return false;
        }

        auto wsIt = std::find_if(workspaces_.begin(), workspaces_.end(),
                                 [&](const Workspace &ws) { return ws.id == workspaceId; });
        if (wsIt == workspaces_.end())
        {
            return false;
        }

        std::wstring targetMonitor = monitorStableId;
        if (targetMonitor.empty())
        {
            targetMonitor = winIt->second.monitorStableId;
        }
        if (targetMonitor.empty())
        {
            targetMonitor = wsIt->monitorStableId;
        }
        if (!WorkspaceBelongsToMonitorUnlocked(workspaceId, targetMonitor))
        {
            return false;
        }

        winIt->second.assignedWorkspaceId = workspaceId;
        winIt->second.monitorStableId = std::move(targetMonitor);
        return true;
    }

    void WorkspaceManager::ForgetWindow(HWND hwnd)
    {
        std::scoped_lock lock(mutex_);
        trackedWindowsByHwnd_.erase(hwnd);
    }

    std::optional<WindowInfo> WorkspaceManager::GetWindow(HWND hwnd) const
    {
        std::scoped_lock lock(mutex_);
        auto it = trackedWindowsByHwnd_.find(hwnd);
        if (it == trackedWindowsByHwnd_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    void WorkspaceManager::SetWindowState(HWND hwnd, WindowVisibilityState state)
    {
        std::scoped_lock lock(mutex_);
        auto it = trackedWindowsByHwnd_.find(hwnd);
        if (it != trackedWindowsByHwnd_.end())
        {
            it->second.visibilityState = state;
        }
    }

    std::vector<WindowInfo> WorkspaceManager::GetTrackedWindows() const
    {
        std::scoped_lock lock(mutex_);
        std::vector<WindowInfo> result;
        result.reserve(trackedWindowsByHwnd_.size());
        for (const auto &[_, win] : trackedWindowsByHwnd_)
        {
            result.push_back(win);
        }
        return result;
    }

    std::vector<WindowInfo> WorkspaceManager::GetWindowsForMonitorAndWorkspace(const std::wstring &monitorStableId,
                                                                               const std::string &workspaceId) const
    {
        std::scoped_lock lock(mutex_);
        std::vector<WindowInfo> result;
        for (const auto &[_, win] : trackedWindowsByHwnd_)
        {
            if (win.monitorStableId == monitorStableId && win.assignedWorkspaceId == workspaceId)
            {
                result.push_back(win);
            }
        }
        return result;
    }

    std::vector<Workspace *> WorkspaceManager::WorkspacesForMonitorUnlocked(const std::wstring &monitorStableId)
    {
        return SortedWorkspacesForMonitor(workspaces_, monitorStableId);
    }

    void WorkspaceManager::RenumberMonitorWorkspacesUnlocked(const std::wstring &monitorStableId)
    {
        auto list = SortedWorkspacesForMonitor(workspaces_, monitorStableId);
        for (size_t i = 0; i < list.size(); ++i)
        {
            list[i]->orderIndex = static_cast<int32_t>(i);
        }
    }

    std::string WorkspaceManager::FirstWorkspaceIdForMonitorUnlocked(const std::wstring &monitorStableId) const
    {
        const auto list = CopyWorkspacesForMonitor(workspaces_, monitorStableId);
        return list.empty() ? std::string{} : list.front().id;
    }

    std::string WorkspaceManager::ActiveOrDefaultWorkspaceIdUnlocked(const std::wstring &monitorStableId) const
    {
        if (auto monIt = monitorsByStableId_.find(monitorStableId); monIt != monitorsByStableId_.end())
        {
            const std::string &active = monIt->second.activeWorkspaceId;
            if (!active.empty())
            {
                const auto list = CopyWorkspacesForMonitor(workspaces_, monitorStableId);
                if (std::any_of(list.begin(), list.end(), [&](const Workspace &ws) { return ws.id == active; }))
                {
                    return active;
                }
            }
            const std::string first = FirstWorkspaceIdForMonitorUnlocked(monitorStableId);
            if (!first.empty())
            {
                return first;
            }
        }

        // Unknown/stale monitor: fall back to any unassigned workspace, then to the first one.
        for (const auto &ws : workspaces_)
        {
            if (ws.monitorStableId.empty())
            {
                return ws.id;
            }
        }
        return workspaces_.empty() ? std::string{} : workspaces_.front().id;
    }

    std::string WorkspaceManager::GenerateWorkspaceIdUnlocked() const
    {
        static std::atomic<uint64_t> counter{100};
        for (;;)
        {
            std::string id = std::format("ws-{}", counter.fetch_add(1, std::memory_order_relaxed));
            const bool used =
                std::any_of(workspaces_.begin(), workspaces_.end(), [&](const Workspace &ws) { return ws.id == id; });
            if (!used)
            {
                return id;
            }
        }
    }

    std::string WorkspaceManager::CloneWorkspaceSetUnlocked(const std::wstring &sourceMonitorId,
                                                            const std::wstring &targetMonitorId,
                                                            const std::string &sourceActiveWorkspaceId)
    {
        const auto sourceList = CopyWorkspacesForMonitor(workspaces_, sourceMonitorId);
        std::string cloneActive;
        int32_t nextOrder = 0;
        for (const auto &source : sourceList)
        {
            Workspace copy = source;
            copy.id = GenerateWorkspaceIdUnlocked();
            copy.monitorStableId = targetMonitorId;
            copy.orderIndex = nextOrder++;
            if (source.id == sourceActiveWorkspaceId)
            {
                cloneActive = copy.id;
            }
            workspaces_.push_back(std::move(copy));
        }
        if (cloneActive.empty() && nextOrder > 0)
        {
            cloneActive = FirstWorkspaceIdForMonitorUnlocked(targetMonitorId);
        }
        return cloneActive;
    }

    bool WorkspaceManager::WorkspaceBelongsToMonitorUnlocked(const std::string &workspaceId,
                                                             const std::wstring &monitorStableId) const
    {
        return std::any_of(workspaces_.begin(), workspaces_.end(), [&](const Workspace &ws)
                           { return ws.id == workspaceId && ws.monitorStableId == monitorStableId; });
    }

} // namespace flowspace::core
