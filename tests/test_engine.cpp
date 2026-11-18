// FlowSpace engine integration tests. Drives the real WorkspaceEngine against real top-level
// windows owned by child processes (this same exe started with --child-window). A window
// filter restricts the engine to those child windows, so the user's own windows are never
// hidden. Wallpaper management and hotkeys are disabled; config and journal live in %TEMP%.
//
// Run: build/cmake/flowspace_engine_tests.exe (exit code 0 = all pass).

#include "core/ConfigService.h"
#include "core/RecoveryManager.h"
#include "core/WorkspaceManager.h"
#include "engine/WorkspaceEngine.h"
#include "platform/HotkeyManager.h"
#include "platform/MonitorManager.h"
#include "platform/WallpaperManager.h"
#include "platform/WindowManager.h"

#include <Windows.h>
#include <objbase.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace flowspace;

namespace
{
    int g_failures = 0;
    int g_checks = 0;

    void Report(bool ok, const std::string &what, const char *file, int line)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_failures;
            std::printf("FAIL %s:%d  %s\n", file, line, what.c_str());
        }
    }
#define CHECK(cond) Report((cond), #cond, __FILE__, __LINE__)

    constexpr wchar_t kChildClass[] = L"FlowSpaceEngineTestChild";

    // ------------------------------------------------------------------ child process
    int RunChildWindow()
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT
        {
            if (m == WM_DESTROY)
            {
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(h, m, w, l);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kChildClass;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);

        POINT origin{0, 0};
        const HMONITOR primary = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(primary, &mi);
        const int x = mi.rcWork.left + 40 + static_cast<int>(GetCurrentProcessId() % 7) * 30;
        const int y = mi.rcWork.top + 40;
        HWND hwnd = CreateWindowExW(0, kChildClass, L"FlowSpace engine test window", WS_OVERLAPPEDWINDOW,
                                    x, y, 320, 200, nullptr, nullptr, wc.hInstance, nullptr);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return 0;
    }

    // ------------------------------------------------------------------ helpers
    void Pump(int ms)
    {
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(ms);
        do
        {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(10);
        } while (GetTickCount64() < end);
    }

    bool WaitFor(const std::function<bool()> &pred, int timeoutMs = 3000)
    {
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);
        while (GetTickCount64() < end)
        {
            if (pred())
            {
                return true;
            }
            Pump(20);
        }
        return pred();
    }

    struct Child
    {
        PROCESS_INFORMATION pi{};
        HWND hwnd{nullptr};
    };

    HANDLE g_job = nullptr; // kill-on-close: no child outlives the test run

    Child SpawnChild()
    {
        Child child;
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --child-window";
        STARTUPINFOW si{sizeof(si)};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &child.pi))
        {
            return child;
        }
        AssignProcessToJobObject(g_job, child.pi.hProcess);
        WaitFor([&]
                {
                    struct Ctx { DWORD pid; HWND found; } ctx{child.pi.dwProcessId, nullptr};
                    EnumWindows([](HWND h, LPARAM l) -> BOOL
                                {
                                    auto *c = reinterpret_cast<Ctx *>(l);
                                    DWORD pid = 0;
                                    GetWindowThreadProcessId(h, &pid);
                                    if (pid == c->pid && IsWindowVisible(h))
                                    {
                                        c->found = h;
                                        return FALSE;
                                    }
                                    return TRUE;
                                },
                                reinterpret_cast<LPARAM>(&ctx));
                    child.hwnd = ctx.found;
                    return child.hwnd != nullptr;
                },
                5000);
        return child;
    }

    void KillChild(Child &child)
    {
        if (child.pi.hProcess)
        {
            if (child.hwnd && IsWindow(child.hwnd))
            {
                PostMessageW(child.hwnd, WM_CLOSE, 0, 0);
            }
            if (WaitForSingleObject(child.pi.hProcess, 3000) != WAIT_OBJECT_0)
            {
                TerminateProcess(child.pi.hProcess, 1);
            }
            CloseHandle(child.pi.hProcess);
            CloseHandle(child.pi.hThread);
            child.pi = {};
        }
    }

    bool Visible(HWND h) { return IsWindowVisible(h) != FALSE; }

    struct TempDir
    {
        std::wstring path;
        explicit TempDir(const std::wstring &tag)
        {
            wchar_t base[MAX_PATH]{};
            GetTempPathW(MAX_PATH, base);
            path = std::wstring(base) + L"flowspace-engine-" + tag + L"-" + std::to_wstring(GetCurrentProcessId());
            std::error_code ec;
            fs::remove_all(path, ec);
            fs::create_directories(path);
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
        std::wstring File(const wchar_t *name) const { return path + L"\\" + name; }
    };

    LRESULT CALLBACK MsgProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

    // A complete engine stack wired like main.cpp, sandboxed to temp files and child windows.
    struct Harness
    {
        TempDir dir;
        platform::MonitorManager monitors;
        platform::WindowManager windows{monitors};
        core::WorkspaceManager workspaces;
        std::unique_ptr<core::RecoveryManager> recovery;
        platform::WallpaperManager wallpaper;
        platform::HotkeyManager hotkeys{7000};
        std::unique_ptr<core::ConfigService> config;
        std::unique_ptr<engine::WorkspaceEngine> engine;
        std::set<HWND> allowed;
        HWND msgWindow{nullptr};
        std::wstring primary;

        explicit Harness(const std::wstring &tag, const std::function<void(core::Config &)> &seed = {})
            : dir(tag)
        {
            // Seed a config file on disk (as a previous session would have left it).
            {
                core::ConfigService seedCfg(ConfigPath(), BackupPath());
                seedCfg.Load();
                Sandbox(seedCfg.Get());
                if (seed)
                {
                    seed(seedCfg.Get());
                }
                seedCfg.Save();
            }
            recovery = std::make_unique<core::RecoveryManager>(dir.File(L"journal.json"));
            recovery->Load();
            config = std::make_unique<core::ConfigService>(ConfigPath(), BackupPath());
            config->Load();
            engine = std::make_unique<engine::WorkspaceEngine>(monitors, windows, workspaces, *config,
                                                               *recovery, wallpaper, hotkeys);
            engine->SetWindowFilter([this](HWND h) { return allowed.count(h) != 0; });

            WNDCLASSW wc{};
            wc.lpfnWndProc = MsgProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"FlowSpaceEngineTestMsg";
            RegisterClassW(&wc);
            msgWindow = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
            engine->Attach(msgWindow);

            for (const auto &m : monitors.EnumerateMonitors())
            {
                if (m.isPrimary)
                {
                    primary = m.stableId;
                }
            }
        }

        ~Harness()
        {
            engine->RevealAllWindows(); // never leave a test window hidden
            DestroyWindow(msgWindow);
        }

        static void Sandbox(core::Config &c)
        {
            c.settings.wallpaperManagementEnabled = false;
            c.settings.startWithWindows = false;
            c.settings.showOverlay = false;
            c.settings.groupWindowsByApplication = false;
            c.hotkeys.clear();
        }

        std::wstring ConfigPath() const { return dir.File(L"config.json"); }
        std::wstring BackupPath() const { return dir.File(L"config.backup.json"); }

        Child Spawn()
        {
            Child c = SpawnChild();
            if (c.hwnd)
            {
                allowed.insert(c.hwnd);
            }
            return c;
        }

        std::string WsId(size_t i) const { return engine->GetWorkspaces().at(i).id; }

        std::string Assigned(HWND h) const
        {
            const auto w = workspaces.GetWindow(h);
            return w ? w->assignedWorkspaceId : std::string{"<untracked>"};
        }
    };

    std::vector<std::string> SavedWorkspaceIds(const std::wstring &configPath, const std::wstring &backupPath)
    {
        core::ConfigService reread(configPath, backupPath);
        reread.Load();
        std::vector<std::string> ids;
        for (const auto &ws : reread.Get().workspaces)
        {
            ids.push_back(ws.id);
        }
        return ids;
    }

    // ------------------------------------------------------------------ C1: persistence
    void TestWorkspacesLoadAndPersist()
    {
        std::printf("- workspace persistence\n");
        Harness h(L"persist", [](core::Config &c)
                  {
                      c.workspaces = {
                          core::Workspace{.id = "ws-a", .name = "Alpha", .orderIndex = 0},
                          core::Workspace{.id = "ws-b", .name = "Beta", .orderIndex = 1},
                      };
                  });
        h.engine->Initialize();

        const auto loaded = h.engine->GetWorkspaces();
        CHECK(loaded.size() == 2);
        CHECK(!loaded.empty() && loaded[0].name == "Alpha");

        h.engine->CreateWorkspace("Gamma");
        h.engine->RenameWorkspaceIndex(0, "Alpha renamed");
        h.engine->WorkspaceAt(1).accentColor = "#E5534B"; // in-place UI edit path
        h.config->Save();

        core::ConfigService reread(h.ConfigPath(), h.BackupPath());
        reread.Load();
        const auto &saved = reread.Get().workspaces;
        auto has = [&](const std::string &name)
        { return std::any_of(saved.begin(), saved.end(), [&](const auto &w) { return w.name == name; }); };
        CHECK(has("Gamma"));
        CHECK(has("Alpha renamed"));
        CHECK(std::any_of(saved.begin(), saved.end(), [](const auto &w) { return w.id == "ws-b" && w.accentColor == "#E5534B"; }));
    }

    // ------------------------------------------------------------------ H5: unique ids
    void TestWorkspaceIdsStayUnique()
    {
        std::printf("- workspace ids unique across restarts\n");
        Harness h(L"ids", [](core::Config &c)
                  {
                      c.workspaces = {
                          core::Workspace{.id = "ws-100", .name = "Created last session", .orderIndex = 0},
                          core::Workspace{.id = "ws-900", .name = "Duplicated last session", .orderIndex = 1},
                      };
                  });
        h.engine->Initialize();
        const std::string created = h.engine->CreateWorkspace("New");
        h.engine->DuplicateWorkspaceIndex(1);
        h.config->Save();

        const auto ids = SavedWorkspaceIds(h.ConfigPath(), h.BackupPath());
        CHECK(created != "ws-100" && created != "ws-900");
        CHECK(ids.size() == 4); // nothing silently dropped as a "duplicate"
        CHECK(std::set<std::string>(ids.begin(), ids.end()).size() == ids.size());
    }

    // ------------------------------------------------------------------ C2: reveal all
    void TestRevealAllRestoresEveryWindow()
    {
        std::printf("- reveal all restores every hidden window\n");
        Harness h(L"reveal");
        h.engine->Initialize();
        std::vector<Child> kids;
        for (int i = 0; i < 4; ++i)
        {
            kids.push_back(h.Spawn());
        }
        CHECK(std::all_of(kids.begin(), kids.end(), [](const Child &c) { return c.hwnd != nullptr; }));

        h.engine->SwitchMonitorToWorkspace(h.primary, h.WsId(0)); // track on ws-1
        h.engine->SwitchMonitorToWorkspace(h.primary, h.WsId(1)); // hide them all
        CHECK(WaitFor([&] { return std::none_of(kids.begin(), kids.end(), [](const Child &c) { return Visible(c.hwnd); }); }));
        CHECK(h.recovery->Count() == kids.size());

        h.engine->RevealAllWindows();
        CHECK(WaitFor([&] { return std::all_of(kids.begin(), kids.end(), [](const Child &c) { return Visible(c.hwnd); }); }));
        CHECK(h.recovery->Count() == 0);
        for (auto &k : kids)
        {
            KillChild(k);
        }
    }

    // ------------------------------------------------------------------ C3: move untracked
    void TestMoveUntrackedWindow()
    {
        std::printf("- move a window opened after the last switch\n");
        Harness h(L"move");
        h.engine->Initialize();
        h.engine->SwitchMonitorToWorkspace(h.primary, h.WsId(0));

        Child late = h.Spawn(); // opened after the last switch: not tracked yet
        CHECK(late.hwnd != nullptr);
        CHECK(h.engine->MoveWindowToWorkspace(late.hwnd, h.WsId(1)));
        CHECK(h.Assigned(late.hwnd) == h.WsId(1));
        CHECK(WaitFor([&] { return !Visible(late.hwnd); }));

        h.engine->SwitchMonitorToWorkspace(h.primary, h.WsId(1));
        CHECK(WaitFor([&] { return Visible(late.hwnd); }));

        // Moving to the workspace already on screen must not hide the window.
        Child other = h.Spawn();
        CHECK(h.engine->MoveWindowToWorkspace(other.hwnd, h.WsId(1)));
        Pump(300);
        CHECK(Visible(other.hwnd));
        KillChild(late);
        KillChild(other);
    }

    // ------------------------------------------------------------------ H2: app rules
    void TestAppRuleAppliesToNewWindow()
    {
        std::printf("- app rule sends a new window to its workspace\n");
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const std::wstring exeName = fs::path(exe).filename().wstring();
        Harness h(L"rules", [&](core::Config &c)
                  {
                      c.appRules.push_back(core::AppRule{.id = "rule-test", .enabled = true, .exeName = exeName,
                                                         .targetWorkspaceId = "ws-2"});
                  });
        h.engine->Initialize();
        h.engine->SwitchMonitorToWorkspace(h.primary, "ws-1");

        Child c = h.Spawn();
        h.engine->HandleWindowEvent(engine::WindowEventType::Shown, c.hwnd);
        CHECK(h.Assigned(c.hwnd) == "ws-2");
        CHECK(WaitFor([&] { return !Visible(c.hwnd); }));

        // "Add rule" must target the last external foreground window, not FlowSpace itself.
        Child fg = h.Spawn();
        h.engine->HandleWindowEvent(engine::WindowEventType::Foreground, fg.hwnd);
        CHECK(h.engine->GetLastExternalForegroundWindow().hwnd == fg.hwnd);
        KillChild(c);
        KillChild(fg);
    }

    // ------------------------------------------------------------------ H3: destroyed
    void TestDestroyedWindowIsForgotten()
    {
        std::printf("- destroyed windows are forgotten\n");
        Harness h(L"destroy");
        h.engine->Initialize();
        Child c = h.Spawn();
        h.engine->SwitchMonitorToWorkspace(h.primary, h.WsId(0));
        CHECK(h.workspaces.GetWindow(c.hwnd).has_value());
        const HWND gone = c.hwnd;
        KillChild(c);
        CHECK(!IsWindow(gone));
        h.engine->HandleWindowEvent(engine::WindowEventType::Destroyed, gone);
        CHECK(!h.workspaces.GetWindow(gone).has_value());
    }

    // ------------------------------------------------------------------ H4: monitor removed
    void TestDisplayRemovalRevealsStrandedWindows()
    {
        std::printf("- windows hidden on a removed monitor are revealed\n");
        Harness h(L"display");
        h.engine->Initialize();
        Child c = h.Spawn();
        h.engine->SwitchMonitorToWorkspace(h.primary, h.WsId(0));
        h.engine->SwitchMonitorToWorkspace(h.primary, h.WsId(1));
        CHECK(WaitFor([&] { return !Visible(c.hwnd); }));

        // Pretend the window was hidden on a monitor that has since been unplugged.
        h.workspaces.AssignWindowToWorkspace(c.hwnd, h.WsId(0), L"\\\\?\\DISPLAY#UNPLUGGED#0");
        h.engine->HandleDisplayChange();
        CHECK(WaitFor([&] { return Visible(c.hwnd); }));
        CHECK(h.recovery->Count() == 0);
        KillChild(c);
    }
}

int wmain(int argc, wchar_t **argv)
{
    if (argc > 1 && std::wstring(argv[1]) == L"--child-window")
    {
        return RunChildWindow();
    }

    std::printf("FlowSpace engine tests\n======================\n");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    TestWorkspacesLoadAndPersist();
    TestWorkspaceIdsStayUnique();
    TestRevealAllRestoresEveryWindow();
    TestMoveUntrackedWindow();
    TestAppRuleAppliesToNewWindow();
    TestDestroyedWindowIsForgotten();
    TestDisplayRemovalRevealsStrandedWindows();

    CloseHandle(g_job);
    CoUninitialize();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
