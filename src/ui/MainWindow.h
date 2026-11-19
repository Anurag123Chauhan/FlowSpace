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
    /// <summary>
    /// The settings window: a Technical Grid / Swiss International redesign with a fixed
    /// sidebar, eight content pages, light/dark/system theming, reusable GDI components and
    /// a flicker-free double-buffered renderer. Everything is custom-drawn; interactions are
    /// hit-tested clicks. The window never appears in the engine's window tracking (owned by
    /// the FlowSpace process).
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

        // Painting & layout.
        void Paint(HDC dc);
        bool EnsureBackBuffer(HDC dc, int width, int height);
        void ReleaseBackBuffer();
        void PaintPage(HDC dc, int pageIndex, const RECT &body);
        void PaintPageHeader(HDC dc, const RECT &rect, int pageIndex);
        void PaintScrollbar(HDC dc, const RECT &viewport);
        int ContentHeight() const { return pageContentHeight_; }
        void SetContentEnd(int y);

        // --- shell ---
        void PaintNav(HDC dc, const RECT &client);

        // --- pages ---
        void PaintPageOverview(HDC dc, const RECT &body);
        void PaintPageWorkspaces(HDC dc, const RECT &body);
        void PaintPageMonitors(HDC dc, const RECT &body);
        void PaintPageShortcuts(HDC dc, const RECT &body);
        void PaintPageBehavior(HDC dc, const RECT &body);
        void PaintPageAppRules(HDC dc, const RECT &body);
        void PaintPageRecovery(HDC dc, const RECT &body);
        void PaintPageAbout(HDC dc, const RECT &body);

        // Drawing helpers (delegated to DesignSystem where possible).
        void DrawTextAt(HDC dc, const std::wstring &text, RECT rect, HFONT font, COLORREF color, UINT format);
        void Fill(HDC dc, const RECT &rect, COLORREF color);
        std::wstring PickImageFile();

        // Rename editor (keyboard-captured; no child controls).
        void BeginRename(int workspaceIndex);
        void CommitRename(bool apply);
        bool HandleRenameKey(WPARAM vk, WPARAM Char);
        void PaintRenameBox(HDC dc, const RECT &rect);

        // Workspace row menu + shared action handler.
        void ShowWorkspaceMenu(int workspaceIndex);
        void RunWorkspaceAction(int id, int workspaceIndex);
        bool PickWorkspaceWallpaper(int workspaceIndex);

        // Shortcut capture mode: next key press becomes a binding.
        void BeginCapture(int bindingIndex);
        bool HandleCaptureKey(WPARAM vk);

        // Theme.
        void ApplySystemTheme();

        // Applies config to disk + notifies the app (hotkeys/startup).
        void SaveConfig();

        // Hotspots: rebuilt during each paint; clipped to the visible content area.
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
        int contentTop_ = 0;   // body.top (viewport top minus scroll offset)
        RECT viewport_{};      // visible content area (below the page header)

        // Flicker-free back buffer.
        HDC backDc_ = nullptr;
        HBITMAP backBitmap_ = nullptr;
        int backWidth_ = 0;
        int backHeight_ = 0;

        // Hover tracking for understated hover states.
        int hoveredId_ = -1;
        int hoveredIndex_ = -1;
        TRACKMOUSEEVENT trackMouse_{};

        // Rename editor state.
        bool renaming_ = false;
        int renameIndex_ = -1;
        std::wstring renameBuffer_;
        RECT renameRect_ = {}; // row text rect captured during paint

        // Shortcut capture state.
        bool capturing_ = false;
        int captureIndex_ = -1;
    };

} // namespace flowspace::ui
