#pragma once

// Must be defined before <Windows.h> is pulled in, otherwise MinGW defaults
// _WIN32_WINNT to WS03 (0x0502), NTDDI_VERSION becomes 0x05020000, and headers
// such as ShellScalingApi.h compile out newer APIs like GetDpiForMonitor.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000 // NTDDI_WIN10
#endif

#include <Windows.h>
#include <cstdint>
#include <map>
#include <string>

namespace flowspace::core
{

    struct Rect
    {
        int32_t left{0};
        int32_t top{0};
        int32_t right{0};
        int32_t bottom{0};
        [[nodiscard]] constexpr int32_t width() const noexcept { return right - left; }
        [[nodiscard]] constexpr int32_t height() const noexcept { return bottom - top; }
        [[nodiscard]] constexpr bool Contains(int32_t x, int32_t y) const noexcept
        {
            return x >= left && x < right && y >= top && y < bottom; // windows screen coordinates Y-axis increase downwards thats why y>=top
        }
    };

    struct MonitorInfo
    {
        std::wstring stableId;           // Persistent hardware device path (eg: \\?\DISPLAY#DEL4060#5&1a2b3c4d&0&UID4352#{some-guid})
        std::wstring deviceName;         // GDI device name (eg: \\.\DISPLAY1)
        std::wstring friendlyName;       // Friendly name (eg: LG UltraWide)
        HMONITOR runtimeHandle{nullptr}; // Transient runtime handle (valid only until next WM_DISPLAYCHANGE)

        Rect bounds{};           // Monitor bounds in virtual screen coordinates
        Rect workArea{};         // Monitor work area in virtual screen coordinates
        uint32_t dpiX{96};       // Horizontal DPI (dots per inch)
        uint32_t dpiY{96};       // Vertical DPI
        bool isPrimary{false};   // True if this monitor is the primary display
        bool isConnected{false}; // True if this monitor is currently connected

        std::string activeWorkspaceId; // Active workspace ID (eg: "1", "2", "3", etc.) or empty string if no workspace is active
    };

    struct Workspace
    {
        std::string id;           // Persistent unique ID (eg: "ws-dev", UUID)
        std::string name;         // User-facing lable (eg: "Development", "Gaming", etc.)
        std::string icon{"grid"}; // Fluent icon glyph or identifier (eg: "grid", "code", "gamepad", etc.)
        std::string accentColor{"#4389D8"};
        int32_t orderIndex{0}; // Order index for sorting workspaces in the UI
        // Optional per-monitor wallpaper overrides: map<MonitorStableId, WallpaperPath>
        // Empty key "" acts as the workspace's default wallpaper across all monitors.
        std::wstring defaultWallpaperPath{};

        // Wallpaper fitted to the desktop via IDesktopWallpaper::SetPosition when applied.
        int32_t wallpaperFit{4}; // 0 Center, 1 Tile, 2 Stretch, 3 Fit, 4 Fill, 5 Span (DESKTOP_WALLPAPER_POSITION)
        std::wstring wallpaperBackgroundColor{L"#000000"};

        // Optional per-monitor wallpaper overrides keyed by the monitor's stable device path.
        // Values are full wallpaper image paths; a missing key falls back to defaultWallpaperPath.
        std::map<std::wstring, std::wstring, std::less<>> perMonitorWallpaper;
        // Per-monitor wallpaper fit overrides using the same DESKTOP_WALLPAPER_POSITION values.
        std::map<std::wstring, int32_t, std::less<>> perMonitorWallpaperFit;

        // Overlay appearance. The accent color is shared with accentColor; the remaining fields
        // only affect the switch overlay and previews.
        std::wstring overlayBackgroundColor{L""}; // empty = derived from accentColor
        std::wstring overlayTextColor{L"#FFFFFFFF"};
        std::wstring overlayPattern{L"none"}; // none | grid | dots | diagonal
        double overlayPatternOpacity{0.10};
        std::string overlayAnimation{"slide"}; // none | slide | fade | scale
        int32_t overlayAnimationMs{180};
    };

    enum class WindowVisibilityState
    {
        Visible,
        Minimized,
        Maximized,
        HiddenBySystem,
        CloakedByDwm, // Window is hidden by DWM (Desktop Window Manager), e.g. it lives on another virtual desktop
        HiddenByFlowSpace,
    };

    struct WindowInfo
    {
        // Runtime identity (never persisted across app restarts)
        HWND hwnd{nullptr}; // window handle
        DWORD processId{0}; // process ID of the window
        DWORD threadId{0};  // thread ID of the window
        uint64_t processCreationTime{0};

        // Application & window metadata
        std::wstring title;
        std::wstring className;
        std::wstring executablePath;
        std::wstring executableName;

        // Spatial & workspace ownership
        std::wstring monitorStableId;    // Stable ID of the monitor where the window is currently located
        std::string assignedWorkspaceId; // Stable ID of the workspace where the window is currently assigned

        // Window state flags
        WindowVisibilityState visibilityState{WindowVisibilityState::Visible};
        bool isManageable{true};     // True if the window is manageable by FlowSpace (eg: not a system window, not a UWP app, etc.)
        std::string exclusionReason; // If isManageable is false, this field contains the reason why the window is excluded from management
    };
}
