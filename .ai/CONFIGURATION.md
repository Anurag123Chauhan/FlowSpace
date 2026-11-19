# FlowSpace configuration reference

Config lives at `%LOCALAPPDATA%\FlowSpace\config.json` with a `config.backup.json` that
lags one save cycle. Delete both to regenerate defaults.

```jsonc
{
  "schemaVersion": 1,
  "settings": {
    "hideMode": 0,                      // 0 = SW_HIDE, 1 = minimize
    "showOverlay": true,                // compact switch notification after a workspace change
    "overlayAnimation": "slide",        // "none" disables the fade/scale animation
    "overlayAnimationMs": 180,          // fade duration (clamped to 0-2000 ms in Normalize)
    "switchByMouseMonitor": true,       // hotkeys act on the monitor under the cursor
    "wallpaperManagementEnabled": true, // master switch for per-workspace wallpapers
    "restoreWallpaperOnExit": true,     // put the original desktop back on Exit
    "startWithWindows": false,          // HKCU Run entry (no admin)
    "groupWindowsByApplication": true,  // new windows inherit the app's workspace
    "wallpaperCaptureInitialized": false
  },
  "workspaces": [
    {
      "id": "ws-1",                     // stable, never user-visible
      "name": "Work",
      "icon": "briefcase",              // icon key used by tray/switcher
      "accentColor": "#4389D8",         // RRGGBB; shown in UI accents
      "orderIndex": 0,
      "defaultWallpaperPath": "",       // empty = leave the monitor's wallpaper alone
      "wallpaperFit": 3,                // IDesktopWallpaper fit (3 = fill)
      "wallpaperBackgroundColor": "#000000",
      "perMonitorWallpaper": {},        // optional per-monitor overrides by stable id
      "perMonitorWallpaperFit": {},
      "overlayBackgroundColor": "",
      "overlayTextColor": "#FFFFFFFF",
      "overlayPattern": "none",
      "overlayPatternOpacity": 0.10,
      "overlayAnimation": "slide",
      "overlayAnimationMs": 180
    }
  ],
  "hotkeys": [
    // modifiers: Ctrl, Alt, Shift, Win; key: Left/Right/Space/F1-F12/D1-D9/letters/...
    { "action": 0, "enabled": true, "modifiers": "Ctrl+Alt", "key": "Left"  },
    { "action": 1, "enabled": true, "modifiers": "Ctrl+Alt", "key": "Right" }
  ],
  "appRules": [
    // auto-assign by executable: new windows from these apps land in the workspace
    { "id": "rule-1", "enabled": true, "exeName": "notepad.exe", "targetWorkspaceId": "ws-1" }
  ],
  "monitorState": {
    // "\\\\?\\DISPLAY#..." -> active workspace id; dangling ids are dropped on load
  },
  "originalWallpapers": [
    // captured on engine init; used for restore-on-exit and manual "restore" in Advanced
  ]
}
```

## Settings (`Settings` struct)

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `hideMode` | int | 0 | 0 = `SW_HIDE` (taskbar button disappears), 1 = minimize (taskbar button remains) |
| `startWithWindows` | bool | false | Register FlowSpace in the HKCU Run key |
| `switchByMouseMonitor` | bool | true | Hotkeys act on the monitor under the cursor |
| `wallpaperManagementEnabled` | bool | true | Apply workspace wallpapers through `IDesktopWallpaper` |
| `restoreWallpaperOnExit` | bool | true | Put the user's wallpapers back when FlowSpace closes |
| `paused` | bool | false | Suspend workspace management (hotkeys stay alive) |
| `groupWindowsByApplication` | bool | true | New windows inherit the workspace of their app's other windows |
| `showOverlay` | bool | true | Display a compact notification after switching |
| `overlayAnimation` | string | "slide" | `none` \| `slide` \| `fade` \| `scale` |
| `overlayAnimationMs` | int | 180 | Fade duration, clamped to 0-2000 ms on load |
| `wallpaperCaptureInitialized` | bool | false | Set once original wallpapers have been captured |

The settings UI follows the Windows apps light/dark preference automatically; there is no persisted theme option.

## Actions (`HotkeyAction` enum)

| # | Action | Default binding |
|---|--------|-----------------|
| 0 | `PreviousWorkspace` | `Ctrl+Alt+Left` |
| 1 | `NextWorkspace` | `Ctrl+Alt+Right` |
| 2 | `SwitchToWorkspaceByNumber` | `Ctrl+Alt+1` (registers 1..9) |
| 3 | `MoveActiveWindowToPreviousWorkspace` | `Ctrl+Alt+Shift+Left` |
| 4 | `MoveActiveWindowToNextWorkspace` | `Ctrl+Alt+Shift+Right` |
| 5 | `MoveActiveWindowToWorkspaceByNumber` | `Ctrl+Alt+Shift+1` |
| 6 | `OpenWorkspaceSwitcher` | `Ctrl+Alt+Space` |
| 7 | `RevealAllWindows` | `Ctrl+Alt+F12` |
| 8 | `OpenSettings` | `Ctrl+Alt+O` |

## Valid hotkey key names

`Left, Right, Up, Down, Space, Tab, Back, D0-D9, F1-F12, Home, End, Insert, PageUp,
PageDown, OemComma, OemPeriod, Minus, Plus, A-Z`. Modifier names: `Ctrl` (or `Control`),
`Alt`, `Shift`, `Win` (case-insensitive).

## Files

| File | Purpose | Corruption behavior |
|------|---------|---------------------|
| `config.json` | main config | recovered from backup; else fresh defaults |
| `config.backup.json` | previous save | if both corrupt → fresh defaults |
| `journal.json` | pending hidden windows | discarded (fail-safe, nothing restored) |
| `logs\flowspace.log` | rotating app log | truncated/recreated |

## Migration

`schemaVersion` < 1 (diagnostic-era config) is migrated: default names/hotkeys attached,
schema stamped to 1. Unknown fields are ignored; new fields default.

## Load statuses (`ConfigLoadStatus`)

- `Loaded` — parsed successfully
- `RecoveredFromBackup` — main file corrupt, backup used
- `FreshDefaults` — no file existed (or all files unreadable): defaults created

## Normalization (runs after every load)

- Every workspace gets an id; exact-duplicate ids dropped; `orderIndex` renumbered contiguously
- Invalid accent colors repaired to `#4389D8`; dangling `monitorState` references dropped
- Non-existent wallpaper image paths cleared
- `overlayAnimationMs` clamped to 0-2000
- Hotkey entries without a key are dropped
