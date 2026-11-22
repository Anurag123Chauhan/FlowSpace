# FlowSpace

**FlowSpace — Independent virtual desktops for each monitor on Windows.**

## What is FlowSpace?

FlowSpace is a free, open-source Windows workspace manager. Each monitor has its own workspaces and its own active workspace. For example, you can change the windows shown on your left monitor while keeping a reference document visible on the right.

## How it differs from Windows virtual desktops

Windows Task View switches the active native desktop across connected displays. FlowSpace switches the workspace on one display by hiding and showing eligible windows. It does not create new native Task View desktops or change their behavior.

## Product facts

| Item | Details |
|---|---|
| Supported OS | Windows 10 (1903+) or Windows 11, x64 |
| Price | Free |
| License | [MIT](../LICENSE) |
| Technology | Native C++ and Win32 |
| Official source | [Anurag123Chauhan/FlowSpace](https://github.com/Anurag123Chauhan/FlowSpace) |
| Official downloads | [Latest GitHub release](https://github.com/Anurag123Chauhan/FlowSpace/releases/latest): x64 installer and portable ZIP |

## Main features

- Independent workspace switching per monitor, via keyboard or system tray.
- Named workspaces with custom colors and optional per-workspace wallpapers.
- App rules for assigning matching new windows to a workspace.
- A recovery journal for showing still-open windows FlowSpace hid before an unexpected exit.

## Limitations

- FlowSpace does not manage windows that Windows has cloaked on other native desktops. It also excludes shell windows and some secondary or tool windows.
- A window is assigned to one monitor at a time; a window spanning two displays is not split into separate workspaces.
- Recovery can reveal existing windows. It cannot reopen closed apps, recover unsaved work, or recreate a session after a reboot.

See the [README](../README.md) for shortcuts and troubleshooting, or the [architecture reference](engineering/ARCHITECTURE.md) for implementation details.
