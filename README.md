# FlowSpace

**One PC, many workspaces — one per monitor.**

FlowSpace gives every monitor its own independent set of workspaces. Switch workspaces on one screen without touching the others — like virtual desktops, but per monitor. Your windows, wallpapers, and focus stay exactly where you put them.

## How FlowSpace makes life easier

| Situation | Without FlowSpace | With FlowSpace |
|-----------|-------------------|----------------|
| **Work + personal on the same PC** | One cluttered desktop, everything mixed together | Monitor 1 = Work (code, email, Slack); Monitor 2 = Personal (music, browser, notes). Switch independently. |
| **Multitasking across projects** | Alt+Tab through dozens of windows | Each monitor shows only the workspace you need. One hotkey flips the whole screen. |
| **Accidentally closed or crashed** | Windows are gone | FlowSpace journals hidden windows and restores them on next start — even after a crash or power loss. |
| **Different moods, different desktops** | Same wallpaper everywhere | Each workspace can have its own wallpaper per monitor. Work = focused; Gaming = vibrant. |

## Get started

### Download

Get the latest release from the [Releases page](https://github.com/Anurag123Chauhan/FlowSpace/releases):

- **Installer** (`FlowSpace-<version>-windows-x64-setup.exe`) — recommended. Adds Start Menu and optional desktop shortcut.
- **ZIP** (`FlowSpace-<version>-windows-x64.zip`) — portable. Extract anywhere and run `flowspace.exe`.

No compiler, CMake, or source code needed.

### Install & run

1. Download and run the installer (or extract the ZIP).
2. FlowSpace starts hidden in the system tray (blue icon).
3. Configuration and logs live at `%LOCALAPPDATA%\FlowSpace\`.

To uninstall: exit FlowSpace from the tray, then use **Installed apps** in Windows Settings or **Uninstall FlowSpace** from the Start Menu. See [UNINSTALL.md](UNINSTALL.md) for details.

## How to use it

FlowSpace lives in your tray and is controlled mostly by keyboard:

| Hotkey | What it does |
|--------|--------------|
| `Ctrl+Alt+Left` / `Ctrl+Alt+Right` | Switch to previous/next workspace on the monitor under your mouse |
| `Ctrl+Alt+1` … `Ctrl+Alt+9` | Jump directly to workspace 1–9 |
| `Ctrl+Alt+Shift+Left` / `Ctrl+Alt+Right` | Move the active window to the previous/next workspace |
| `Ctrl+Alt+Space` | Open the workspace switcher popup (arrows + Enter to select) |
| `Ctrl+Alt+F12` | Reveal all hidden windows |
| `Ctrl+Alt+O` | Open Settings |

Right-click the tray icon to switch workspaces per monitor, pause, reveal all, or open Settings. Left-click the tray icon to open Settings.

### Settings

Open Settings (`Ctrl+Alt+O` or tray → Open Settings) to:

- **Workspaces** — create, rename, recolor, reorder, duplicate, or delete workspaces
- **Wallpaper** — assign a different wallpaper to each workspace, per monitor
- **Hotkeys** — change or disable shortcuts (conflicts are flagged automatically)
- **Behavior** — hide vs. minimize windows, start with Windows, pause management, and more
- **App Rules** — automatically send specific apps (e.g. notepad.exe) to a workspace
- **Recovery** — view and restore hidden windows from the journal

### App rules

App rules send matching applications to a workspace automatically.

**Add a rule in Settings:**

1. Focus a window of the app you want to assign (for example, Notepad).
2. Press `Ctrl+Alt+O` and open **App Rules**.
3. Click **+ Add rule**.

FlowSpace captures the `.exe` name of the app you were using before Settings opened, and targets the workspace that was active on the monitor where that app was. The rule is saved immediately. In the App Rules list you can toggle a rule on/off or delete it.

**Advanced conditions (edit `config.json`):**

The Settings page only shows the executable, target workspace, and on/off state. To narrow a rule by full path or window title, exit FlowSpace from the tray first (it rewrites the config on changes), then edit the `appRules` array in `%LOCALAPPDATA%\FlowSpace\config.json` and start FlowSpace again:

```jsonc
"appRules": [
  {
    "id": "rule-1",
    "enabled": true,
    "exeName": "notepad.exe",       // exact file name, case-insensitive
    "exePath": "",                  // optional full path, case-insensitive
    "titleContains": "Draft",       // optional title substring, case-sensitive
    "targetWorkspaceId": "ws-1",    // workspace id from the "workspaces" array
    "monitorPreference": 0,         // 0 = any, 1 = follow workspace, 2 = specific monitor
    "monitorStableId": "",          // used only when monitorPreference is 2
    "launchCommand": ""             // reserved; currently unused
  }
]
```

Prefer **+ Add rule** when you need a `monitorStableId` — it captures the long display identifier for you, and hand-written values are easy to get wrong.

**How matching works:**

- Rules are checked in order; the first enabled match wins, and its target workspace must belong to the monitor the window appeared on.
- At least one of `exeName`, `exePath`, or `titleContains` must be set; a rule with all three empty matches nothing.
- `exeName`/`exePath` are exact, case-insensitive comparisons. `titleContains` is a case-sensitive substring match.
- `monitorPreference: 2` restricts the rule to one display; `1` moves the window to a monitor whose active workspace is the target; `0` leaves monitor placement alone.
- If no rule matches, FlowSpace groups windows by application, then falls back to the monitor's active workspace.
- Rules affect **new** windows. Adding a rule does not move windows that are already open — restart FlowSpace to apply rules to them. That startup pass switches the target monitor to the rule's workspace so the window becomes visible.

## Features

- **Per-monitor workspaces** — each monitor has its own active workspace and switch history. Nothing on monitor B changes when monitor A switches.
- **Window hide/show with crash recovery** — hidden windows are journaled to disk before being hidden, and restored automatically on the next start — even after a crash or power loss.
- **Per-workspace wallpapers** — each workspace can have its own image per monitor. Your original desktop is captured and restored on exit.
- **Global hotkeys** with conflict detection — two shortcuts can't silently override each other.
- **Tray icon** — per-monitor workspace menu, pause, reveal-all, exit.
- **Overlay & switcher popup** — brief on-screen notification after switching; `Ctrl+Alt+Space` lists all workspaces.
- **Robust config** — `config.json` with a rotating backup. A corrupt main file recovers from the backup automatically.

## System requirements

- Windows 10 (1903+) or Windows 11, x64
- No administrator rights required

## License

FlowSpace is distributed under the MIT License; see [`LICENSE`](LICENSE).
Third-party components retain their own licenses, listed in
[`THIRD-PARTY-NOTICES.txt`](THIRD-PARTY-NOTICES.txt).

## Engineering reference docs

FlowSpace keeps its architecture, configuration, quality, and testing references in [`docs/engineering/`](docs/engineering/). Coding agents should also start with [`AGENTS.md`](AGENTS.md).

| File | Purpose |
|------|---------|
| `AGENTS.md` | Primary instructions for AI coding agents (read first) |
| [`ARCHITECTURE.md`](docs/engineering/ARCHITECTURE.md) | Module responsibilities, Windows APIs, data flow |
| [`CODE_QUALITY.md`](docs/engineering/CODE_QUALITY.md) | C++ conventions and cleanup guidance |
| [`CONFIGURATION.md`](docs/engineering/CONFIGURATION.md) | `config.json` schema reference |
| [`TESTING.md`](docs/engineering/TESTING.md) | Manual integration test checklist |

See [`AGENTS.md`](AGENTS.md) for the mandatory agent workflow.

---

*FlowSpace is an independent per-monitor workspace manager for Windows 10/11. Built with native C++ (MinGW). Source builds require MinGW-w64 UCRT64, CMake, and Ninja — see the build section in [`AGENTS.md`](AGENTS.md) if you want to compile from source.*
