# Uninstalling FlowSpace

## Installed version

1. Exit FlowSpace from the system tray.
2. Open **Windows Settings > Apps > Installed apps**.
3. Find **FlowSpace**, choose **Uninstall**, and follow the prompts.

You can also select **Uninstall FlowSpace** from the Start Menu.

The uninstaller removes the application files and shortcuts. It does not remove your
personal configuration or logs.

## Portable ZIP version

1. Exit FlowSpace from the system tray.
2. Delete the folder where you extracted the ZIP.

## Remove personal data

To remove configuration, recovery data, and logs, delete:

```text
%LOCALAPPDATA%\FlowSpace\
```

If FlowSpace was configured to start with Windows, disable that setting in FlowSpace first
or remove its Startup entry from Windows Settings.
