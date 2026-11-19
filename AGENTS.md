# FlowSpace — AI Agent Instructions

FlowSpace is an independent per-monitor workspace manager for Windows 10/11. Each monitor gets its own set of workspaces (named, colored, wallpaper-capable); switching a monitor hides its windows and shows another set — like virtual desktops, but per monitor, with full crash recovery.

**This file is mandatory reading for every AI coding agent before touching any code.** It is the single source of truth for how to work in this repository.

---

## Project overview

- **Purpose:** Per-monitor independent virtual workspaces for Windows. Hiding/showing windows is journaled to disk so windows are never lost, even after a crash.
- **Language:** Native C++ (C++23), compiled with MinGW-w64 UCRT64 (g++). No C#, no .NET, no Electron — deliberate native Windows implementation.
- **Version:** 0.1.0
- **Platform:** Windows 10 (1903+) / Windows 11, x64. No admin rights required.
- **Data location:** `%LOCALAPPDATA%\FlowSpace\` (`config.json`, `config.backup.json`, `journal.json`, `logs\flowspace.log`).

## Technology stack

| Layer | Responsibility | Key files |
|-------|---------------|-----------|
| `core` | Domain logic, persistence, recovery journal (pure C++, no Win32 beyond HWND handles as data) | `src/core/WorkspaceManager.{h,cpp}`, `ConfigService.{h,cpp}`, `RecoveryManager.{h,cpp}`, `StringUtils.{h,cpp}`, `Models.h` |
| `platform` | Win32 API and COM interop (monitors, windows, wallpaper, hotkeys, startup, event hooks) | `src/platform/MonitorManager.{h,cpp}`, `WindowManager.{h,cpp}`, `WallpaperManager.{h,cpp}`, `HotkeyManager.{h,cpp}`, `StartupManager.{h,cpp}`, `WinEventHook.{h,cpp}`, `ProcessInfo.{h,cpp}` |
| `engine` | Workspace orchestration: hide/show, wallpaper, hotkey actions, window events | `src/engine/WorkspaceEngine.{h,cpp}` |
| `ui` | Switcher popup, notification, tray, settings window (Win32/GDI, custom-drawn) | `src/ui/MainWindow.{h,cpp}`, `SwitcherPopup.{h,cpp}`, `SwitchNotification.{h,cpp}`, `TrayIcon.{h,cpp}`, `Theme.{h,cpp}`, `SwitcherVisual.{h,cpp}` |
| `app` | Entry point, single-instance, COM, message loop, wiring | `src/app/main.cpp`, `src/app/diagnostic_main.cpp` |

- **External dependencies (vendored):** spdlog (`external/spdlog/include`, MIT), nlohmann/json (`external/nlohmann`, MIT single-header).
- **Windows APIs used:** user32, gdi32, shell32, advapi32, ole32, oleaut32, comdlg32, comctl32, msimg32, uuid, dwmapi, shcore, uxtheme. Key COM: `IDesktopWallpaper` (per-monitor wallpaper).
- **Build:** CMake 3.25+, Ninja, MinGW g++. Static `-static` link so the exe needs no MinGW runtime DLLs.

## Repository structure

```text
FlowSpace/
├── AGENTS.md                      # this file — start here
├── README.md                      # user-facing docs (build, run, features, known limits)
├── UNINSTALL.md
├── THIRD-PARTY-NOTICES.txt
├── .ai/
│   ├── ARCHITECTURE.md            # deep-dive module relationships, APIs, data flow
│   ├── CONFIGURATION.md           # config.json schema reference
│   └── TESTING.md                 # manual integration test checklist
├── src/
│   ├── app/                       # main.cpp, diagnostic_main.cpp
│   ├── core/                      # domain models + services
│   ├── engine/                    # WorkspaceEngine
│   ├── platform/                  # Win32/COM managers
│   └── ui/                        # switcher, notification, tray, settings
├── tests/test_core.cpp            # self-contained test harness (137 checks)
├── scripts/                       # build.ps1, compile-check.ps1, make-icon.ps1, stop.ps1
├── packaging/                     # app.rc, app.manifest, FlowSpace.iss
├── external/                      # vendored spdlog, nlohmann/json
├── .github/workflows/release.yml  # automated x64 Windows release
├── CMakeLists.txt                 # build definition (single source of truth)
└── build/                         # gitignored build output
```

## Coding conventions

- **C++23.** `cmake_minimum_required(VERSION 3.25)`, `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_EXTENSIONS OFF`.
- **No C++20/23 modules:** `CMAKE_CXX_SCAN_FOR_MODULES OFF` (MinGW ninja lexer breaks on `$` in rule names).
- **Namespaces:** `flowspace::core`, `flowspace::platform`, `flowspace::engine`, `flowspace::ui`.
- **Error handling:** No exceptions for control flow. `bool`/`std::optional`/`HRESULT` returns; spdlog for diagnostics. `ConfigService::Save()` returns `bool` on write failure.
- **Win32:** `UNICODE`/`_UNICODE`, `_WIN32_WINNT=0x0A00`, `NTDDI_VERSION=0x0A000000`, `NOMINMAX`. Wide strings (`std::wstring`) for Win32 APIs; UTF-8 at the config boundary via `core::WideToUtf8` / `Utf8ToWide`.
- **Single-threaded:** Everything runs on the main message-loop thread. `WorkspaceManager` has a `std::mutex` for future-proofing but nothing contends today. Do not introduce multi-threading.
- **MinGW-specific:** No embedded manifest resource (MinGW auto-links a default manifest; a second manifest fails to merge). PerMonitorV2 DPI is requested at runtime in `main.cpp` via `SetProcessDpiAwarenessContext`. `packaging/app.manifest` is reference only.
- **Resource cleanup:** RAII for handles (local `HandleCloser`/`UniqueHandle` wrappers, not WIL — MSVC-oriented, not in MinGW).
- **Tests:** `tests/test_core.cpp` — self-contained mini assert harness, no external test framework. 137 checks. Additions should follow the existing `CHECK`/`CHECK_EQ` macro pattern.

## Build and testing instructions

### Prerequisites
- MinGW-w64 UCRT64 toolchain (g++, gcc, windres, ninja). Default: `C:\msys64\ucrt64\bin`.
- CMake 3.25+ and Ninja.

### Build (PowerShell)

```powershell
# Debug build (default)
powershell -ExecutionPolicy Bypass -File scripts\build.ps1

# Release build
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -BuildType Release

# Custom MSYS2 path
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -MingwBin C:\tools\ucrt64\bin
```

### CMake directly

```powershell
cmake -S . -B build\cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build\cmake                      # all targets
cmake --build build\cmake --target flowspace   # just the app
```

### Targets

| Target | Purpose |
|--------|---------|
| `flowspace.exe` | The app (GUI subsystem, tray icon, hidden message window) |
| `flowspace-diag.exe` | Read-only diagnostic CLI: monitors + manageable windows report |
| `flowspace_tests.exe` | Core-logic test suite |

### Run and test

```powershell
# Run the app (starts hidden in tray)
build\cmake\flowspace.exe

# Stop gracefully (posts WM_CLOSE to the message window)
powershell -ExecutionPolicy Bypass -File scripts\stop.ps1

# Run the test suite
& build\cmake\flowspace_tests.exe

# Or via CTest
ctest --test-dir build\cmake
```

### Default hotkeys
`Ctrl+Alt+Left/Right` switch workspace, `Ctrl+Alt+1..9` jump, `Ctrl+Alt+Shift+Left/Right` move active window, `Ctrl+Alt+Space` switcher popup, `Ctrl+Alt+F12` reveal all, `Ctrl+Alt+O` settings.

## Documentation reading order

**Always read:**
1. `AGENTS.md` — this file
2. `.ai/ARCHITECTURE.md` — architectural reference

**Read when relevant:**
- `.ai/ARCHITECTURE.md` — before architectural changes or when adding modules.
- `.ai/CONFIGURATION.md` — when changing config schema or settings.
- `.ai/TESTING.md` — before touching engine, window management, wallpaper, or shutdown paths.

**Always inspect actual source code before changing it, regardless of what documentation says.** Documentation can lag the implementation.

## Mandatory agent workflow

### Before starting work
1. Read `AGENTS.md`.
2. Read relevant files from `.ai/`.
3. Inspect Git status (`git status`) and recent commits (`git log --oneline -10`).
4. Inspect relevant source code.
5. Understand the existing implementation before proposing changes.

### Before ending work
1. Update `.ai/ARCHITECTURE.md` when architectural understanding changes.
2. Record modified files and test results.
3. Record unresolved issues and blockers.

Do not update files mechanically if nothing has changed.

## Rules for modifying existing code

1. **Documentation-only tasks do not touch source.** If the task is purely documentation, do not modify `.cpp`, `.h`, `CMakeLists.txt`, scripts, or packaging files.
2. **Read before writing.** Never modify a function you haven't read in full.
3. **Preserve single-threaded design.** All state-changing work flows through the hidden `FlowSpaceMessageWindow` message loop in `src/app/main.cpp`.
4. **Never touch FlowSpace's own windows.** `HandleWindowEvent` skips `GetCurrentProcessId()`; `IsManageableWindow` excludes the FlowSpace process.
5. **Per-monitor independence:** switching one monitor must never touch windows on another monitor.
6. **Recovery journal first:** `RecoveryManager::RecordHide()` is called *before* `ShowWindowAsync(SW_HIDE)`. Never reverse this order.
7. **Config writes are atomic:** rotate main → backup, write temp, rename. Do not bypass this pattern.
8. **No empty monitor IDs in wallpaper calls:** `WallpaperManager::SetWallpaper` refuses empty monitor IDs to avoid clobbering all monitors.

## Rules for updating project documentation

1. **Keep documentation concise but technically useful.** Do not copy large portions of source code into Markdown.
2. **Avoid unnecessary duplication.** Reference actual source files and symbols instead of restating them.
3. **Use relative Markdown links** between related documentation files (e.g. `ARCHITECTURE.md`, `CONFIGURATION.md`).
4. **Distinguish verified facts from assumptions.** Mark unknown information as "unknown" rather than guessing.
5. **Never put secrets, API keys, or credentials in documentation.**
6. **Do not fabricate historical decisions or sessions.** Git history can be referenced as evidence, but is not an AI session transcript.
7. **Keep documentation in Git.** All `.ai/` files are committed to the repository.

## Git safety rules

- **Do not commit or push changes.** Documentation changes are left uncommitted for review.
- **Do not run destructive Git commands** (`git reset --hard`, `git clean -fd`, `git push --force`, etc.).
- **Do not discard existing user changes.** The working tree must remain as you found it (apart from the new documentation files you add).
- **Verify:** `git status` should show only your new documentation files as untracked, with no modified source files.

Do not record dynamic git state (current branch, remotes, last commit) in documentation — it goes stale immediately. Check `git status` and `git log --oneline -10` at the start of a session instead.

## Undocumented / uncertain areas

- No active development task is currently identified.
- The `.vscode/` launch/debug configuration exists (`launch.json`, `tasks.json`, `c_cpp_properties.json`) but is not documented in detail.
- The `build/` directory contains a pre-generated `flowspace.ico` and a `cmake/` subdirectory (gitignored build output).