#pragma once

#include "core/Models.h"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

namespace flowspace::core
{
    // --- Configuration data model (versioned JSON at %LOCALAPPDATA%\FlowSpace\config.json) ---

    enum class HideMode
    {
        Hide = 0,     // ShowWindowAsync(SW_HIDE): taskbar button disappears.
        Minimize = 1, // ShowWindowAsync(SW_MINIMIZE): safer, taskbar button remains.
    };

    enum class HotkeyAction
    {
        PreviousWorkspace = 0,
        NextWorkspace = 1,
        SwitchToWorkspaceByNumber = 2,
        MoveActiveWindowToPreviousWorkspace = 3,
        MoveActiveWindowToNextWorkspace = 4,
        MoveActiveWindowToWorkspaceByNumber = 5,
        OpenWorkspaceSwitcher = 6,
        RevealAllWindows = 7,
        OpenSettings = 8,
    };

    struct HotkeyBinding
    {
        HotkeyAction action{HotkeyAction::PreviousWorkspace};
        bool enabled{true};
        std::wstring modifiers{L"Ctrl+Alt"}; // subset of Ctrl/Alt/Shift/Win
        std::wstring key{L"Left"};           // virtual key name, e.g. "Left", "Right", "D1"
    };

    enum class MonitorPreference
    {
        Any = 0,
        FollowWorkspace = 1,
        Specific = 2,
    };

    struct AppRule
    {
        std::string id;                       // persistent GUID
        bool enabled{true};
        std::wstring exeName;                 // e.g. L"devenv.exe" (case-insensitive)
        std::wstring exePath;                 // optional full path filter
        std::wstring titleContains;           // optional window title substring
        std::string targetWorkspaceId;        // assignment for matched windows
        MonitorPreference monitorPreference{MonitorPreference::Any};
        std::wstring monitorStableId;         // used when monitorPreference == Specific
        std::wstring launchCommand;           // optional "exe args" auto-launch command
    };

    struct OriginalWallpaper
    {
        std::wstring monitorDevicePath; // wallpaper-service monitor id
        std::wstring imagePath;         // empty = solid color only
        uint32_t backgroundColor{0};
        int32_t fit{4}; // DESKTOP_WALLPAPER_POSITION value
    };

    struct Settings
    {
        HideMode hideMode{HideMode::Hide};
        bool startWithWindows{false};
        bool switchByMouseMonitor{true};      // hotkeys act on the monitor under the cursor
        bool wallpaperManagementEnabled{true};
        bool restoreWallpaperOnExit{true};
        bool paused{false};                   // suspended workspace management
        bool groupWindowsByApplication{true}; // new windows inherit the app's workspace
        bool showOverlay{true};
        std::string overlayAnimation{"slide"}; // none | slide | fade | scale
        int32_t overlayAnimationMs{180};
        bool wallpaperCaptureInitialized{false};
    };

    struct Config
    {
        int32_t schemaVersion{1};
        Settings settings;
        std::vector<Workspace> workspaces;
        std::vector<AppRule> appRules;
        std::vector<HotkeyBinding> hotkeys;
        // Active workspace per monitor keyed by the monitor's stable device path.
        std::map<std::wstring, std::string, std::less<>> monitorState;
        std::vector<OriginalWallpaper> originalWallpapers;
    };

    // Result of a load operation; drives user-facing messaging in the UI.
    enum class ConfigLoadStatus
    {
        Loaded,           // parsed successfully
        RecoveredFromBackup, // main file corrupt, backup used
        FreshDefaults,    // no file existed (or all files unreadable): defaults created
    };

    class ConfigService
    {
    public:
        explicit ConfigService(std::wstring configPath, std::wstring backupPath);

        // Reads config.json; falls back to the backup when the main file is corrupt, then to
        // defaults. Applies schema migrations and normalizes the workspace list.
        ConfigLoadStatus Load();

        // Serializes and atomically writes config.json after rotating the previous file into
        // config.backup.json. Returns false when the write failed (the in-memory copy remains).
        bool Save();

        [[nodiscard]] const Config &Get() const { return config_; }
        [[nodiscard]] Config &Get() { return config_; }

        // Default 4-workspace setup, used for a fresh install and by migration from v0.
        static void ApplyDefaultWorkspaces(Config &config);

    private:
        static void Migrate(Config &config); // schemaVersion steps; v0 (missing field) → 1
        static void Normalize(Config &config);
        static void ApplyDefaultHotkeys(Config &config);

        std::wstring configPath_;
        std::wstring backupPath_;
        Config config_;
    };
}
