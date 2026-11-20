# FlowSpace — Architecture

Documented from actual source code. No invented abstractions.

## Design goals

1. **Correctness & recoverability first** — every hide is journaled before the window is hidden; every show removes the entry; a corrupt journal is discarded rather than mis-applied.
2. **Per-monitor independence** — each monitor has its own active workspace and switch history. Nothing on monitor B changes when monitor A switches.
3. **Minimal privilege & footprint** — no admin, single process, single thread.

## Layering (verified from source)

```
┌─────────────────────────────────────────────────────────────────────────┐
│ ui      SwitcherVisual · SwitchNotification · SwitcherPopup · TrayIcon │  Win32/GDI
│         MainWindow · Theme · DesignSystem                              │
├─────────────────────────────────────────────────────────────────────────┤
│ engine  WorkspaceEngine   (orchestration, UI callbacks)                │
├──────────────────────────────────┬──────────────────────────────────────┤
│ platform                         │ core                                 │
│ MonitorManager                   │ WorkspaceManager                     │
│ WindowManager                    │ ConfigService                        │
│ WallpaperManager (COM)           │ RecoveryManager                      │
│ HotkeyManager                    │ StringUtils                          │
│ ProcessInfo · WinEventHook       │ Models.h                             │
│ StartupManager                   │                                      │
└──────────────────────────────────┴──────────────────────────────────────┘
```

Dependencies point downward only: `ui → engine → {platform, core}`, `platform → core`. `core` is pure C++ (no Win32 beyond HWND handles stored as data), which makes the core testable without a desktop session.

Source layout:
- `src/app/main.cpp` — entry point, single instance, COM, message loop, wiring
- `src/app/diagnostic_main.cpp` — read-only diagnostic CLI (`flowspace-diag`)
- `src/core/` — Models.h, ConfigService, RecoveryManager, StringUtils, WorkspaceManager
- `src/engine/` — WorkspaceEngine
- `src/platform/` — MonitorManager, WindowManager, ProcessInfo, WallpaperManager, HotkeyManager, StartupManager, WinEventHook
- `src/ui/` — Theme, DesignSystem, SwitcherVisual, SwitchNotification, SwitcherPopup, TrayIcon, MainWindow

## Threading model

**Single thread.** All work — WinEvent hook callbacks (`SetWinEventHook` with `WINEVENT_OUTOFCONTEXT` deliver on the installing thread), hotkeys (`WM_HOTKEY`), tray (`WM_APP`), UI — flows through the hidden `FlowSpaceMessageWindow` message loop in `src/app/main.cpp`. `WorkspaceManager` still has a `std::mutex` for future-proofing but nothing contends today. This eliminates cross-thread HWND races by construction.

## Application lifecycle (`src/app/main.cpp`)

`wWinMain` runs, in order:

1. **Single-instance mutex** (`Local\FlowSpaceSingleInstance`). A second instance shows "FlowSpace is already running." and exits.
2. **Logging init** → `%LOCALAPPDATA%\FlowSpace\logs\flowspace.log` (rotating, spdlog, truncate on start).
3. **Per-monitor DPI awareness** — `SetProcessDpiAwarenessContext(PER_MONITOR_V2)` via `GetProcAddress` fallback (MinGW auto-links a default manifest, so no manifest resource; see `packaging/app.manifest` note).
4. **COM STA** (`CoInitializeEx` with `COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE`) for `IDesktopWallpaper` and shell APIs.
5. **Construct `AppContext`** — `ConfigService`, `RecoveryManager`, `WorkspaceEngine` and the platform managers. Construction order matters: `WindowManager` references `MonitorManager`.
6. **Register UI window classes** — `SwitchNotification`, `SwitcherPopup`, `MainWindow`.
7. **Create the hidden message window** (`FlowSpaceMessageWindow`). `AppContext::Initialize()` wires:
   - `engine->Attach(window)` — hotkey registration + workspace init + monitor reconcile
   - switch callback → `SwitchNotification::Show` (compact per-monitor notification)
   - state-changed callback → settings window refresh + tray update
   - tray icon + menu builder
   - WinEvent hooks (create/show/hide/destroy/movesizeend/foreground)
8. **Message loop** (`GetMessageW`/`TranslateMessage`/`DispatchMessageW`). `AppContext::Shutdown()` on quit: unregister hotkeys, reveal all windows, restore wallpapers (if `restoreWallpaperOnExit`), save config, `CoUninitialize`, release mutex.

## Module responsibilities

### `core/Models.h` — shared data model
- `Rect` — integer rectangle with `width()`, `height()`, `Contains()`.
- `MonitorInfo` — stableId, deviceName, friendlyName, runtimeHandle (transient HMONITOR), bounds, workArea, dpiX/Y, isPrimary, isConnected, activeWorkspaceId.
- `Workspace` — id, name, icon, accentColor, orderIndex (position within the owning monitor's list), **monitorStableId** (the monitor that owns this workspace; empty = unassigned legacy workspace adopted at runtime), defaultWallpaperPath, wallpaperFit, wallpaperBackgroundColor, perMonitorWallpaper, perMonitorWallpaperFit, overlay fields.
- `WindowVisibilityState` — Visible, Minimized, Maximized, HiddenBySystem, CloakedByDwm, HiddenByFlowSpace.
- `WindowInfo` — hwnd, processId, threadId, processCreationTime, title, className, executablePath, executableName, monitorStableId, assignedWorkspaceId, visibilityState, isManageable, exclusionReason.

### `core/WorkspaceManager` — domain state
- Per-monitor workspace ownership: each `Workspace` carries `monitorStableId`; switching, numbering, adjacency, deletion and renaming are always resolved inside that monitor's own ordered list.
- Monitor reconciliation (`ReconcileMonitors`): connected monitors with no workspaces adopt unassigned (legacy) workspaces or receive a clone of an existing monitor's set; every connected monitor always has ≥1 workspace.
- Per-monitor active workspace (`SetActiveWorkspace`/`GetActiveWorkspaceId`/`ResolveAdjacentWorkspaceId`) — `SetActiveWorkspace` rejects cross-monitor references.
- CRUD for workspaces (`Create`/`Rename`/`Reorder`/`Delete`/`Duplicate`/`Reset`), each scoped to the owner monitor; a monitor's last workspace can never be deleted.
- Window assignment tracking (`SyncDiscoveredWindows`/`AssignWindowToWorkspace`/`ForgetWindow`) — cross-monitor assignment is refused.
- `MutableWorkspaces()` returns a raw pointer — deliberately no lock; the settings UI edits on the same thread that owns the manager.

### `core/ConfigService` — config persistence
- `Load()` → `ConfigLoadStatus` (`Loaded` / `RecoveredFromBackup` / `FreshDefaults`). Schema migration v0→v1→v2 (per-monitor workspaces). Normalization (renumber orderIndex per monitor, drop duplicates, repair colors, validate monitorState references belong to the referenced monitor).
- `Save()` → atomic write: rotate main → backup, write temp, rename.
- `Config` holds `Settings`, `workspaces`, `appRules`, `hotkeys`, `monitorState`, `originalWallpapers`. Each workspace now carries a `monitor` field (stableId) so lists split per display at load time.

### `core/RecoveryManager` — recovery journal
- `RecordHide()` persists hwnd + process identity (PID + creation time + exe path + title) to `journal.json` (write-to-temp + rename). Called *before* the window is hidden.
- `RecordShow()` / `Forget()` remove entries when a window is shown.
- `Load()` / `Save()` — corrupt journal yields empty journal (fail-safe).

### `core/StringUtils` — UTF-8/UTF-16 conversions + hex color helpers

### `platform/MonitorManager` — monitor detection
- `EnumerateMonitors()` via `EnumDisplayMonitors` + `GetMonitorInfoW`. Stable ID from `GetDisplayConfigBufferSizes`/`QueryDisplayConfig` target device path (`\\?\DISPLAY#...`); falls back to GDI device name.
- `GetMonitorIdUnderCursor()` via `MonitorFromPoint`/`GetCursorPos`.
- `GetMonitorIdForWindow(hwnd)` via `MonitorFromWindow`.

### `platform/WindowManager` — window discovery & visibility
- `DiscoverWindows()` via `EnumWindows`. Evaluates `IsManageableWindow` (excludes FlowSpace's own process, invisible, cloaked, child, tool windows, shell windows, zero-area windows).
- `HideWindowFast`/`ShowWindowFast`/`MinimizeWindowFast` — async show/hide (`ShowWindowAsync`) to avoid cross-thread deadlocks. `ShowWindowFast` restores size/position via `WINDOWPLACEMENT` and does not steal activation (`SW_SHOWNOACTIVATE`).
- `CaptureRestoreState` — captures minimized/maximized/restore-rect before hide.
- `GetForegroundWindow`, `GetWindowUnderCursor` (topmost manageable window containing the cursor), `GetMonitorIdForWindow`, `MoveWindowToMonitor`, `IsWindowFromProcess` (validates PID + exe path + creation time).

### `platform/WallpaperManager` — per-monitor wallpaper via COM
- Raw COM vtable `IDesktopWallpaperImpl` (IID `B92B56A9-8B55-4E14-9A89-0199BBB6F93B`, CLSID `C2CF3110-460E-4fc1-B9D0-8A1C0C9CC4BD`). Never uses undocumented interfaces or registry hacks.
- `SetWallpaper(monitorDevicePath, imagePath)` — never passes a null monitor ID (would apply to all monitors). Empty path clears the image.
- `CaptureAll()` / `RestoreAll()` — capture/restore per-monitor wallpaper state. Skips detached monitors (`GetMonitorRECT` returns S_FALSE).
- `IsMonitorAttached()` — true when `GetMonitorRECT` succeeds.

### `platform/HotkeyManager` — global hotkeys
- `RegisterAll()` expands bindings into concrete `RegisterHotKey` combos. Numeric actions (`SwitchToWorkspaceByNumber`, `MoveActiveWindowToWorkspaceByNumber`) register D1–D9.
- Conflict detection compares **logical bindings** (action + numeric index), not display strings. Two different actions on the same combo = conflict; neither is registered.
- `Resolve(hotkeyId)` → `(HotkeyAction, numericIndex)`.

### `platform/WinEventHook` — WinEvent hooks
- Installs `EVENT_OBJECT_CREATE/SHOW/HIDE/DESTROY`, `EVENT_SYSTEM_MOVESIZEEND`, `EVENT_SYSTEM_FOREGROUND` with `WINEVENT_OUTOFCONTEXT`. Callbacks delivered on the installing thread via the message loop.

### `platform/HotkeyManager`, `StartupManager`, `ProcessInfo`
- `StartupManager` — per-user HKCU Run entry (no admin).
- `ProcessInfo` — `QueryDetails`/`IdentityMatches` (process path + creation time) for recovery journal validation.

### `engine/WorkspaceEngine` — orchestration
- `SwitchMonitorToWorkspace(monitor, workspaceId)` — discover windows, sync tracker, hide windows assigned to other workspaces on that monitor, show windows assigned to target, set active workspace, apply wallpaper, fire overlay + state-changed callbacks, save config.
- `SwitchRelative(monitor, direction)`, `SwitchByNumber(monitor, number)`.
- `MoveActiveWindowToWorkspace/workspaceId` / `Relative` / `ByNumber` — resolve window (cursor first, then foreground), assign, hide.
- `RevealAllWindows()` — emergency reveal-all; also used on clean shutdown.
- `HandleDisplayChange()` — `WM_DISPLAYCHANGE` handler: re-enumerate, reconcile, apply persisted monitorState.
- `HandleWindowEvent(type, hwnd)` — WinEvent dispatcher: Created→ProcessNewWindow, Shown→ProcessShownWindow, Destroyed→Forget, MoveSizeEnd→ProcessMovedWindow.
- `HandleHotkey(hotkeyId)` — maps WM_HOTKEY id to action.
- UI facade: `GetWorkspaces`, `WorkspaceAt`, `GetMonitors`, `GetForegroundWindowInfo`, `CreateWorkspace`, `RenameWorkspaceIndex`, `DuplicateWorkspaceIndex`, `ResetWorkspaceIndex`, `DeleteWorkspaceIndex`, `SetWorkspaceWallpaperIndex`, `SwitchMonitorToWorkspaceIndex`, `RevealSingleWindow`, `RestoreOriginalWallpaperForMonitorIndex`, `CaptureOriginalWallpapers`, `ApplyHotkeys`.
- Callbacks: `OnOverlayShow(workspace, monitorStableId)`, `OnStateChanged()`.

### `ui/MainWindow` — settings window
- Settings shell: fixed 208 px (96 DPI) sidebar (drawn brand mark, product name, 8 navigation items with icons + selected indicator, running status, version), flexible content area with a fixed per-page header (title + description) and a scrollable body.
- 8 pages (Overview, Workspaces, Monitors, Shortcuts, Behavior, App Rules, Recovery, About), all custom-drawn via `Draw`/`Components` primitives. Hit-tested hotspots (no child controls). Never appears in engine window tracking (owned by FlowSpace process).
- `Context` holds engine/config/recovery/monitors/wallpaper/startup + `onConfigSaved`.
- Theme: always follows the Windows app light/dark preference (`AppTheme::ThemeMode::System`), resolved from `AppsUseLightTheme` and re-applied on `WM_SETTINGCHANGE`. The native title bar is matched through `DwmSetWindowAttribute` (immersive dark mode + caption colour).
- Pages switch instantly (no cross-fade) to avoid layout jitter; the whole window is painted into an offscreen bitmap (`WM_ERASEBKGND` returns 1) so resizing and page switches never flicker.
- Pages taller than the viewport scroll with the mouse wheel; content is clipped to the viewport and a thin scrollbar is drawn.
- Workspace rows expose a single three-dot menu (rename, wallpaper, accent, duplicate, reset, delete) instead of a row of cryptic icon buttons.
- Per-monitor DPI aware (`WM_DPICHANGED`), minimum track size enforced via `WM_GETMINMAXINFO`.

### `ui/SwitcherPopup` — `Ctrl+Alt+Space` selector
- Layered, per-pixel-alpha translucent panel centered on target monitor. Paginated (9 rows/page). Digits 1–9 select directly. `UpdateLayeredWindow` for flicker-free presentation.

### `ui/SwitchNotification` — post-switch overlay
- Temporary non-activating notification, bottom-center on the correct monitor, self-dismisses after ~800ms with fade/scale animation. At most one per monitor.

### `ui/TrayIcon` — system tray
- Context menu: Open Settings, per-monitor workspace switching, Reveal All, Pause, Exit. Callback delivered to app's message window.

### `ui/Theme` + `ui/SwitcherVisual` — drawing helpers
- Dark palette, accent presets, Segoe UI/Segoe Fluent Icons fonts, rounded-card alpha rendering for layered windows.

## Important Windows APIs

| API | Purpose | File |
|-----|---------|------|
| `RegisterHotKey` / `UnregisterHotKey` | global hotkeys on message window | `src/platform/HotkeyManager.cpp` |
| `SetWinEventHook` / `UnhookWinEvent` | window create/show/hide/destroy/move/foreground | `src/platform/WinEventHook.cpp` |
| `EnumDisplayMonitors` / `GetMonitorInfoW` | monitor enumeration | `src/platform/MonitorManager.cpp` |
| `GetDisplayConfigBufferSizes` / `QueryDisplayConfig` / `DisplayConfigGetDeviceInfo` | stable monitor device paths | `src/platform/MonitorManager.cpp` |
| `EnumWindows` / `GetWindowRect` / `IsWindowVisible` / `IsIconic` / `IsZoomed` | window discovery | `src/platform/WindowManager.cpp` |
| `ShowWindowAsync` (SW_HIDE / SW_MINIMIZE / SW_SHOWNOACTIVATE / SW_SHOWMAXIMIZED) | hide/show without activation | `src/platform/WindowManager.cpp` |
| `GetWindowPlacement` / `SetWindowPlacement` | restore size/position | `src/platform/WindowManager.cpp` |
| `DwmGetWindowAttribute(DWMWA_CLOAKED)` | detect DWM-cloaked windows | `src/platform/WindowManager.cpp` |
| `CoCreateInstance(CLSID_DesktopWallpaper)` / `IDesktopWallpaper` | per-monitor wallpaper | `src/platform/WallpaperManager.cpp` |
| `SetProcessDpiAwarenessContext(PER_MONITOR_V2)` | DPI awareness | `src/app/main.cpp` |
| `CoInitializeEx(COINIT_APARTMENTTHREADED)` | COM STA | `src/app/main.cpp` |
| `CreateMutexW` (Local\FlowSpaceSingleInstance) | single-instance guard | `src/app/main.cpp` |
| `Shell_NotifyIconW` | tray icon | `src/ui/TrayIcon.cpp` |
| `UpdateLayeredWindow` | layered translucent UI | `src/ui/SwitcherPopup.cpp`, `SwitchNotification.cpp` |
| `RegOpenKeyExW` / `RegSetValueExW` / `RegDeleteValueW` | HKCU Run startup | `src/platform/StartupManager.cpp` |
| `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` / `GetProcessTimes` / `QueryFullProcessImageNameW` | process identity for recovery | `src/platform/ProcessInfo.cpp` |

## External dependencies

| Dependency | Location | License | Use |
|------------|----------|---------|-----|
| spdlog | `external/spdlog/include` | MIT | logging |
| {fmt} (bundled with spdlog) | `external/spdlog/include/spdlog/fmt/bundled` | MIT + redistribution exception | spdlog formatting |
| nlohmann/json | `external/nlohmann` (single header) | MIT | config/journal JSON |

Both vendored in-tree. No package manager required.

## Build architecture (`CMakeLists.txt`)

Four static libraries + two executables + one test executable:

1. `flowspace_core` (STATIC) — `src/core/*.cpp` — domain logic, persistence, recovery journal. No Win32 UI deps.
2. `flowspace_platform` (STATIC) — `src/platform/*.cpp` — Win32 API + COM interop. Links `flowspace_core` + system libs (user32, gdi32, shell32, advapi32, ole32, oleaut32, comdlg32, comctl32, msimg32, uuid, dwmapi, shcore, uxtheme, gdiplus).
3. `flowspace_engine` (STATIC) — `src/engine/WorkspaceEngine.cpp` — orchestration. Links `flowspace_core` + `flowspace_platform`.
4. `flowspace_ui` (STATIC) — `src/ui/*.cpp` — switcher, notification, tray, settings. Links `flowspace_core` + `flowspace_platform` + `flowspace_engine`.
5. `flowspace` (EXE, WIN32) — `src/app/main.cpp` + `packaging/app.rc` — the app. Links all four libs. `-municode` → `wWinMain`. Icon embedded via `app.rc` (windres).
6. `flowspace-diag` (EXE, console) — `src/app/diagnostic_main.cpp` — read-only diagnostic CLI. Links `flowspace_platform` + `flowspace_core`.
7. `flowspace_tests` (EXE) — `tests/test_core.cpp` — core-logic tests. Links `flowspace_core` + `flowspace_platform`.

`enable_testing()` + `add_test(NAME core_tests COMMAND flowspace_tests)`.

Compile definitions: `UNICODE`, `_UNICODE`, `_WIN32_WINNT=0x0A00`, `NTDDI_VERSION=0x0A000000`, `NOMINMAX`, `FLOWSPACE_DEBUG` (Debug config). C++23, no extensions, no modules scan. Static link (`-static`).

## Release packaging

`.github/workflows/release.yml` checks out the pinned `spdlog` submodule, builds and tests the x64 app, then calls `scripts/package-release.ps1`. The script creates the portable ZIP and Inno Setup installer, includes third-party licenses, resolves runtime DLL dependencies, and smoke-tests installation and uninstallation. Optional signing uses the `WINDOWS_CERTIFICATE_BASE64` and `WINDOWS_CERTIFICATE_PASSWORD` secrets.

## Data flow between modules

**Workspace switch (hotkey → hide/show):**
1. `main.cpp` message loop → `WM_HOTKEY` → `AppContext::HandleHotkey` → `WorkspaceEngine::HandleHotkey` → `SwitchRelative`/`SwitchByNumber`.
2. `WorkspaceEngine::ApplySwitch`:
   a. `WindowManager::DiscoverWindows` → `WorkspaceManager::SyncDiscoveredWindows`
   b. For each tracked window on the target monitor: if assigned to target workspace → `ShowWindowInternal` + `RecoveryManager::RecordShow`; if assigned elsewhere and visible → `HideWindowInternal` → `RecoveryManager::RecordHide` (before `ShowWindowAsync(SW_HIDE)`) + `WorkspaceManager::SetWindowState(HiddenByFlowSpace)`.
   c. `WorkspaceManager::SetActiveWorkspace(monitor, workspaceId)`; `config.monitorState[monitor] = workspaceId`; `PersistWorkspacesToConfig()` syncs `workspaces_` into `config.workspaces` before saving.
   d. `ApplyWallpaperForMonitor` → `WallpaperManager::SetWallpaper`.
   e. `OnOverlayShow` → `SwitchNotification::Show`.
   f. `config.Save()`; `OnStateChanged()` → tray + settings refresh.

**Crash recovery (startup):**
`WorkspaceEngine::Initialize` → `RecoveryManager::Entries()` → for each entry: validate `IsWindow` + `WindowManager::IsWindowFromProcess` (PID/exe/creation time) → if hidden, `WindowManager::ShowWindowFast` → `RecoveryManager::RecordShow`.

**Display change (`WM_DISPLAYCHANGE`):**
`WorkspaceEngine::HandleDisplayChange` → `MonitorManager::EnumerateMonitors` → `WorkspaceManager::ReconcileMonitors` (preserve activeWorkspaceId on reconnect) → apply `config.monitorState` → `PersistWorkspacesToConfig()` → `config.Save()` → `OnStateChanged`.

**New window (`EVENT_OBJECT_CREATE`):**
`HandleWindowEvent` → `ProcessNewWindow` → `DiscoverWindows` → `SyncDiscoveredWindows` → `ResolveAssignmentForNewWindow` (app rules → group-by-application → monitor active workspace) → `AssignWindowToWorkspace` → optional `MoveWindowToMonitor`.

## Key symbols reference

- `flowspace::engine::WorkspaceEngine` — `src/engine/WorkspaceEngine.h/.cpp`
- `flowspace::core::WorkspaceManager` — `src/core/WorkspaceManager.h/.cpp`
- `flowspace::core::ConfigService` — `src/core/ConfigService.h/.cpp`
- `flowspace::core::RecoveryManager` — `src/core/RecoveryManager.h/.cpp`
- `flowspace::platform::MonitorManager` — `src/platform/MonitorManager.h/.cpp`
- `flowspace::platform::WindowManager` — `src/platform/WindowManager.h/.cpp`
- `flowspace::platform::WallpaperManager` — `src/platform/WallpaperManager.h/.cpp`
- `flowspace::platform::HotkeyManager` — `src/platform/HotkeyManager.h/.cpp`
- `flowspace::platform::WinEventHook` — `src/platform/WinEventHook.h/.cpp`
- `flowspace::platform::ProcessInfo` — `src/platform/ProcessInfo.h/.cpp`
- `flowspace::platform::StartupManager` — `src/platform/StartupManager.h/.cpp`
- `flowspace::ui::MainWindow` — `src/ui/MainWindow.h/.cpp`
- `flowspace::ui::SwitcherPopup` — `src/ui/SwitcherPopup.h/.cpp`
- `flowspace::ui::SwitchNotification` — `src/ui/SwitchNotification.h/.cpp`
- `flowspace::ui::TrayIcon` — `src/ui/TrayIcon.h/.cpp`
- `flowspace::ui::Theme` / `flowspace::ui::visual` — `src/ui/Theme.{h,cpp}`, `src/ui/SwitcherVisual.{h,cpp}`
- `flowspace::ui::AppTheme` + `flowspace::ui::Draw` / `flowspace::ui::Components` — `src/ui/DesignSystem.{h,cpp}`: theme tokens (light/dark/system), DPI-scaled fonts/spacing, drawing primitives and reusable components (card, button, toggle, badge, divider, brand mark, empty state).
- `flowspace::ui::MainWindow` — `src/ui/MainWindow.h/.cpp`: settings shell with fixed sidebar, eight content pages, instant page switching, wheel scrolling, system-following theme and hit-tested interactions.

### `ui/DesignSystem` — design tokens and reusable components
- `AppTheme` — runtime theme state (mode, DPI scale, resolved `ThemeTokens`, lazy fonts). Supports Light and Dark; the settings window runs in System mode and resolves the Windows `AppsUseLightTheme` preference (with a `WM_SETTINGCHANGE` refresh). DPI scale comes from `GetDpiForWindow`.
- `ThemeTokens` — dark and light palettes per the design spec (bg, sidebar, surface, elevated, border, text, accent, semantic colors, workspace accent presets).
- `Draw` — primitives: fill, rect, line, text, icon glyph, text measurement. `Draw::RoundRect` is rendered through GDI+ with anti-aliasing so card, button and toggle edges stay smooth; GDI+ is started once per process and `gdiplus` is linked into `flowspace`.
- `Components` — stateless reusable controls: card, brand mark, divider, button, icon button, toggle, badge, section header, empty state. All support both themes.
- Spacing scale 4/8/12/16/20/24/32 px, DPI scaled.
- `scripts/make-icon.ps1` generates a multi-size `build/flowspace.ico` (16–256 px, 32-bit DIB + PNG) embedded by `packaging/app.rc`; the window class uses resource ID 1 for the title bar, taskbar and Alt+Tab icons.
- `AppContext` (anonymous namespace) — `src/app/main.cpp`

## Related documentation

- [CONFIGURATION.md](CONFIGURATION.md) — config.json schema reference.
- [TESTING.md](TESTING.md) — manual integration test checklist (live run, crash recovery, display-change, wallpaper restore).
