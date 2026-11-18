#pragma once

#include "core/Models.h"
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace flowspace::core
{
    enum class WorkspaceSwitchDirection
    {
        Previous,
        Next
    };

    class WorkspaceManager
    {
    public:
        WorkspaceManager();
        ~WorkspaceManager() = default;

        // --- Monitor State & Reconciliation ---
        // Reconciles the internal monitor state with the provided list of detected monitors, updating the internal state to refelect the current physical display congiguration.
        // This includes adding new monitors, removing disconnected monitors, and updating existing monitor information.
        void ReconcileMonitors(const std::vector<MonitorInfo> &detectedMonitors);

        [[nodiscard]] std::vector<MonitorInfo> GetMonitors() const;
        [[nodiscard]] std::optional<MonitorInfo> GetMonitor(const std::wstring &stableId) const;

        // --- Dynamic Workspace CRUD ---
        void InitializeDefaultWorkspaces();
        // Replaces the workspace list with a persisted one (ignored when empty).
        void SetWorkspaces(std::vector<Workspace> workspaces);
        [[nodiscard]] std::vector<Workspace> GetWorkspaces() const;
        [[nodiscard]] std::optional<Workspace> GetWorkspace(const std::string &workspaceId) const;

        Workspace CreateWorkspace(std::string name, std::string icon = "grid", std::string accentColor = "#4389D8");
        bool RenameWorkspace(const std::string &workspaceId, std::string newName);
        bool ReorderWorkspace(const std::string &workspaceId, int32_t newOrderIndex);
        // Deletes a workspace and reassigns any tracked windows to fallbackWorkspaceId.Never destroys or orphans tracked windows.
        bool DeleteWorkspace(const std::string &workspaceId, const std::string &fallbackWorkspaceId);
        // Copies a workspace's name/icon/accent/wallpaper configuration into a new workspace
        // placed directly after the original. Returns the new id, or nullopt if the source
        // workspace was not found.
        std::optional<std::string> DuplicateWorkspace(const std::string &workspaceId);
        // Resets appearance and wallpaper configuration of a workspace to defaults, keeping its
        // identity, name, and any tracked windows.
        bool ResetWorkspace(const std::string &workspaceId);
        // Mutable access to the internal workspace list (settings UI editors). The list is
        // returned sorted by orderIndex; callers must keep ids intact.
        [[nodiscard]] std::vector<Workspace> *MutableWorkspaces();

        // --- Per-Monitor Active Workspace State ---
        [[nodiscard]] std::optional<std::string> GetActiveWorkspaceId(const std::wstring &monitorStableId) const;
        bool SetActiveWorkspace(const std::wstring &monitorStableId, const std::string &workspaceId);
        // Resolves the adjacent workspace ID for a given monitor and direction (prev or next)
        [[nodiscard]] std::optional<std::string> ResolveAdjacentWorkspaceId(const std::wstring &monitorStableId, WorkspaceSwitchDirection direction) const;

        // --- Window Assignment Tracking ---
        // Syncs discovered manageable windows into internal state without altering OS window visibility
        void SyncDiscoveredWindows(const std::vector<WindowInfo> &discoveredWindows);
        bool AssignWindowToWorkspace(HWND hwnd, const std::string &workspaceId, const std::wstring &monitorStableId);
        void ForgetWindow(HWND hwnd);

        // Read access to a single tracked window (by runtime HWND).
        [[nodiscard]] std::optional<WindowInfo> GetWindow(HWND hwnd) const;
        // Updates only the visibility state of a tracked window (used after hide/show).
        void SetWindowState(HWND hwnd, WindowVisibilityState state);

        [[nodiscard]] std::vector<WindowInfo> GetTrackedWindows() const;
        [[nodiscard]] std::vector<WindowInfo> GetWindowsForMonitorAndWorkspace(const std::wstring &monitorStableId, const std::string &workspaceId) const;

    private:
        [[nodiscard]] std::string EnsureValidDefaultWorkspaceId() const;

        mutable std::mutex mutex_;
        std::vector<Workspace> workspaces_;
        std::unordered_map<std::wstring, MonitorInfo> monitorsByStableId_;
        std::unordered_map<HWND, WindowInfo> trackedWindowsByHwnd_;
    };
}