#include "core/WorkspaceManager.h"

#include <algorithm>
#include <format>

namespace flowspace::core
{

    namespace
    {
        // Next "ws-N" id not used by any existing workspace (persisted ids included), so ids
        // stay unique across restarts.
        std::string GenerateWorkspaceId(const std::vector<Workspace> &existing)
        {
            uint64_t next = 100;
            for (const auto &ws : existing)
            {
                if (ws.id.starts_with("ws-"))
                {
                    try
                    {
                        next = std::max<uint64_t>(next, std::stoull(ws.id.substr(3)) + 1);
                    }
                    catch (const std::exception &)
                    {
                        // non-numeric suffix: cannot collide with a numeric one
                    }
                }
            }
            return std::format("ws-{}", next);
        }

    } // namespace

    WorkspaceManager::WorkspaceManager()
    {
        InitializeDefaultWorkspaces();
    }

    void WorkspaceManager::InitializeDefaultWorkspaces()
    {
        std::scoped_lock lock(mutex_);
        workspaces_ = {
            Workspace{.id = "ws-1", .name = "Development", .icon = "code", .accentColor = "#4389D8", .orderIndex = 0},
            Workspace{.id = "ws-2", .name = "Research", .icon = "book", .accentColor = "#2EA043", .orderIndex = 1},
            Workspace{.id = "ws-3", .name = "Personal", .icon = "home", .accentColor = "#D29922", .orderIndex = 2},
            Workspace{.id = "ws-4", .name = "Communication", .icon = "chat", .accentColor = "#A371F7", .orderIndex = 3}};
    }

    void WorkspaceManager::ReconcileMonitors(const std::vector<MonitorInfo> &detectedMonitors)
    {
        std::scoped_lock lock(mutex_);

        // Mark all previously known monitors as disconnected until matched
        for (auto &[id, mon] : monitorsByStableId_)
        {
            mon.isConnected = false;
            mon.runtimeHandle = nullptr;
        }

        const std::string defaultWsId = EnsureValidDefaultWorkspaceId();

        for (const auto &detected : detectedMonitors)
        {
            auto it = monitorsByStableId_.find(detected.stableId);
            if (it != monitorsByStableId_.end())
            {
                // Reconnected or refreshed monitor: preserve its activeWorkspaceId!
                std::string preservedWorkspaceId = it->second.activeWorkspaceId;
                if (preservedWorkspaceId.empty())
                {
                    preservedWorkspaceId = defaultWsId;
                }
                it->second = detected;
                it->second.activeWorkspaceId = std::move(preservedWorkspaceId);
                it->second.isConnected = true;
            }
            else
            {
                MonitorInfo newMon = detected;
                newMon.activeWorkspaceId = defaultWsId;
                newMon.isConnected = true;
                monitorsByStableId_.emplace(newMon.stableId, std::move(newMon));
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
        return workspaces_;
    }

    void WorkspaceManager::SetWorkspaces(std::vector<Workspace> workspaces)
    {
        std::scoped_lock lock(mutex_);
        if (workspaces.empty())
        {
            return; // never run with zero workspaces
        }
        std::sort(workspaces.begin(), workspaces.end(),
                  [](const Workspace &a, const Workspace &b) { return a.orderIndex < b.orderIndex; });
        workspaces_ = std::move(workspaces);
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
        auto it = std::find_if(workspaces_.begin(), workspaces_.end(), [&](const Workspace &ws)
                               { return ws.id == workspaceId; });
        if (it != workspaces_.end())
        {
            return *it;
        }
        return std::nullopt;
    }

    Workspace WorkspaceManager::CreateWorkspace(std::string name, std::string icon, std::string accentColor)
    {
        std::scoped_lock lock(mutex_);
        Workspace ws{
            .id = GenerateWorkspaceId(workspaces_),
            .name = std::move(name),
            .icon = std::move(icon),
            .accentColor = std::move(accentColor),
            .orderIndex = static_cast<int32_t>(workspaces_.size())};
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
        auto it = std::find_if(workspaces_.begin(), workspaces_.end(), [&](const Workspace &ws)
                               { return ws.id == workspaceId; });
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
        if (workspaces_.empty())
        {
            return false;
        }

        auto it = std::find_if(workspaces_.begin(), workspaces_.end(), [&](const Workspace &ws)
                               { return ws.id == workspaceId; });
        if (it == workspaces_.end())
        {
            return false;
        }

        const int32_t clampedIndex = std::clamp(newOrderIndex, 0, static_cast<int32_t>(workspaces_.size() - 1));
        Workspace moved = std::move(*it);
        workspaces_.erase(it);
        workspaces_.insert(workspaces_.begin() + clampedIndex, std::move(moved));

        for (size_t i = 0; i < workspaces_.size(); ++i)
        {
            workspaces_[i].orderIndex = static_cast<int32_t>(i);
        }
        return true;
    }

    bool WorkspaceManager::DeleteWorkspace(const std::string &workspaceId, const std::string &fallbackWorkspaceId)
    {
        std::scoped_lock lock(mutex_);
        if (workspaces_.size() <= 1 || workspaceId == fallbackWorkspaceId)
        {
            return false;
        }

        const auto targetIt = std::find_if(workspaces_.begin(), workspaces_.end(), [&](const Workspace &ws)
                                           { return ws.id == workspaceId; });
        const auto fallbackIt = std::find_if(workspaces_.begin(), workspaces_.end(), [&](const Workspace &ws)
                                             { return ws.id == fallbackWorkspaceId; });

        if (targetIt == workspaces_.end() || fallbackIt == workspaces_.end())
        {
            return false;
        }

        // Reassign all windows currently bound to the deleted workspace
        for (auto &[_, win] : trackedWindowsByHwnd_)
        {
            if (win.assignedWorkspaceId == workspaceId)
            {
                win.assignedWorkspaceId = fallbackWorkspaceId;
            }
        }

        // Update any monitor currently viewing the deleted workspace
        for (auto &[_, mon] : monitorsByStableId_)
        {
            if (mon.activeWorkspaceId == workspaceId)
            {
                mon.activeWorkspaceId = fallbackWorkspaceId;
            }
        }

        workspaces_.erase(targetIt);
        for (size_t i = 0; i < workspaces_.size(); ++i)
        {
            workspaces_[i].orderIndex = static_cast<int32_t>(i);
        }

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
        copy.id = GenerateWorkspaceId(workspaces_);
        copy.name += " (copy)";
        workspaces_.insert(it + 1, copy);

        // Re-number order indexes.
        for (size_t i = 0; i < workspaces_.size(); ++i)
        {
            workspaces_[i].orderIndex = static_cast<int32_t>(i);
        }
        return copy.id;
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
        it->defaultWallpaperPath.clear();
        it->perMonitorWallpaper.clear();
        it->perMonitorWallpaperFit.clear();
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

        const bool wsExists = std::any_of(workspaces_.begin(), workspaces_.end(), [&](const Workspace &ws)
                                          { return ws.id == workspaceId; });
        if (!wsExists)
        {
            return false;
        }

        monIt->second.activeWorkspaceId = workspaceId;
        return true;
    }

    std::optional<std::string> WorkspaceManager::ResolveAdjacentWorkspaceId(
        const std::wstring &monitorStableId,
        WorkspaceSwitchDirection direction) const
    {

        std::scoped_lock lock(mutex_);
        if (workspaces_.empty())
        {
            return std::nullopt;
        }

        auto monIt = monitorsByStableId_.find(monitorStableId);
        if (monIt == monitorsByStableId_.end())
        {
            return std::nullopt;
        }

        auto wsIt = std::find_if(workspaces_.begin(), workspaces_.end(), [&](const Workspace &ws)
                                 { return ws.id == monIt->second.activeWorkspaceId; });

        if (wsIt == workspaces_.end())
        {
            return workspaces_.front().id;
        }

        const size_t currentIdx = static_cast<size_t>(std::distance(workspaces_.begin(), wsIt));
        const size_t count = workspaces_.size();
        const size_t nextIdx = (direction == WorkspaceSwitchDirection::Next)
                                   ? (currentIdx + 1) % count
                                   : (currentIdx + count - 1) % count;

        return workspaces_[nextIdx].id;
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
                // Update dynamic metadata while preserving existing workspace assignment
                existingIt->second.title = win.title;
                existingIt->second.monitorStableId = win.monitorStableId;
                existingIt->second.visibilityState = win.visibilityState;
            }
            else
            {
                // New window: assign to the active workspace of the monitor where it appeared
                WindowInfo tracked = win;
                if (auto monIt = monitorsByStableId_.find(win.monitorStableId);
                    monIt != monitorsByStableId_.end() && !monIt->second.activeWorkspaceId.empty())
                {
                    tracked.assignedWorkspaceId = monIt->second.activeWorkspaceId;
                }
                else
                {
                    tracked.assignedWorkspaceId = EnsureValidDefaultWorkspaceId();
                }
                trackedWindowsByHwnd_[win.hwnd] = std::move(tracked);
            }
        }
    }

    bool WorkspaceManager::AssignWindowToWorkspace(
        HWND hwnd,
        const std::string &workspaceId,
        const std::wstring &monitorStableId)
    {

        std::scoped_lock lock(mutex_);
        auto winIt = trackedWindowsByHwnd_.find(hwnd);
        if (winIt == trackedWindowsByHwnd_.end())
        {
            return false;
        }

        const bool wsExists = std::any_of(workspaces_.begin(), workspaces_.end(), [&](const Workspace &ws)
                                          { return ws.id == workspaceId; });
        if (!wsExists)
        {
            return false;
        }

        winIt->second.assignedWorkspaceId = workspaceId;
        if (!monitorStableId.empty())
        {
            winIt->second.monitorStableId = monitorStableId;
        }
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

    std::vector<WindowInfo> WorkspaceManager::GetWindowsForMonitorAndWorkspace(
        const std::wstring &monitorStableId,
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

    std::string WorkspaceManager::EnsureValidDefaultWorkspaceId() const
    {
        if (!workspaces_.empty())
        {
            return workspaces_.front().id;
        }
        return "ws-1";
    }

} // namespace flowspace::core