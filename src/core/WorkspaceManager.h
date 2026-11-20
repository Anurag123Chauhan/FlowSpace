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

    /// Stores each monitor's workspaces and its active workspace.
    /// Switching and numbering only use that monitor's list.
    class WorkspaceManager
    {
      public:
        WorkspaceManager();
        ~WorkspaceManager() = default;

        // Reconciles the internal monitor state with the provided list of detected monitors.
        // Connected monitors with no workspaces adopt unassigned workspaces (legacy config) or
        // receive a clone of an existing monitor's set, so every display always has a list.
        void ReconcileMonitors(const std::vector<MonitorInfo> &detectedMonitors);

        [[nodiscard]] std::vector<MonitorInfo> GetMonitors() const;
        [[nodiscard]] std::optional<MonitorInfo> GetMonitor(const std::wstring &stableId) const;

        void InitializeDefaultWorkspaces();
        void LoadWorkspaces(std::vector<Workspace> workspaces);
        [[nodiscard]] std::vector<Workspace> GetWorkspaces() const;
        [[nodiscard]] std::vector<Workspace> GetWorkspacesForMonitor(const std::wstring &monitorStableId) const;
        [[nodiscard]] std::optional<Workspace> GetWorkspace(const std::string &workspaceId) const;
        // 1-based position inside the monitor's own list; nullopt when not on that monitor.
        [[nodiscard]] std::optional<int> GetWorkspaceNumber(const std::wstring &monitorStableId,
                                                            const std::string &workspaceId) const;

        Workspace CreateWorkspace(std::string name, std::wstring monitorStableId, std::string icon = "grid",
                                  std::string accentColor = "#A87E5A");
        bool RenameWorkspace(const std::string &workspaceId, std::string newName);
        bool ReorderWorkspace(const std::string &workspaceId, int32_t newOrderIndex);
        // Deletes a workspace and reassigns any tracked windows to fallbackWorkspaceId.
        // A monitor's last workspace can never be deleted. Never destroys or orphans windows.
        bool DeleteWorkspace(const std::string &workspaceId, const std::string &fallbackWorkspaceId);
        // Copies a workspace into the same monitor's list, directly after the original.
        std::optional<std::string> DuplicateWorkspace(const std::string &workspaceId);
        // Resets appearance of a workspace to defaults, keeping its
        // identity, name, owner monitor, and any tracked windows.
        bool ResetWorkspace(const std::string &workspaceId);
        // Mutable access to the internal workspace list (settings UI editors). Callers must keep
        // ids and monitor ownership intact.
        [[nodiscard]] std::vector<Workspace> *MutableWorkspaces();

        [[nodiscard]] std::optional<std::string> GetActiveWorkspaceId(const std::wstring &monitorStableId) const;
        // Fails when the workspace does not belong to the given monitor.
        bool SetActiveWorkspace(const std::wstring &monitorStableId, const std::string &workspaceId);
        // Resolves the adjacent workspace of that monitor's own list (prev or next).
        [[nodiscard]] std::optional<std::string> ResolveAdjacentWorkspaceId(const std::wstring &monitorStableId,
                                                                            WorkspaceSwitchDirection direction) const;

        // Syncs discovered manageable windows into internal state without altering OS window visibility
        void SyncDiscoveredWindows(const std::vector<WindowInfo> &discoveredWindows);
        bool AssignWindowToWorkspace(HWND hwnd, const std::string &workspaceId, const std::wstring &monitorStableId);
        void ForgetWindow(HWND hwnd);

        [[nodiscard]] std::optional<WindowInfo> GetWindow(HWND hwnd) const;
        // Updates only the visibility state of a tracked window (used after hide/show).
        void SetWindowState(HWND hwnd, WindowVisibilityState state);

        [[nodiscard]] std::vector<WindowInfo> GetTrackedWindows() const;
        [[nodiscard]] std::vector<WindowInfo> GetWindowsForMonitorAndWorkspace(const std::wstring &monitorStableId,
                                                                               const std::string &workspaceId) const;

      private:
        // Unlocked helpers (caller holds mutex_).
        [[nodiscard]] std::vector<Workspace *> WorkspacesForMonitorUnlocked(const std::wstring &monitorStableId);
        void RenumberMonitorWorkspacesUnlocked(const std::wstring &monitorStableId);
        [[nodiscard]] std::string FirstWorkspaceIdForMonitorUnlocked(const std::wstring &monitorStableId) const;
        [[nodiscard]] std::string ActiveOrDefaultWorkspaceIdUnlocked(const std::wstring &monitorStableId) const;
        [[nodiscard]] std::string GenerateWorkspaceIdUnlocked() const;
        // Copies every workspace of sourceMonitorId into targetMonitorId with fresh ids.
        // Returns the target-equivalent of sourceActiveWorkspaceId (or the first clone).
        std::string CloneWorkspaceSetUnlocked(const std::wstring &sourceMonitorId, const std::wstring &targetMonitorId,
                                              const std::string &sourceActiveWorkspaceId);
        bool WorkspaceBelongsToMonitorUnlocked(const std::string &workspaceId,
                                               const std::wstring &monitorStableId) const;

        mutable std::mutex mutex_;
        std::vector<Workspace> workspaces_;
        std::unordered_map<std::wstring, MonitorInfo> monitorsByStableId_;
        std::unordered_map<HWND, WindowInfo> trackedWindowsByHwnd_;
    };
}
