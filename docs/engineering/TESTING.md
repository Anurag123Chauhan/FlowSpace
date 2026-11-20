# FlowSpace manual test checklist

Automated suite: `build\cmake\flowspace_tests.exe` (core-logic checks, no external framework).
Everything below is the live checklist — re-run after touching the engine, window management,
wallpaper, or shutdown paths.

Before you start: note your current desktop wallpaper (Personalization → Background)
so you can verify restore-on-exit.

## 1. Startup

- [ ] `build\cmake\flowspace.exe` — no error box, app appears in tray (blue icon).
- [ ] Log `%LOCALAPPDATA%\FlowSpace\logs\flowspace.log` shows:
  `IDesktopWallpaper COM object created`, `Captured original wallpaper state for ... monitor(s)`,
  `WinEvent hooks installed`, `FlowSpace started.` — and **no** hotkey warnings.
- [ ] Second instance: running `flowspace.exe` again shows "FlowSpace is already running."

## 2. Workspace switching (per monitor)

- [ ] `Ctrl+Alt+Right` / `Ctrl+Alt+Left` on the primary: current windows hide, other
      workspaces' windows appear; overlay flashes the workspace name.
- [ ] With the mouse over the **secondary** monitor, `Ctrl+Alt+Right` switches only that
      monitor; primary windows are unaffected.
- [ ] Switch to an empty workspace: desktop shows the workspace wallpaper (if configured)
      or the existing one (if empty).
- [ ] `Ctrl+Alt+1..9` jumps directly; `Ctrl+Alt+Space` opens the switcher popup; arrows +
      Enter select.

## 3. Window handling

- [ ] Minimized window stays minimized across hide/show; restored windows come back on top.
- [ ] `Ctrl+Alt+Shift+Right` moves the focused window to the next workspace (it hides now,
      reappears when you switch to that workspace).
- [ ] Reveal all (`Ctrl+Alt+F12` or tray → Reveal all): every FlowSpace-hidden window is
      visible again, journal empties (`journal.json` == `[]`).
- [ ] Close a hidden app (e.g. from tray of that app): next switch logs the stale entry
      being dropped, no zombie in the journal.

## 4. Wallpaper

- [ ] Assign an image to a workspace (Settings → Workspaces → Wallpaper): switching to it
      on monitor 1 changes only monitor 1; monitor 2 keeps its wallpaper.
- [ ] Log shows `Wallpaper applied: monitor '...' -> '...'`.
- [ ] Exit via tray → Exit: wallpaper returns to the pre-run picture (log: restore, no
      errors; registry `HKCU\Control Panel\Desktop\Wallpaper` may show
      `TranscodedWallpaper` — Windows 11 re-encodes on apply; visually it is the same image).

## 5. Crash recovery

- [ ] Hide some windows in a non-active workspace, then kill the process
      (`Stop-Process -Name flowspace`). `journal.json` still lists them.
- [ ] Start again: hidden windows are re-shown automatically (recovery log lines), journal
      empties. Verify by PID/exe, not hwnd (handles change across sessions).
- [ ] Corrupt `journal.json` (write `[{broken`), restart: app starts normally, nothing
      spurious is shown/hidden (fail-safe).

## 6. Config robustness

- [ ] Corrupt `config.json` (truncate it), restart: `RecoveredFromBackup` in the log, last
      backup state applied, app fully functional.
- [ ] Corrupt both config and backup: app starts with fresh default workspaces.
- [ ] Make a hotkey conflict (bind two actions to the same combo in Settings): conflict is
      reported in the Hotkeys page and **neither** fires; fix it and both register.

## 7. Display changes

- [ ] Disconnect/reconnect a monitor (or Win+P): no crash; log shows reconciliation; each
      monitor keeps its active workspace (`WM_DISPLAYCHANGE` path).
- [ ] Workspaces created while a monitor was absent don't break switching when it returns.

## 8. Settings UI

- [ ] Tray → Settings opens `FlowSpace Settings`; all pages render:
  Overview, Workspaces, Monitors, Shortcuts, Behavior, App Rules, Recovery, About.
- [ ] Create / rename / recolor / reorder / duplicate / delete a workspace; tray menu and
      switcher reflect changes immediately.
- [ ] Toggle pause (tray): switching disabled; toggling again re-enables.

## 9. Shutdown

- [ ] Tray → Exit (or `scripts\stop.ps1`): log ends with `Reveal all: ...` and
      `FlowSpace stopped.`; no orphan processes; wallpaper restored; mutex released
      (restart works immediately).

## 10. Diagnostic CLI

- [ ] `build\cmake\flowspace-diag.exe` prints monitor and manageable-window reports and
      exits 0 without touching any state.

## Release workflow

Tag pushes and manual runs start `.github/workflows/release.yml`; only tags publish a GitHub release. CI builds and tests the app, creates the ZIP and installer with `scripts/package-release.ps1`, verifies both packages, and tests installer/uninstaller behavior.
