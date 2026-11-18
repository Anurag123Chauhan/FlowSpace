#include "core/ConfigService.h"
#include "core/RecoveryManager.h"
#include "core/StringUtils.h"
#include "core/WorkspaceManager.h"
#include "engine/WorkspaceEngine.h"
#include "platform/HotkeyManager.h"
#include "platform/MonitorManager.h"
#include "platform/StartupManager.h"
#include "platform/WallpaperManager.h"
#include "platform/WinEventHook.h"
#include "ui/MainWindow.h"
#include "ui/SwitchNotification.h"
#include "ui/SwitcherPopup.h"
#include "ui/TrayIcon.h"

#include <Windows.h>
#include <objbase.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <memory>
#include <string>

// GUI entry point. The Phase-1 diagnostic CLI lives separately in
// src/app/diagnostic_main.cpp (target: flowspace-diag, console subsystem).

namespace fs = std::filesystem;

namespace
{
    namespace core = flowspace::core;
    namespace engine = flowspace::engine;
    namespace platform = flowspace::platform;
    namespace ui = flowspace::ui;

    // --- constants ---------------------------------------------------------------
    constexpr wchar_t kMutexName[] = L"Local\\FlowSpaceSingleInstance";
    constexpr wchar_t kMessageClassName[] = L"FlowSpaceMessageWindow";
    constexpr UINT WM_APP_TRAY = WM_APP + 1; // tray icon callback message
    constexpr UINT kTrayMenuBase = 5000;     // settings/reveal/pause/exit at +0..+3
    // workspace items: kTrayMenuBase + 100 + monitorIndex*25 + workspaceIndex

    std::wstring ExpandEnv(const wchar_t *text)
    {
        wchar_t buffer[MAX_PATH]{};
        ExpandEnvironmentStringsW(text, buffer, MAX_PATH);
        return buffer;
    }

    void InitializeLogging()
    {
        const std::wstring logDir = ExpandEnv(L"%LOCALAPPDATA%\\FlowSpace\\logs");
        std::error_code ec;
        fs::create_directories(fs::path(logDir), ec);
        const std::string logFile = flowspace::core::WideToUtf8(logDir) + "\\flowspace.log";
        try
        {
            auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, /*truncate=*/true);
            fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            auto logger = std::make_shared<spdlog::logger>("flowspace", spdlog::sinks_init_list{fileSink});
            logger->set_level(spdlog::level::debug);
            spdlog::set_default_logger(logger);
            spdlog::flush_on(spdlog::level::info);
        }
        catch (const std::exception &)
        {
            // Logging is best-effort; the app still runs without it.
        }
    }

    void EnablePerMonitorDpiAwareness()
    {
        // The manifest requests PerMonitorV2; this call is a fallback for a missing
        // manifest. Failure (already set by the manifest) is tolerated.
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (DPI_CONTEXT_HANDLE)-4
        using Fn = BOOL(WINAPI *)(HANDLE);
        if (const HMODULE user32 = GetModuleHandleW(L"user32.dll"))
        {
            if (const auto fn = reinterpret_cast<Fn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")))
            {
                (void)fn(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
            }
        }
    }

    // --- application context -----------------------------------------------------
    struct AppContext
    {
        HINSTANCE hinstance = nullptr;
        HWND messageWindow = nullptr;

        std::wstring configPath;
        std::wstring backupPath;
        std::wstring journalPath;

        // Managers (construction order matters: windows references monitors).
        flowspace::platform::MonitorManager monitors;
        flowspace::platform::WindowManager windows{monitors};
        flowspace::core::WorkspaceManager workspaces;
        std::unique_ptr<flowspace::core::RecoveryManager> recovery;
        flowspace::platform::WallpaperManager wallpaper;
        flowspace::platform::HotkeyManager hotkeys{1};
        std::unique_ptr<flowspace::core::ConfigService> config;
        std::unique_ptr<flowspace::engine::WorkspaceEngine> engine;
        flowspace::platform::StartupManager startup{L"FlowSpace"};
        flowspace::platform::WinEventHook hooks;
        flowspace::ui::TrayIcon tray;

        flowspace::ui::MainWindow::Context MainWindowContext()
        {
            flowspace::ui::MainWindow::Context ctx;
            ctx.engine = engine.get();
            ctx.config = config.get();
            ctx.recovery = recovery.get();
            ctx.monitors = &monitors;
            ctx.wallpaper = &wallpaper;
            ctx.startup = &startup;
            ctx.onConfigSaved = [this] { OnConfigSaved(); };
            return ctx;
        }

        void ApplyStartupEntry()
        {
            const bool want = config->Get().settings.startWithWindows;
            const bool registered = startup.IsRegistered();
            if (want && !registered)
            {
                startup.Enable();
            }
            else if (!want && registered)
            {
                startup.Disable();
            }
        }

        void OnConfigSaved()
        {
            // Re-apply everything derived from settings after a UI change.
            engine->ApplyHotkeys();
            ApplyStartupEntry();
            tray.Update(config->Get().settings.paused);
        }

        void BuildTrayMenu(HMENU menu)
        {
            // Per-monitor workspace switching section.
            const auto monitorsList = engine->GetMonitors();
            const auto workspaceList = engine->GetWorkspaces();
            for (size_t m = 0; m < monitorsList.size() && m < 8; ++m)
            {
                std::wstring label = monitorsList[m].isPrimary ? L"Primary: " : L"Monitor: ";
                label += monitorsList[m].friendlyName.empty() ? L"Display" : monitorsList[m].friendlyName;

                HMENU sub = CreatePopupMenu();
                for (size_t w = 0; w < workspaceList.size() && w < 20; ++w)
                {
                    const UINT id = kTrayMenuBase + 100 + static_cast<UINT>(m) * 25 + static_cast<UINT>(w);
                    std::wstring text = std::to_wstring(w + 1) + L"  " + flowspace::core::Utf8ToWide(workspaceList[w].name);
                    const UINT flags = MF_STRING
                                     | (workspaceList[w].id == monitorsList[m].activeWorkspaceId ? MF_CHECKED : 0);
                    AppendMenuW(sub, flags, id, text.c_str());
                }
                AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), label.c_str());
            }
        }

        void HandleTrayCommand(int cmd)
        {
            if (cmd == kTrayMenuBase + 0)
            {
                flowspace::ui::MainWindow::Open(hinstance, MainWindowContext());
            }
            else if (cmd == kTrayMenuBase + 1)
            {
                engine->RevealAllWindows();
            }
            else if (cmd == kTrayMenuBase + 2)
            {
                config->Get().settings.paused = !config->Get().settings.paused;
                config->Save();
                tray.Update(config->Get().settings.paused);
            }
            else if (cmd == kTrayMenuBase + 3)
            {
                DestroyWindow(messageWindow);
            }
            else if (cmd >= kTrayMenuBase + 100)
            {
                const int offset = cmd - (kTrayMenuBase + 100);
                engine->SwitchMonitorToWorkspaceIndex(offset / 25, offset % 25);
            }
        }

        void OpenSwitcherPopup()
        {
            const std::wstring monitorId = engine->ResolveActionMonitor();
            const auto monitorsList = engine->GetMonitors();
            const auto workspaceList = engine->GetWorkspaces();
            if (workspaceList.empty())
            {
                return;
            }
            const core::MonitorInfo *target = nullptr;
            for (const auto &mon : monitorsList)
            {
                if (mon.stableId == monitorId)
                {
                    target = &mon;
                    break;
                }
            }
            if (!target)
            {
                return;
            }

            std::vector<flowspace::ui::SwitcherPopup::Item> items;
            for (size_t i = 0; i < workspaceList.size(); ++i)
            {
                flowspace::ui::SwitcherPopup::Item item;
                item.workspace = workspaceList[i];
                item.number = i < 9 ? static_cast<int>(i) + 1 : 0;
                item.active = workspaceList[i].id == target->activeWorkspaceId;
                items.push_back(std::move(item));
            }

            const int selection = flowspace::ui::SwitcherPopup::Show(hinstance, *target, items);
            if (selection >= 1 && selection <= static_cast<int>(workspaceList.size()))
            {
                engine->SwitchByNumber(monitorId, selection);
            }
        }

        void HandleHotkey(WPARAM hotkeyId)
        {
            if (engine->HandleHotkey(static_cast<int>(hotkeyId)))
            {
                return; // engine handled it
            }
            // UI actions the engine leaves to the app.
            const auto resolved = hotkeys.Resolve(static_cast<int>(hotkeyId));
            if (!resolved.has_value())
            {
                return;
            }
            switch (resolved->first)
            {
            case flowspace::core::HotkeyAction::OpenWorkspaceSwitcher:
                OpenSwitcherPopup();
                break;
            case flowspace::core::HotkeyAction::OpenSettings:
                flowspace::ui::MainWindow::Open(hinstance, MainWindowContext());
                break;
            default:
                break;
            }
        }

        static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            auto *app = reinterpret_cast<AppContext *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg)
            {
            case WM_CREATE:
            {
                auto create = reinterpret_cast<CREATESTRUCTW *>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
                return 0;
            }
            case WM_HOTKEY:
                if (app)
                {
                    app->HandleHotkey(wParam);
                }
                return 0;
            case WM_DISPLAYCHANGE:
                if (app)
                {
                    app->engine->HandleDisplayChange();
                }
                return 0;
            case WM_APP_TRAY:
                if (app)
                {
                    const int cmd = app->tray.HandleCallbackMessage(lParam);
                    if (cmd == -1)
                    {
                        flowspace::ui::MainWindow::Open(app->hinstance, app->MainWindowContext());
                    }
                    else if (cmd > 0)
                    {
                        app->HandleTrayCommand(cmd);
                    }
                }
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        // One-time initialization after the message window exists.
        bool Initialize()
        {
            config->Load();

            engine->Attach(messageWindow);
            engine->OnOverlayShow = [this](const core::Workspace &ws, const std::wstring &monitorStableId)
            {
                if (!config->Get().settings.showOverlay)
                {
                    return;
                }
                const auto monitorsList = monitors.EnumerateMonitors();
                for (const auto &mon : monitorsList)
                {
                    if (mon.stableId == monitorStableId)
                    {
                        const auto all = engine->GetWorkspaces();
                        int number = 0;
                        for (size_t i = 0; i < all.size(); ++i)
                        {
                            if (all[i].id == ws.id)
                            {
                                number = static_cast<int>(i) + 1;
                                break;
                            }
                        }
                        flowspace::ui::SwitchNotification::Show(hinstance, mon, ws, number,
                                                                config->Get().settings.overlayAnimation,
                                                                config->Get().settings.overlayAnimationMs);
                        return;
                    }
                }
            };
            engine->OnStateChanged = [this]
            {
                if (auto *window = ui::MainWindow::Instance())
                {
                    window->NotifyStateChanged();
                }
                tray.Update(config->Get().settings.paused);
            };

            engine->Initialize();
            ApplyStartupEntry();

            tray.Initialize(messageWindow, WM_APP_TRAY, kTrayMenuBase);
            tray.SetWorkspaceMenuBuilder([this](HMENU menu) { BuildTrayMenu(menu); });
            tray.Update(config->Get().settings.paused);

            // WinEvent hooks: callbacks are delivered on this thread via the message loop,
            // serialized with all other engine work.
            const DWORD threadId = GetCurrentThreadId();
            hooks.Install(threadId, [this](DWORD event, HWND hwnd, LONG, LONG)
            {
                using ET = flowspace::engine::WindowEventType;
                switch (event)
                {
                case EVENT_OBJECT_CREATE:    engine->HandleWindowEvent(ET::Created, hwnd); break;
                case EVENT_OBJECT_SHOW:      engine->HandleWindowEvent(ET::Shown, hwnd); break;
                case EVENT_OBJECT_HIDE:      engine->HandleWindowEvent(ET::Hidden, hwnd); break;
                case EVENT_OBJECT_DESTROY:   engine->HandleWindowEvent(ET::Destroyed, hwnd); break;
                case EVENT_SYSTEM_MOVESIZEEND: engine->HandleWindowEvent(ET::MoveSizeEnd, hwnd); break;
                case EVENT_SYSTEM_FOREGROUND:  engine->HandleWindowEvent(ET::Foreground, hwnd); break;
                default: break;
                }
            });

            // Report conflicts found during hotkey registration (also visible in the UI page).
            return true;
        }

        // Graceful shutdown: unregister hotkeys, reveal hidden windows, restore wallpapers.
        void Shutdown()
        {
            hooks.Uninstall();
            tray.Shutdown();
            const bool restore = config->Get().settings.restoreWallpaperOnExit;
            engine->Shutdown(restore);
        }
    };
}

int wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    (void)lpCmdLine;
    (void)nCmdShow;

    // Single instance guard.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mutex)
        {
            CloseHandle(mutex);
        }
        MessageBoxW(nullptr, L"FlowSpace is already running.", L"FlowSpace", MB_ICONINFORMATION);
        return 0;
    }

    InitializeLogging();
    EnablePerMonitorDpiAwareness();

    // COM (STA) for IDesktopWallpaper and the shell APIs; required before WallpaperManager.
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(comHr))
    {
        MessageBoxW(nullptr, L"COM initialization failed.", L"FlowSpace", MB_ICONERROR);
        return 1;
    }

    const std::wstring dataDir = ExpandEnv(L"%LOCALAPPDATA%\\FlowSpace");
    std::error_code ec;
    fs::create_directories(fs::path(dataDir), ec);

    AppContext app;
    app.hinstance = hInstance;
    app.configPath = dataDir + L"\\config.json";
    app.backupPath = dataDir + L"\\config.backup.json";
    app.journalPath = dataDir + L"\\journal.json";
    app.recovery = std::make_unique<flowspace::core::RecoveryManager>(app.journalPath);
    app.config = std::make_unique<flowspace::core::ConfigService>(app.configPath, app.backupPath);
    app.engine = std::make_unique<flowspace::engine::WorkspaceEngine>(
        app.monitors, app.windows, app.workspaces, *app.config, *app.recovery,
        app.wallpaper, app.hotkeys);

    // UI classes.
    flowspace::ui::RegisterSwitchNotificationClass(hInstance);
    flowspace::ui::RegisterSwitcherClass(hInstance);
    flowspace::ui::MainWindow::RegisterMainWindowClass(hInstance);

    // Hidden message window: receives WM_HOTKEY, WM_DISPLAYCHANGE and the tray callback.
    WNDCLASSW wc{};
    wc.lpfnWndProc = &AppContext::MessageWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kMessageClassName;
    RegisterClassW(&wc);

    app.messageWindow = CreateWindowExW(0, kMessageClassName, L"FlowSpace", WS_OVERLAPPED,
                                        0, 0, 0, 0, nullptr, nullptr, hInstance, &app);
    if (!app.messageWindow)
    {
        spdlog::error("Failed to create the message window (GetLastError={})", GetLastError());
        CoUninitialize();
        ReleaseMutex(mutex);
        return 1;
    }

    if (!app.Initialize())
    {
        spdlog::error("Engine initialization failed; exiting.");
        app.Shutdown();
        DestroyWindow(app.messageWindow);
        CoUninitialize();
        ReleaseMutex(mutex);
        return 1;
    }

    spdlog::info("FlowSpace started.");

    // Standard message loop; everything (hotkeys, hooks, tray, UI) runs on this thread.
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    app.Shutdown();
    spdlog::info("FlowSpace stopped.");
    spdlog::shutdown();

    CoUninitialize();
    if (mutex)
    {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return static_cast<int>(msg.wParam);
}
