# FlowSpace architecture

## Design goals

1. **Correctness & recoverability first** — a hidden window must never be lost: every hide is
   journaled before the window is hidden, every show removes the entry; a corrupt journal is
   discarded rather than mis-applied.
2. **Per-monitor independence** — each monitor has its own active workspace and its own
   switch history. Nothing on monitor B changes when monitor A switches.
3. **Minimal privilege & footprint** — no admin, single process, single thread.

## Layering

```
┌──────────────────────────────────────────────────────────┐
│ ui      Overlay · TrayIcon · SwitcherPopup · MainWindow  │  Win32 painting only
├──────────────────────────────────────────────────────────┤
│ engine  WorkspaceEngine   (orchestration, UI callbacks)  │
├──────────────────────────────────┬───────────────────────┤
│ platform                         │ core                  │
│ MonitorManager                   │ WorkspaceManager      │
│ WindowManager                    │ ConfigService         │
│ WallpaperManager (COM)           │ RecoveryManager       │
│ HotkeyManager                    │ StringUtils           │
│ ProcessInfo · WinEventHook       │ Models.h              │
│ StartupManager                   │                       │
└──────────────────────────────────┴───────────────────────┘
```

Dependencies point downward only: `ui → engine → {platform, core}`, `platform → core`.
`core` is pure C++ (no Win32 beyond HWND handles stored as data), which is what makes the
core testable without a desktop session.

## Threading model

Single thread. All work — WinEvent hook callbacks (`SetWinEventHook` with
`WINEVENT_OUTOFCONTEXT` deliver on the installing thread), hotkeys (`WM_HOTKEY`), tray
(`WM_APP`), UI — flows through the hidden `FlowSpaceMessageWindow` message loop in
`main.cpp`. `WorkspaceManager` still has a mutex for future-proofing but nothing contends
today. This eliminates cross-thread HWND races by construction.

## Entry point (`src/app/main.cpp`)

`wWinMain` does, in order:

1. Single-instance mutex (`Local\FlowSpaceSingleInstance`).
2. Logging init → `%LOCALAPPDATA%\FlowSpace\logs\flowspace.log` (rotating, spdlog).
3. `SetProcessDpiAwarenessContext(PER_MONITOR_V2)` via `GetProcAddress` fallback (MinGW
   ld auto-links a default manifest, so no manifest resource — see packaging note).
4. COM STA (`CoInitializeEx`) for `IDesktopWallpaper` and shell APIs.
5. Construct `AppContext`: `ConfigService`, `RecoveryManager`, `WorkspaceEngine` and the
   platform managers.
6. Register UI window classes (Overlay, SwitcherPopup, MainWindow).
7. Create the hidden message window; `AppContext::Initialize()` wires:
   - `engine->Attach(window)` — hotkey registration + workspace init + monitor reconcile
   - overlay callback (per-monitor overlay on switch)
   - state-changed callback (settings window refresh + tray update)
   - tray icon + menu builder
   - WinEvent hooks (create/show/hide/destroy/movesizeend/foreground)
8. Message loop; `AppContext::Shutdown()` on quit: unregister hotkeys, reveal all windows,
   restore wallpapers (if `restoreWallpaperOnExit`), save config.

## Switching flow (`WorkspaceEngine::Switch`)

1. Resolve target monitor (mouse-under-cursor mode, else primary) → stable monitor ID
   (`\\?\DISPLAY#...` path from `GetMonitorInfo` + device path — survives re-ordering).
2. `WorkspaceManager::SetActiveWorkspace(monitor, ws)` — adjacency resolution wraps around
   the ordered workspace list.
3. Hide windows: every tracked window assigned to that monitor whose workspace ≠ target is
   hidden (`ShowWindow(SW_HIDE)`; mode Minimize/Hide per settings). **Before** hiding,
   `RecoveryManager::RecordHide()` persists hwnd + process identity (PID + creation time +
   exe path + title) to `journal.json` (write-to-temp + rename).
4. Show windows: windows assigned to the target workspace are shown (restore minimized/
   maximized state recorded at hide time), then their journal entries are removed.
5. Wallpaper: if the workspace has an image, apply per-monitor via `IDesktopWallpaper`;
   an empty image means "leave the desktop as is".
6. `OnOverlayShow` fires → UI shows the brief overlay; `OnStateChanged` fires → tray/menu.

## Recovery (`RecoveryManager`)

- **Journal entry**: hwnd, PID + process creation time + exe path/name, title, class,
  monitor stable id, workspace id, was-minimized/was-maximized, hidden-at timestamp.
- **Startup**: `Load()` → entries restored by *process identity*, not hwnd (handles are not
  valid across sessions). A stale entry (process gone) is dropped with a log line.
- **Corruption**: parse failure → treat as empty (fail-safe: do not hide/show anything based
  on partial data). Entries are removed as soon as a window is shown; the journal is empty
  in steady state.
- **Crash mid-hide**: the entry was written *before* the hide, so the next start re-shows it.

## Wallpaper (`WallpaperManager`)

- COM object `IDesktopWallpaper`, one per process. `GetWallpaper(monitor)`/`SetWallpaper`.
- On engine init: capture per-monitor current image path, fit mode and background color into
  `config.originalWallpapers` (persisted so an abnormal exit still allows restore via
  Settings → Advanced, or the next run's shutdown restore).
- Guard rails: empty monitor id → refuse (would clobber all monitors); missing file → warn
  and skip; per-monitor override > workspace default.

## Config (`ConfigService`)

- Single `config.json`, schema v1. Save = rotate main → backup, then write main atomically
  (so the backup lags one save cycle by design).
- Load statuses: `Loaded` / `FreshDefaults` / `RecoveredFromBackup` / `Migrated`.
- Normalize (after load): renumber `orderIndex`, drop duplicate/empty ids, repair invalid
  accent colors, drop dangling `monitorState` references, drop empty hotkeys.

## Hotkeys (`HotkeyManager`)

- `RegisterHotKey` on the message window. Bindings expand: a `SwitchToWorkspaceByNumber`
  binding with key `D1` registers ids for 1..9.
- Conflict detection compares **logical bindings** (action + numeric index), not display
  strings: two different actions on the same combo = conflict, neither is registered and the
  conflict list is surfaced to the Settings hotkey page.

## Packaging note (MinGW)

MinGW's gcc spec auto-links `%:if-exists(default-manifest.o%s)` — a default manifest object.
Adding an embedded manifest resource (`1 24`) on top fails at link time with
`.rsrc merge failure: multiple non-default manifests`. Hence: no manifest in `app.rc`;
PerMonitorV2 DPI is requested at runtime instead. `packaging/app.manifest` is kept for
reference (MSVC builds or future use).
