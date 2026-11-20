#pragma once

#include "core/ConfigService.h"
#include "core/Models.h"
#include "core/RecoveryManager.h"
#include "engine/WorkspaceEngine.h"
#include "platform/MonitorManager.h"
#include "platform/StartupManager.h"
#include "platform/WallpaperManager.h"
#include "platform/HotkeyManager.h"

#include "ui/DesignSystem.h"

#include <Windows.h>
#include <functional>
#include <string>
#include <vector>

namespace flowspace::ui
{
    /// Custom-drawn settings window with eight pages and light/dark theme support.
    /// It handles clicks by hit-testing and is excluded from workspace management.
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
        bool EnsureBackBuffer(HDC dc, int width, int height);
        void ReleaseBackBuffer();
        void PaintPage(HDC dc, int pageIndex, const RECT &body);
        void PaintPageHeader(HDC dc, const RECT &rect, int pageIndex);
        void PaintScrollbar(HDC dc, const RECT &viewport);
        int ContentHeight() const { return pageContentHeight_; }
        void SetContentEnd(int y);

        void PaintNav(HDC dc, const RECT &client);

        void PaintPageOverview(HDC dc, const RECT &body);
        void PaintPageWorkspaces(HDC dc, const RECT &body);
        void PaintPageMonitors(HDC dc, const RECT &body);
        void PaintPageShortcuts(HDC dc, const RECT &body);
        void PaintPageBehavior(HDC dc, const RECT &body);
        void PaintPageAppRules(HDC dc, const RECT &body);
        void PaintPageRecovery(HDC dc, const RECT &body);
        void PaintPageAbout(HDC dc, const RECT &body);

        void DrawTextAt(HDC dc, const std::wstring &text, RECT rect, HFONT font, COLORREF color, UINT format);
        void Fill(HDC dc, const RECT &rect, COLORREF color);
        std::wstring PickImageFile();

        void BeginRename(int workspaceIndex);
        void CommitRename(bool apply);
        bool HandleRenameKey(WPARAM vk, WPARAM Char);
        void PaintRenameBox(HDC dc, const RECT &rect);

        void ShowWorkspaceMenu(int workspaceIndex);
        void RunWorkspaceAction(int id, int workspaceIndex);
        bool PickWorkspaceWallpaper(int workspaceIndex);

        void BeginCapture(int bindingIndex);
        bool HandleCaptureKey(WPARAM vk);

        void ApplySystemTheme();

        void SaveConfig();

        struct Hotspot
        {
            int id;
            int index;
            RECT rect;
        };
        void AddHotspot(int id, int index, const RECT &rect);
        bool OnClick(int x, int y);
        void OnMouseMove(int x, int y);
        void OnMouseLeave();
        void OnMouseWheel(int delta);

        static MainWindow *instance_;

        HWND hwnd_ = nullptr;
        HINSTANCE hinstance_ = nullptr;
        Context ctx_;
        AppTheme theme_;
        int page_ = 0;
        std::vector<Hotspot> hotspots_;
        RECT hotspotClip_{}; // when set, content hotspots are clipped to this rect
        int clientWidth_ = 0;
        int clientHeight_ = 0;
        int scrollOffset_ = 0;
        int pageContentHeight_ = 0;
        int contentTop_ = 0; // body.top (viewport top minus scroll offset)
        RECT viewport_{};    // visible content area (below the page header)

        HDC backDc_ = nullptr;
        HBITMAP backBitmap_ = nullptr;
        int backWidth_ = 0;
        int backHeight_ = 0;

        int hoveredId_ = -1;
        int hoveredIndex_ = -1;
        TRACKMOUSEEVENT trackMouse_{};

        bool renaming_ = false;
        int renameIndex_ = -1;
        std::wstring renameBuffer_;
        RECT renameRect_ = {}; // row text rect captured during paint

        bool capturing_ = false;
        int captureIndex_ = -1;
    };

} // namespace flowspace::ui
