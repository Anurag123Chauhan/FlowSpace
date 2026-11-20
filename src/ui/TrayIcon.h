#pragma once

#include <Windows.h>
#include <shellapi.h>
#include <functional>

namespace flowspace::ui
{
    /// Shows the tray icon and menu for settings, workspace switching, and app actions.
    /// Menu events are handled by the app's main message window.
    class TrayIcon
    {
      public:
        // id = tray callback message id; menuIdBase = first command id for generated menu items.
        void Initialize(HWND messageWindow, UINT callbackMessage, UINT menuIdBase);
        void Shutdown();

        // Adds the icon; existing icon is replaced on re-call (e.g. pause state change).
        bool Update(bool paused);

        // Returns the selected command, 0 if dismissed, or -1 for a left-click.
        int HandleCallbackMessage(LPARAM lParam);

        void SetWorkspaceMenuBuilder(std::function<void(HMENU)> builder) { workspaceMenuBuilder_ = std::move(builder); }

      private:
        HWND window_ = nullptr;
        UINT callbackMessage_ = 0;
        UINT menuIdBase_ = 0;
        bool added_ = false;
        bool paused_ = false;
        std::function<void(HMENU)> workspaceMenuBuilder_;
    };
}
