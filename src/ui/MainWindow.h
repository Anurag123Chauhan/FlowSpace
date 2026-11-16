#pragma once

#include "core/ConfigService.h"
#include "core/Models.h"
#include "core/RecoveryManager.h"
#include "engine/WorkspaceEngine.h"
#include "platform/MonitorManager.h"
#include "platform/StartupManager.h"
#include "platform/WallpaperManager.h"

#include <Windows.h>
#include <functional>
#include <string>
#include <vector>

namespace flowspace::ui
{
    /// <summary>
    /// The settings window: frameless dark UI with a left navigation rail and eight pages
    /// (Overview, Workspaces, Monitors, Shortcuts, Behavior, App Rules, Recovery, About).
    /// Everything is custom-drawn; interactions are hit-tested clicks. The window never
    /// appears in the engine's window tracking (owned by the FlowSpace process).
    /// </summary>
    class MainWindow
    {
    public:
        struct Context
        {
            engine::WorkspaceEngine *engine = nullptr;
            core::ConfigService *config = nullptr;
            core::RecoveryManager *recovery = nullptr;
            platform::MonitorManager *monitors = nullptr;
            platform::WallpaperManager *wallpaper = nullptr;
            platform::StartupManager *startup = nullptr;
            std::function<void()> onConfigSaved; // e.g. re-apply hotkeys + startup entry
        };

        static void RegisterMainWindowClass(HINSTANCE instance);
        static void Open(HINSTANCE instance, Context context); // creates or activates
        static MainWindow *Instance() { return instance_; }
        HWND Hwnd() const { return hwnd_; }

        // Called by the app when engine state changed (switch, window events).
        void NotifyStateChanged();

    private:
        MainWindow();
        ~MainWindow();

        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

        void Paint(HDC dc);
        void Layout(); // computes page/nav rects for hit-testing

        // Hotspots: rebuilt during each paint; clicked by id.
        struct Hotspot
        {
            int id;
            int index;
            RECT rect;
        };
        void AddHotspot(int id, int index, const RECT &rect);
        bool OnClick(int x, int y);

        // --- pages ---
        void PaintNav(HDC dc, const RECT &client);
        void PaintPageOverview(HDC dc, const RECT &page);
        void PaintPageWorkspaces(HDC dc, const RECT &page);
        void PaintPageMonitors(HDC dc, const RECT &page);
        void PaintPageShortcuts(HDC dc, const RECT &page);
        void PaintPageBehavior(HDC dc, const RECT &page);
        void PaintPageAppRules(HDC dc, const RECT &page);
        void PaintPageRecovery(HDC dc, const RECT &page);
        void PaintPageAbout(HDC dc, const RECT &page);

        // Drawing helpers.
        void DrawTextAt(HDC dc, const std::wstring &text, RECT rect, HFONT font, COLORREF color, UINT format);
        void Fill(HDC dc, const RECT &rect, COLORREF color);
        void DrawToggle(HDC dc, const RECT &rect, bool on);
        std::wstring PickImageFile();

        // Rename editor (keyboard-captured; no child controls).
        void BeginRename(int workspaceIndex);
        void CommitRename(bool apply);
        bool HandleRenameKey(WPARAM vk, WPARAM Char);
        void PaintRenameBox(HDC dc);

        // Shortcut capture mode: next key press becomes a binding.
        void BeginCapture(int bindingIndex);
        bool HandleCaptureKey(WPARAM vk);

        // Applies config to disk + notifies the app (hotkeys/startup).
        void SaveConfig();

        static MainWindow *instance_;

        HWND hwnd_ = nullptr;
        HINSTANCE hinstance_ = nullptr;
        Context ctx_;
        int page_ = 0;
        std::vector<Hotspot> hotspots_;
        int clientWidth_ = 0;
        int clientHeight_ = 0;
        int scrollOffset_ = 0;
        int pageContentHeight_ = 0;

        // Rename editor state.
        bool renaming_ = false;
        int renameIndex_ = -1;
        std::wstring renameBuffer_;

        // Shortcut capture state.
        bool capturing_ = false;
        int captureIndex_ = -1;

        HFONT fontTitle_ = nullptr;
        HFONT fontH2_ = nullptr;
        HFONT fontBody_ = nullptr;
        HFONT fontSmall_ = nullptr;
        HFONT fontIcon_ = nullptr;
    };
}
