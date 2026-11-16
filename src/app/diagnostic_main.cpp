// Phase-1 diagnostic entry point: read-only report of monitors and manageable windows.
// Preserved as a migration reference from the original single-file diagnostic; built with
// -DFLOWSPACE_DIAGNOSTIC (see scripts/build.ps1). Never modifies window or wallpaper state.
#include "core/Models.h"
#include "core/StringUtils.h"
#include "platform/MonitorManager.h"
#include "platform/WindowManager.h"

#include <Windows.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
    void InitializeDiagnosticLogging()
    {
        std::filesystem::create_directories("logs");
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/flowspace-diag.log", true);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        auto logger = std::make_shared<spdlog::logger>("flowspace", spdlog::sinks_init_list{consoleSink, fileSink});
        logger->set_level(spdlog::level::debug);
        spdlog::set_default_logger(logger);
    }

    const char *VisibilityStateToString(flowspace::core::WindowVisibilityState state)
    {
        switch (state)
        {
        case flowspace::core::WindowVisibilityState::Visible: return "Visible";
        case flowspace::core::WindowVisibilityState::Minimized: return "Minimized";
        case flowspace::core::WindowVisibilityState::Maximized: return "Maximized";
        case flowspace::core::WindowVisibilityState::HiddenBySystem: return "HiddenBySystem";
        case flowspace::core::WindowVisibilityState::CloakedByDwm: return "CloakedByDwm";
        case flowspace::core::WindowVisibilityState::HiddenByFlowSpace: return "HiddenByFlowSpace";
        default: return "Unknown";
        }
    }
}

int main()
{
    InitializeDiagnosticLogging();

    flowspace::platform::MonitorManager monitorManager;
    flowspace::platform::WindowManager windowManager(monitorManager);

    const auto monitors = monitorManager.EnumerateMonitors();
    spdlog::info("=== Monitors ({} detected) ===", monitors.size());
    for (const auto &mon : monitors)
    {
        spdlog::info("  {} | {} | {} | bounds=({},{})-({},{}) | primary={} dpi={}",
                     flowspace::core::WideToUtf8(mon.stableId),
                     flowspace::core::WideToUtf8(mon.deviceName),
                     flowspace::core::WideToUtf8(mon.friendlyName),
                     mon.bounds.left, mon.bounds.top, mon.bounds.right, mon.bounds.bottom,
                     mon.isPrimary, mon.dpiX);
    }

    const auto windows = windowManager.DiscoverWindows();
    spdlog::info("=== Manageable windows ({} detected) ===", windows.size());
    for (const auto &win : windows)
    {
        std::string reason;
        if (!windowManager.IsManageableWindow(win.hwnd, &reason))
        {
            spdlog::info("  [excluded: {}] HWND=0x{:08X} '{}'", reason,
                         reinterpret_cast<uintptr_t>(win.hwnd), flowspace::core::WideToUtf8(win.title));
            continue;
        }
        spdlog::info("  HWND=0x{:08X} '{}' class='{}' exe='{}' monitor='{}' state={}",
                     reinterpret_cast<uintptr_t>(win.hwnd),
                     flowspace::core::WideToUtf8(win.title),
                     flowspace::core::WideToUtf8(win.className),
                     flowspace::core::WideToUtf8(win.executableName),
                     flowspace::core::WideToUtf8(win.monitorStableId),
                     VisibilityStateToString(win.visibilityState));
    }

    spdlog::info("Diagnostic complete. No state was modified.");
    spdlog::shutdown();
    return 0;
}
