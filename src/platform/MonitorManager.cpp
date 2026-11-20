#include "platform/MonitorManager.h"

#include <ShellScalingApi.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <vector>

namespace flowspace::platform
{
    namespace
    {
        BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC /*hdc*/, LPRECT /*lprcMonitor*/, LPARAM dwData)
        {
            auto *monitors = reinterpret_cast<std::vector<core::MonitorInfo> *>(dwData);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(MONITORINFOEXW);
            if (!GetMonitorInfoW(hMonitor, &mi))
            {
                spdlog::warn("GetMonitorInfoW failed for monitor 0x{:X} (error {})",
                             reinterpret_cast<uintptr_t>(hMonitor), GetLastError());
                return TRUE; // Continue enumeration
            }

            core::MonitorInfo info{};
            info.runtimeHandle = hMonitor;
            info.deviceName = mi.szDevice;
            info.bounds = {
                mi.rcMonitor.left,
                mi.rcMonitor.top,
                mi.rcMonitor.right,
                mi.rcMonitor.bottom,
            };
            info.workArea = {
                mi.rcWork.left,
                mi.rcWork.top,
                mi.rcWork.right,
                mi.rcWork.bottom,
            };
            info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            info.isConnected = true;

            UINT dpiX = 96, dpiY = 96;
            if (SUCCEEDED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
            {
                info.dpiX = dpiX;
                info.dpiY = dpiY;
            }
            monitors->push_back(std::move(info));
            return TRUE;
        }
    }

    std::vector<core::MonitorInfo> MonitorManager::EnumerateMonitors() const
    {
        std::vector<core::MonitorInfo> monitors;
        if (!EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&monitors)))
        {
            spdlog::error("EnumDisplayMonitors failed with error {}", GetLastError());
            return monitors;
        }

        for (auto &monitor : monitors)
        {
            if (auto metadata = QueryTargetMetadata(monitor.deviceName); metadata.has_value())
            {
                monitor.stableId = std::move(metadata->devicePath);
                monitor.friendlyName = std::move(metadata->friendlyName);
            }

            // Fallback if a virtual/headless display driver does not expose a CCD target device path
            if (monitor.stableId.empty())
            {
                spdlog::warn("Falling back to GDI device name for monitor '{}'",
                             std::string(monitor.deviceName.begin(), monitor.deviceName.end()));
                monitor.stableId = monitor.deviceName;
            }

            if (monitor.friendlyName.empty())
            {
                monitor.friendlyName = monitor.deviceName;
            }
        }

        // Sort deterministically: primary monitor first, then left-to-right, top-to-bottom
        std::sort(monitors.begin(), monitors.end(),
                  [](const core::MonitorInfo &a, const core::MonitorInfo &b)
                  {
                      if (a.isPrimary != b.isPrimary)
                          return a.isPrimary > b.isPrimary;
                      if (a.bounds.left != b.bounds.left)
                          return a.bounds.left < b.bounds.left;
                      return a.bounds.top < b.bounds.top;
                  });

        return monitors;
    }

    std::optional<std::wstring> MonitorManager::GetMonitorIdForWindow(HWND hwnd) const
    {
        if (!IsWindow(hwnd))
        {
            return std::nullopt;
        }

        HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (!hMonitor)
        {
            return std::nullopt;
        }

        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(MONITORINFOEXW);
        if (!GetMonitorInfoW(hMonitor, &mi))
        {
            return std::nullopt;
        }

        if (auto metadata = QueryTargetMetadata(mi.szDevice); metadata.has_value() && !metadata->devicePath.empty())
        {
            return metadata->devicePath;
        }
        return std::wstring(mi.szDevice);
    }

    std::optional<std::wstring> MonitorManager::GetMonitorIdUnderCursor() const
    {
        POINT pt{};
        if (!GetCursorPos(&pt))
        {
            return std::nullopt;
        }

        HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        if (!hMonitor)
        {
            return std::nullopt;
        }

        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(MONITORINFOEXW);
        if (!GetMonitorInfoW(hMonitor, &mi))
        {
            return std::nullopt;
        }

        if (auto metadata = QueryTargetMetadata(mi.szDevice); metadata.has_value() && !metadata->devicePath.empty())
        {
            return metadata->devicePath;
        }
        return std::wstring(mi.szDevice);
    }

    std::optional<core::MonitorInfo> MonitorManager::GetMonitor(const std::wstring &stableId) const
    {
        if (stableId.empty())
        {
            return std::nullopt;
        }
        for (const auto &mon : EnumerateMonitors())
        {
            if (mon.stableId == stableId)
            {
                return mon;
            }
        }
        return std::nullopt;
    }

    std::optional<MonitorManager::DisplayTargetMetadata>
    MonitorManager::QueryTargetMetadata(const std::wstring &gdiDeviceName) const
    {
        UINT32 pathCount = 0; // Number of display paths
        UINT32 modeCount = 0; // Number of display modes
        LONG status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
        if (status != ERROR_SUCCESS || pathCount == 0)
        {
            return std::nullopt;
        }

        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        if (status != ERROR_SUCCESS)
        {
            spdlog::warn("QueryDisplayConfig failed with status {}", status);
            return std::nullopt;
        }

        // Iterate through the display paths to find the one that matches the GDI device name
        for (const auto &path : paths)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
            sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            sourceName.header.size = sizeof(sourceName);
            sourceName.header.adapterId = path.sourceInfo.adapterId;
            sourceName.header.id = path.sourceInfo.id;

            if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS)
            {
                continue;
            }

            if (gdiDeviceName != sourceName.viewGdiDeviceName)
            {
                continue;
            }

            DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
            targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            targetName.header.size = sizeof(targetName);
            targetName.header.adapterId = path.targetInfo.adapterId;
            targetName.header.id = path.targetInfo.id;

            // Query the target device name to get the device path and friendly name
            if (DisplayConfigGetDeviceInfo(&targetName.header) == ERROR_SUCCESS)
            {
                DisplayTargetMetadata result{};
                result.devicePath = targetName.monitorDevicePath;
                if (targetName.flags.friendlyNameFromEdid && targetName.monitorFriendlyDeviceName[0] != L'\0')
                {
                    result.friendlyName = targetName.monitorFriendlyDeviceName;
                }
                return result;
            }
        }

        return std::nullopt;
    }
}