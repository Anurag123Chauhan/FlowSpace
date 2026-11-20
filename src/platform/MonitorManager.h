#pragma once

#include "core/Models.h"
#include <optional>
#include <string>
#include <vector>

namespace flowspace::platform
{
    class IMonitorManager
    {
      public:
        virtual ~IMonitorManager() = default;

        [[nodiscard]] virtual std::vector<core::MonitorInfo> EnumerateMonitors() const = 0;
        [[nodiscard]] virtual std::optional<std::wstring> GetMonitorIdForWindow(HWND hwnd) const = 0;
        [[nodiscard]] virtual std::optional<std::wstring> GetMonitorIdUnderCursor() const = 0;
        // Resolves the monitor descriptor for a stable id; nullopt when disconnected/unknown.
        [[nodiscard]] virtual std::optional<core::MonitorInfo> GetMonitor(const std::wstring &stableId) const = 0;
    };

    class MonitorManager final : public IMonitorManager
    {
      public:
        MonitorManager() = default;
        ~MonitorManager() override = default;

        MonitorManager(const MonitorManager &) = delete;
        MonitorManager &operator=(const MonitorManager &) = delete;

        [[nodiscard]] virtual std::vector<core::MonitorInfo> EnumerateMonitors() const override;
        [[nodiscard]] virtual std::optional<std::wstring> GetMonitorIdForWindow(HWND hwnd) const override;
        [[nodiscard]] virtual std::optional<std::wstring> GetMonitorIdUnderCursor() const override;
        [[nodiscard]] std::optional<core::MonitorInfo> GetMonitor(const std::wstring &stableId) const;

      private:
        struct DisplayTargetMetadata
        {
            std::wstring devicePath;
            std::wstring friendlyName;
        };

        [[nodiscard]] std::optional<DisplayTargetMetadata> QueryTargetMetadata(const std::wstring &devicePath) const;
    };
}
