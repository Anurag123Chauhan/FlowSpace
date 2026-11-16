// FlowSpace core-logic tests. Self-contained mini assert harness (no external framework),
// per the project's vendored-deps-only constraint. Covers:
//   - StringUtils: wide<->utf8 round trip, hex color parsing/formatting
//   - ConfigService: fresh defaults, save/load round trip, corrupt-file backup recovery,
//     v0->v1 migration, normalize (duplicate ids, order renumbering, invalid colors)
//   - RecoveryManager: record/hide->show round trip, save/load persistence
//   - WorkspaceManager: CRUD, duplicate, reset, reorder, adjacency, window assignment
//   - HotkeyManager: modifier/key parsing, name round trip, conflict detection
//
// Run: build/cmake/flowspace_tests.exe (exit code 0 = all pass).

#include "core/ConfigService.h"
#include "core/RecoveryManager.h"
#include "core/StringUtils.h"
#include "core/WorkspaceManager.h"
#include "platform/HotkeyManager.h"

#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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
#define CHECK_EQ(a, b) Report(((a) == (b)), std::string(#a " == " #b), __FILE__, __LINE__)

    // Isolated temp directory per test group.
    struct TempDir
    {
        std::wstring path;
        explicit TempDir(const std::string &tag)
        {
            wchar_t base[MAX_PATH]{};
            GetTempPathW(MAX_PATH, base);
            path = std::wstring(base) + L"flowspace-tests-" + std::wstring(tag.begin(), tag.end()) + L"-" + std::to_wstring(GetCurrentProcessId());
            fs::remove_all(fs::path(path), ec);
            fs::create_directories(fs::path(path));
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(fs::path(path), ec);
        }
        std::wstring File(const wchar_t *name) const { return path + L"\\" + name; }
        std::error_code ec;
    };

    // ---------------------------------------------------------------- StringUtils
    void TestStringUtils()
    {
        // Round trips (ASCII, Greek, CJK, emoji).
        for (const std::wstring &w : {L"hello", L"Development", L"Καλημέρα", L"工作区", L"workspace 🎉"})
        {
            CHECK_EQ(flowspace::core::Utf8ToWide(flowspace::core::WideToUtf8(w)), w);
            CHECK_EQ(flowspace::core::WideToUtf8(flowspace::core::Utf8ToWide(flowspace::core::WideToUtf8(w))),
                     flowspace::core::WideToUtf8(w));
        }
        CHECK(flowspace::core::WideToUtf8(L"").empty());
        CHECK(flowspace::core::Utf8ToWide("").empty());

        // Hex colors: RGB.
        uint32_t rgb = 0;
        CHECK(flowspace::core::TryParseHexColor(L"#4389D8", rgb));
        CHECK_EQ(rgb, 0x4389D8);
        CHECK(flowspace::core::TryParseHexColor(L"4389d8", rgb));
        CHECK_EQ(rgb, 0x4389D8);
        CHECK(flowspace::core::TryParseHexColor(L"#FF4389D8", rgb)); // ARGB accepted, alpha dropped
        CHECK_EQ(rgb, 0x4389D8);
        uint32_t bad = 0x123456;
        CHECK(!flowspace::core::TryParseHexColor(L"#12345", bad));
        CHECK(!flowspace::core::TryParseHexColor(L"#GGGGGG", bad));
        CHECK(!flowspace::core::TryParseHexColor(L"", bad));
        CHECK_EQ(bad, 0x123456); // untouched on failure

        // Hex colors: ARGB.
        uint32_t argb = 0;
        CHECK(flowspace::core::TryParseHexColor8(L"#FF4389D8", argb));
        CHECK_EQ(argb, 0xFF4389D8);
        CHECK(flowspace::core::TryParseHexColor8(L"#4389D8", argb)); // 6-digit defaults to opaque
        CHECK_EQ(argb, 0xFF4389D8);

        // Formatting.
        CHECK_EQ(flowspace::core::FormatHexColor(0x4389D8), std::wstring(L"#4389D8"));
        CHECK_EQ(flowspace::core::FormatHexColor8(0xFF4389D8), std::wstring(L"#FF4389D8"));
    }

    // -------------------------------------------------------------- ConfigService
    void TestConfigFreshDefaults()
    {
        TempDir dir("fresh");
        flowspace::core::ConfigService config(dir.File(L"config.json"), dir.File(L"config.backup.json"));
        const auto status = config.Load();
        CHECK(status == flowspace::core::ConfigLoadStatus::FreshDefaults);
        const auto &c = config.Get();
        CHECK(c.workspaces.size() >= 4);
        CHECK(c.hotkeys.size() >= 6);
        CHECK(c.schemaVersion == 1);
        for (size_t i = 0; i < c.workspaces.size(); ++i)
        {
            CHECK_EQ(c.workspaces[i].orderIndex, static_cast<int32_t>(i));
        }
    }

    void TestConfigRoundTrip()
    {
        TempDir dir("roundtrip");
        {
            flowspace::core::ConfigService config(dir.File(L"config.json"), dir.File(L"config.backup.json"));
            config.Load();
            config.Get().workspaces.push_back(flowspace::core::Workspace{
                .id = "ws-custom", .name = "Καλημέρα", .icon = "gamepad",
                .accentColor = "#E5534B", .orderIndex = 90});
            config.Get().settings.hideMode = flowspace::core::HideMode::Minimize;
            config.Get().settings.overlayAnimationMs = 250;
            config.Get().appRules.push_back(flowspace::core::AppRule{
                .id = "rule-1", .enabled = true, .exeName = L"notepad.exe",
                .targetWorkspaceId = "ws-1"});
            config.Get().monitorState[L"\\\\?\\DISPLAY#TEST#1"] = "ws-1";
            CHECK(config.Save());
        }
        {
            flowspace::core::ConfigService config(dir.File(L"config.json"), dir.File(L"config.backup.json"));
            const auto status = config.Load();
            CHECK(status == flowspace::core::ConfigLoadStatus::Loaded);
            const auto &c = config.Get();
            const bool foundCustom = std::any_of(c.workspaces.begin(), c.workspaces.end(),
                                                 [](const auto &ws)
                                                 { return ws.id == "ws-custom" && ws.name == "Καλημέρα"
                                                              && ws.accentColor == "#E5534B"; });
            CHECK(foundCustom);
            CHECK(c.settings.hideMode == flowspace::core::HideMode::Minimize);
            CHECK(c.settings.overlayAnimationMs == 250);
            CHECK(c.appRules.size() == 1 && c.appRules[0].exeName == L"notepad.exe");
            CHECK(c.monitorState.count(L"\\\\?\\DISPLAY#TEST#1") == 1);
        }
    }

    void TestConfigCorruptRecovery()
    {
        TempDir dir("corrupt");
        // Write a valid config first (so a backup exists), then corrupt the main file.
        {
            flowspace::core::ConfigService config(dir.File(L"config.json"), dir.File(L"config.backup.json"));
            config.Load();
            // Save twice: the first save creates config.json, the second rotates it into the
            // backup (mirroring real usage, where the backup always lags one save cycle).
            config.Save(); // main = 180 (default), backup = none yet
            config.Get().settings.overlayAnimationMs = 999;
            CHECK(config.Save()); // main = 999, backup = 180 (previous state)
        }
        CHECK(fs::exists(fs::path(dir.File(L"config.backup.json"))));
        {   // Corrupt main: truncated garbage.
            FILE *f = _wfopen(dir.File(L"config.json").c_str(), L"wb");
            fwprintf(f, L"{ \"schemaVersion\": 1, \"settings\": { \"overlayAnimationMs\": [");
            fclose(f);
        }
        flowspace::core::ConfigService config(dir.File(L"config.json"), dir.File(L"config.backup.json"));
        const auto status = config.Load();
        CHECK(status == flowspace::core::ConfigLoadStatus::RecoveredFromBackup);
        // The backup holds the state of the previous save cycle: 180, not 999.
        CHECK(config.Get().settings.overlayAnimationMs == 180);
    }

    void TestConfigAllCorruptFallsBackToDefaults()
    {
        TempDir dir("allcorrupt");
        FILE *f1 = _wfopen(dir.File(L"config.json").c_str(), L"wb");
        fwprintf(f1, L"not json at all");
        fclose(f1);
        FILE *f2 = _wfopen(dir.File(L"config.backup.json").c_str(), L"wb");
        fwprintf(f2, L"also not json");
        fclose(f2);
        flowspace::core::ConfigService config(dir.File(L"config.json"), dir.File(L"config.backup.json"));
        const auto status = config.Load();
        CHECK(status == flowspace::core::ConfigLoadStatus::FreshDefaults);
        CHECK(config.Get().workspaces.size() >= 4);
    }

    void TestConfigMigrationV0()
    {
        TempDir dir("migrate");
        // Legacy v0 config: no schemaVersion field, no hotkeys.
        const char *v0 =
            "{\n"
            "  \"settings\": {\"hideMode\": 1, \"wallpaperManagementEnabled\": true},\n"
            "  \"workspaces\": [\n"
            "    {\"id\": \"ws-1\", \"name\": \"Work\"},\n"
            "    {\"id\": \"ws-2\", \"name\": \"Home\"}\n"
            "  ]\n"
            "}\n";
        FILE *f = _wfopen(dir.File(L"config.json").c_str(), L"wb");
        fwrite(v0, 1, strlen(v0), f);
        fclose(f);

        flowspace::core::ConfigService config(dir.File(L"config.json"), dir.File(L"config.backup.json"));
        const auto status = config.Load();
        CHECK(status == flowspace::core::ConfigLoadStatus::Loaded);
        const auto &c = config.Get();
        CHECK(c.schemaVersion == 1); // migrated
        CHECK(c.workspaces.size() == 2);
        CHECK(c.hotkeys.size() >= 6); // defaults attached by migration
        CHECK(c.workspaces[0].name == "Work");
    }

    void TestConfigNormalize()
    {
        flowspace::core::ConfigService config(L"", L"");
        flowspace::core::Config c = config.Get(); // defaults
        // Inject duplicates + invalid accent + bad order.
        flowspace::core::Workspace a{.id = "dup", .name = "A", .accentColor = "#XYZXYZ", .orderIndex = 5};
        flowspace::core::Workspace b{.id = "dup", .name = "B", .orderIndex = 1};
        flowspace::core::Workspace noId{.name = "C", .orderIndex = 2};
        c.workspaces = {a, b, noId};
        c.monitorState[L"\\\\?\\MON"] = "ghost-workspace";
        c.hotkeys.clear(); // drop defaults: test only the injected empty-key binding
        c.hotkeys.push_back({flowspace::core::HotkeyAction::NextWorkspace, true, L"Ctrl+Alt", L""}); // empty key

        // Normalize runs inside Load(); simulate by saving through a service.
        TempDir dir("normalize");
        flowspace::core::ConfigService svc(dir.File(L"config.json"), dir.File(L"config.backup.json"));
        svc.Get() = c;
        svc.Save();
        svc.Load();

        const auto &fixed = svc.Get();
        // Exactly one "dup" survives; the id-less workspace gets one.
        CHECK(fixed.workspaces.size() == 2);
        CHECK(fixed.workspaces[0].accentColor == "#4389D8"); // invalid color repaired
        for (size_t i = 0; i < fixed.workspaces.size(); ++i)
        {
            CHECK_EQ(fixed.workspaces[i].orderIndex, static_cast<int32_t>(i));
        }
        CHECK(fixed.monitorState.empty()); // dangling reference dropped
        CHECK(fixed.hotkeys.empty());      // empty-key binding dropped
    }

    // ------------------------------------------------------------- RecoveryManager
    void TestRecoveryRoundTrip()
    {
        TempDir dir("recovery");
        flowspace::core::RecoveryManager recovery(dir.File(L"journal.json"));
        recovery.Load();
        CHECK(recovery.Count() == 0);

        flowspace::core::WindowInfo win;
        win.hwnd = reinterpret_cast<HWND>(0x1234);
        win.processId = 4242;
        win.executablePath = L"C:\\apps\\test.exe";
        win.executableName = L"test.exe";
        win.processCreationTime = 123456789;
        win.title = L"Test window";
        win.className = L"TestClass";
        win.monitorStableId = L"\\\\?\\DISPLAY#TEST#1";

        RECT restoreRect{10, 20, 810, 620};
        recovery.RecordHide(win, "ws-1", /*wasMinimized=*/true, /*wasMaximized=*/false, restoreRect);
        CHECK(recovery.Count() == 1);
        CHECK(fs::exists(fs::path(dir.File(L"journal.json"))));

        // Reload through a fresh manager: entry must survive.
        flowspace::core::RecoveryManager loaded(dir.File(L"journal.json"));
        loaded.Load();
        CHECK(loaded.Count() == 1);
        const auto &e = loaded.Entries().front();
        CHECK_EQ(e.hwnd, reinterpret_cast<HWND>(0x1234));
        CHECK_EQ(e.processId, 4242u);
        CHECK(e.executablePath == L"C:\\apps\\test.exe");
        CHECK_EQ(e.processCreationTime, 123456789ull);
        CHECK(e.title == L"Test window");
        CHECK(e.workspaceId == "ws-1");
        CHECK(e.wasMinimized);
        CHECK(!e.wasMaximized);
        CHECK(e.restoreRect.left == 10);
        CHECK(e.restoreRect.right == 810);
        CHECK(e.hiddenAtUnixMs > 0);

        // Show removes it.
        CHECK(loaded.RecordShow(e.hwnd));
        CHECK(loaded.Count() == 0);

        // Re-load the saved journal to verify the removal persisted.
        flowspace::core::RecoveryManager reloaded(dir.File(L"journal.json"));
        reloaded.Load();
        CHECK(reloaded.Count() == 0);
    }

    void TestRecoveryCorruptJournalIsFailSafe()
    {
        TempDir dir("recoverycorrupt");
        FILE *f = _wfopen(dir.File(L"journal.json").c_str(), L"wb");
        fwprintf(f, L"[{broken");
        fclose(f);
        flowspace::core::RecoveryManager recovery(dir.File(L"journal.json"));
        recovery.Load();
        // Fail-safe: corrupt journal = empty, no restoration attempted.
        CHECK(recovery.Count() == 0);
    }

    // ------------------------------------------------------------ WorkspaceManager
    void TestWorkspaceCrud()
    {
        flowspace::core::WorkspaceManager wm;
        wm.InitializeDefaultWorkspaces();
        const size_t initial = wm.GetWorkspaces().size();
        CHECK(initial >= 4);

        // Create.
        auto created = wm.CreateWorkspace("Testing");
        CHECK(!created.id.empty());
        CHECK_EQ(wm.GetWorkspaces().size(), initial + 1);
        CHECK(wm.GetWorkspace(created.id).has_value());

        // Rename.
        CHECK(wm.RenameWorkspace(created.id, "Renamed"));
        CHECK(wm.GetWorkspace(created.id)->name == "Renamed");
        // Order indexes stay contiguous.
        const auto all = wm.GetWorkspaces();
        for (size_t i = 0; i < all.size(); ++i)
        {
            CHECK_EQ(all[i].orderIndex, static_cast<int32_t>(i));
        }

        // Duplicate: copy lands after the original with suffix.
        auto dupId = wm.DuplicateWorkspace(created.id);
        CHECK(dupId.has_value() && *dupId != created.id);
        const auto dup = wm.GetWorkspace(*dupId);
        CHECK(dup.has_value());
        CHECK(dup->accentColor == wm.GetWorkspace(created.id)->accentColor);

        // Reset clears appearance but keeps identity/name/windows.
        CHECK(wm.ResetWorkspace(created.id));
        const auto reset = wm.GetWorkspace(created.id);
        CHECK(reset.has_value() && reset->name == "Renamed");
        CHECK(reset->defaultWallpaperPath.empty());

        // Delete renumbers; deleting the unknown fails.
        CHECK(wm.DeleteWorkspace(*dupId, all.front().id));
        CHECK_EQ(wm.GetWorkspaces().size(), initial + 1);
        CHECK(!wm.DeleteWorkspace("does-not-exist", all.front().id));
        CHECK(!wm.GetWorkspace(*dupId).has_value());

        // Reorder.
        const auto &list = wm.GetWorkspaces();
        if (list.size() >= 2)
        {
            CHECK(wm.ReorderWorkspace(list.back().id, 0));
            const auto reordered = wm.GetWorkspaces();
            CHECK_EQ(reordered.front().id, list.back().id);
        }
    }

    void TestWorkspaceMonitorStateAndAdjacency()
    {
        flowspace::core::WorkspaceManager wm;
        wm.InitializeDefaultWorkspaces();

        std::vector<flowspace::core::MonitorInfo> monitors(2);
        monitors[0].stableId = L"\\\\?\\DISPLAY#A#1";
        monitors[0].isPrimary = true;
        monitors[1].stableId = L"\\\\?\\DISPLAY#B#2";
        wm.ReconcileMonitors(monitors);

        // Fresh monitors get no active workspace until set.
        CHECK(wm.SetActiveWorkspace(monitors[0].stableId, "ws-1"));
        CHECK(wm.SetActiveWorkspace(monitors[1].stableId, "ws-2"));
        CHECK(wm.GetActiveWorkspaceId(monitors[0].stableId) == std::optional<std::string>("ws-1"));
        CHECK(wm.GetActiveWorkspaceId(monitors[1].stableId) == std::optional<std::string>("ws-2"));
        CHECK(!wm.GetActiveWorkspaceId(L"unknown-monitor").has_value());
        CHECK(!wm.SetActiveWorkspace(monitors[0].stableId, "ghost"));

        // Adjacent resolution wraps around the list.
        const auto all = wm.GetWorkspaces();
        CHECK(wm.ResolveAdjacentWorkspaceId(monitors[0].stableId, flowspace::core::WorkspaceSwitchDirection::Next)
                  == std::optional<std::string>(all[1].id));
        CHECK(wm.ResolveAdjacentWorkspaceId(monitors[0].stableId, flowspace::core::WorkspaceSwitchDirection::Previous)
                  == std::optional<std::string>(all.back().id));

        // Reconcile with a disconnected monitor keeps state per monitor.
        std::vector<flowspace::core::MonitorInfo> onlyPrimary(1);
        onlyPrimary[0].stableId = monitors[0].stableId;
        wm.ReconcileMonitors(onlyPrimary);
        CHECK(wm.GetActiveWorkspaceId(monitors[0].stableId) == std::optional<std::string>("ws-1"));
    }

    void TestWorkspaceWindowAssignment()
    {
        flowspace::core::WorkspaceManager wm;
        wm.InitializeDefaultWorkspaces();

        std::vector<flowspace::core::MonitorInfo> monitors(1);
        monitors[0].stableId = L"\\\\?\\DISPLAY#A#1";
        wm.ReconcileMonitors(monitors);
        (void)wm.SetActiveWorkspace(monitors[0].stableId, "ws-1");

        flowspace::core::WindowInfo win;
        win.hwnd = reinterpret_cast<HWND>(0xABCDEF);
        win.processId = 99;
        win.title = L"App";
        win.monitorStableId = monitors[0].stableId;
        win.visibilityState = flowspace::core::WindowVisibilityState::Visible;

        wm.SyncDiscoveredWindows({win});
        CHECK(wm.GetTrackedWindows().size() == 1);
        // Unassigned windows default to the active workspace of their monitor.
        CHECK(wm.GetTrackedWindows().front().assignedWorkspaceId == "ws-1");

        // Explicit assignment + lookup + forget.
        CHECK(wm.AssignWindowToWorkspace(win.hwnd, "ws-2", monitors[0].stableId));
        CHECK(wm.GetWindow(win.hwnd).has_value());
        CHECK(wm.GetWindow(win.hwnd)->assignedWorkspaceId == "ws-2");

        wm.SetWindowState(win.hwnd, flowspace::core::WindowVisibilityState::HiddenByFlowSpace);
        CHECK(wm.GetWindow(win.hwnd)->visibilityState == flowspace::core::WindowVisibilityState::HiddenByFlowSpace);

        wm.ForgetWindow(win.hwnd);
        CHECK(!wm.GetWindow(win.hwnd).has_value());
        CHECK(wm.GetTrackedWindows().empty());

        // Assignment to a nonexistent workspace fails.
        flowspace::core::WindowInfo win2 = win;
        win2.hwnd = reinterpret_cast<HWND>(0xFEED);
        wm.SyncDiscoveredWindows({win2});
        CHECK(!wm.AssignWindowToWorkspace(win2.hwnd, "ghost", monitors[0].stableId));
    }

    // --------------------------------------------------------------- HotkeyManager
    void TestHotkeyParsing()
    {
        using flowspace::platform::HotkeyManager;
        CHECK_EQ(HotkeyManager::ParseModifiers(L"Ctrl+Alt"), MOD_CONTROL | MOD_ALT);
        CHECK_EQ(HotkeyManager::ParseModifiers(L"Control+Shift"), MOD_CONTROL | MOD_SHIFT);
        CHECK_EQ(HotkeyManager::ParseModifiers(L"Win"), MOD_WIN);
        CHECK_EQ(HotkeyManager::ParseModifiers(L"ctrl+win"), MOD_CONTROL | MOD_WIN);
        CHECK_EQ(HotkeyManager::ParseModifiers(L""), 0u);
        CHECK_EQ(HotkeyManager::ParseKey(L"Left"), VK_LEFT);
        CHECK_EQ(HotkeyManager::ParseKey(L"F12"), VK_F12);
        CHECK_EQ(HotkeyManager::ParseKey(L"D5"), '5');
        CHECK_EQ(HotkeyManager::ParseKey(L"space"), VK_SPACE);
        CHECK_EQ(HotkeyManager::ParseKey(L"O"), 'O');
        CHECK_EQ(HotkeyManager::ParseKey(L"Unknown"), 0u);
        CHECK(HotkeyManager::KeyName(VK_LEFT) == L"Left");
        CHECK(HotkeyManager::KeyName(VK_F12) == L"F12");

        flowspace::core::HotkeyBinding b{flowspace::core::HotkeyAction::NextWorkspace, true, L"Ctrl+Alt", L"Right"};
        CHECK(HotkeyManager::Describe(b) == L"Ctrl+Alt+Right");
        flowspace::core::HotkeyBinding num{flowspace::core::HotkeyAction::SwitchToWorkspaceByNumber, true, L"Ctrl+Alt", L"D3"};
        CHECK(HotkeyManager::Describe(num) == L"Ctrl+Alt+3");
    }

    void TestHotkeyConflicts()
    {
        TempDir dir("hotkeys"); // unused; kept for symmetry
        using flowspace::core::HotkeyAction;
        flowspace::platform::HotkeyManager hotkeys(1);

        // Two different actions on the same combination: conflict, neither registered.
        std::vector<flowspace::core::HotkeyBinding> conflicting = {
            {HotkeyAction::PreviousWorkspace, true, L"Ctrl+Alt", L"Left"},
            {HotkeyAction::RevealAllWindows, true, L"Ctrl+Alt", L"Left"},
        };
        // Use a real (hidden) message window so RegisterHotKey has an owner.
        const char *cls = "FlowSpaceTestHotkeyWindow";
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.lpszClassName = reinterpret_cast<LPCWSTR>(cls);
        wc.hInstance = GetModuleHandleW(nullptr);
        RegisterClassW(&wc);
        HWND msgWindow = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
        CHECK(msgWindow != nullptr);

        const auto conflicts = hotkeys.RegisterAll(msgWindow, conflicting);
        CHECK(!conflicts.empty());
        CHECK(!hotkeys.Resolve(1).has_value());

        // Valid, non-conflicting set: numeric expansion registers many ids.
        std::vector<flowspace::core::HotkeyBinding> valid = {
            {HotkeyAction::PreviousWorkspace, true, L"Ctrl+Alt", L"Left"},
            {HotkeyAction::NextWorkspace, true, L"Ctrl+Alt", L"Right"},
            {HotkeyAction::SwitchToWorkspaceByNumber, true, L"Ctrl+Alt", L"D1"},
        };
        const auto conflicts2 = hotkeys.RegisterAll(msgWindow, valid);
        CHECK(conflicts2.empty());
        hotkeys.UnregisterAll();
        DestroyWindow(msgWindow);
    }
}

int main()
{
    std::printf("FlowSpace core tests\n====================\n");
    TestStringUtils();
    TestConfigFreshDefaults();
    TestConfigRoundTrip();
    TestConfigCorruptRecovery();
    TestConfigAllCorruptFallsBackToDefaults();
    TestConfigMigrationV0();
    TestConfigNormalize();
    TestRecoveryRoundTrip();
    TestRecoveryCorruptJournalIsFailSafe();
    TestWorkspaceCrud();
    TestWorkspaceMonitorStateAndAdjacency();
    TestWorkspaceWindowAssignment();
    TestHotkeyParsing();
    TestHotkeyConflicts();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("TESTS FAILED\n");
    return 1;
}
