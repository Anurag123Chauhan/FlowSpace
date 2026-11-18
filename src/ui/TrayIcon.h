#pragma once

#include <Windows.h>
#include <shellapi.h>
#include <functional>

namespace flowspace::ui
{
    /// <summary>
    /// System tray icon with a context menu (open settings, per-monitor workspace switching,
    /// reveal all, pause, wallpaper toggle, restart engine, exit). The callback message is
    /// delivered to the app's message window, keeping tray handling on the main thread.
    /// </summary>
    class TrayIcon
    {
    public:
        // id = tray callback message id; menuIdBase = first command id for generated menu items.
        void Initialize(HWND messageWindow, UINT callbackMessage, UINT menuIdBase);
        void Shutdown();

        // Adds the icon; existing icon is replaced on re-call (e.g. pause state change).
        bool Update(bool paused);

        // Explorer restarted ("TaskbarCreated"): the old icon is gone, add it again.
        bool Readd() { added_ = false; return Update(paused_); }

        // Called by the message window on the tray callback message:
        // returns the selected menu command id (0 when the menu was dismissed), or
        // -1 when the event was a plain left-click (settings open).
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
