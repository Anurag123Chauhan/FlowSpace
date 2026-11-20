#include "ui/MainWindow.h"
#include "ui/DesignSystem.h"
#include "ui/Theme.h"
#include "core/StringUtils.h"

#include <spdlog/spdlog.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace flowspace::ui
{
    namespace
    {
        constexpr wchar_t kClassName[] = L"FlowSpaceMainWindow";
        constexpr int kPageMargin96 = 24;
        constexpr int kHeaderHeight96 = 76;
        constexpr int kSectionGap96 = 28;
        constexpr int kRowGap96 = 8;

        // Icon glyphs (Segoe MDL2 / Fluent Icons codepoints).
        constexpr wchar_t GlyphNavOverview = 0xE80F;   // home / dashboard
        constexpr wchar_t GlyphNavWorkspaces = 0xE8A9; // view all (grid of workspaces)
        constexpr wchar_t GlyphNavMonitors = 0xE7F4;   // TV monitor
        constexpr wchar_t GlyphNavShortcuts = 0xE765;  // keyboard
        constexpr wchar_t GlyphNavBehavior = 0xE713;   // settings gear
        constexpr wchar_t GlyphNavRules = 0xE71D;      // all apps
        constexpr wchar_t GlyphNavRecovery = 0xE81C;   // history / journal
        constexpr wchar_t GlyphNavAbout = 0xE946;      // info

        constexpr wchar_t GlyphWs = 0xE80F;        // home
        constexpr wchar_t GlyphMonitor = 0xE7F4;   // TV monitor
        constexpr wchar_t GlyphPlus = 0xE710;      // add
        constexpr wchar_t GlyphDelete = 0xE74D;    // delete
        constexpr wchar_t GlyphReveal = 0xE8A7;    // open in new window
        constexpr wchar_t GlyphApp = 0xE71D;       // all apps
        constexpr wchar_t GlyphWarning = 0xE7BA;   // warning
        constexpr wchar_t GlyphCheck = 0xE73E;     // check mark
        constexpr wchar_t GlyphMore = 0xE712;      // more (ellipsis)

        // Hotspot ids.
        constexpr int HS_NAV = 100;
        constexpr int HS_WS_RENAME = 201;
        constexpr int HS_WS_DUPLICATE = 202;
        constexpr int HS_WS_DELETE = 203;
        constexpr int HS_WS_RESET = 204;
        constexpr int HS_WS_ACCENT = 205;
        constexpr int HS_WS_MENU = 206;
        constexpr int HS_WS_ADD = 207;
        constexpr int HS_SHORT_TOGGLE = 300;
        constexpr int HS_SHORT_CAPTURE = 301;
        constexpr int HS_BEHAVIOR_TOGGLE = 400;
        constexpr int HS_BEHAVIOR_STEP = 401;
        constexpr int HS_RULE_TOGGLE = 500;
        constexpr int HS_RULE_DELETE = 501;
        constexpr int HS_RULE_ADD = 502;
        constexpr int HS_RECOVERY_REVEAL = 600;
        constexpr int HS_MON_SWITCH = 701;

        struct PageDesc
        {
            const wchar_t *title;
            const wchar_t *description;
        };

        constexpr PageDesc kPages[] = {
            {L"Overview", L"Workspace activity, monitor assignments and quick actions at a glance."},
            {L"Workspaces", L"Create, rename and configure workspaces. Open a row's menu for color and more."},
            {L"Monitors", L"Each display keeps its own active workspace. Switch a monitor from the chips."},
            {L"Shortcuts", L"Click a key combination to record a new binding."},
            {L"Behavior", L"Switching, windows, startup and notification options."},
            {L"App Rules", L"Assign matching applications to a workspace automatically."},
            {L"Recovery", L"Every hidden window is journaled and can be restored."},
            {L"About", L"Per-monitor independent workspaces for Windows."},
        };

        constexpr wchar_t kNavLabels[][24] = {
            L"Overview", L"Workspaces", L"Monitors", L"Shortcuts",
            L"Behavior", L"App Rules", L"Recovery", L"About",
        };

        constexpr wchar_t kNavGlyphs[] = {
            GlyphNavOverview, GlyphNavWorkspaces, GlyphNavMonitors, GlyphNavShortcuts,
            GlyphNavBehavior, GlyphNavRules, GlyphNavRecovery, GlyphNavAbout,
        };

        COLORREF WsAccent(const core::Workspace &ws) { return Theme::FromHex(core::Utf8ToWide(ws.accentColor)); }

        std::wstring ActionName(core::HotkeyAction action)
        {
            switch (action)
            {
            case core::HotkeyAction::PreviousWorkspace: return L"Previous workspace";
            case core::HotkeyAction::NextWorkspace: return L"Next workspace";
            case core::HotkeyAction::SwitchToWorkspaceByNumber: return L"Switch to workspace (number)";
            case core::HotkeyAction::MoveActiveWindowToPreviousWorkspace: return L"Move window to previous workspace";
            case core::HotkeyAction::MoveActiveWindowToNextWorkspace: return L"Move window to next workspace";
            case core::HotkeyAction::MoveActiveWindowToWorkspaceByNumber: return L"Move window to workspace (number)";
            case core::HotkeyAction::OpenWorkspaceSwitcher: return L"Open workspace switcher";
            case core::HotkeyAction::RevealAllWindows: return L"Reveal all windows";
            case core::HotkeyAction::OpenSettings: return L"Open settings";
            }
            return L"Unknown action";
        }

        std::wstring MonitorName(const core::MonitorInfo &mon)
        {
            if (!mon.friendlyName.empty())
            {
                return mon.friendlyName;
            }
            if (!mon.deviceName.empty())
            {
                return mon.deviceName;
            }
            return L"Display";
        }

        std::wstring MonitorGeometry(const core::MonitorInfo &mon)
        {
            return std::to_wstring(mon.bounds.width()) + L" \u00D7 " + std::to_wstring(mon.bounds.height()) +
                   L"  \u00B7  " + std::to_wstring(mon.dpiX) + L" DPI";
        }

        const core::Workspace *FindWorkspace(const std::vector<core::Workspace> &workspaces, const std::string &id)
        {
            for (const auto &ws : workspaces)
            {
                if (ws.id == id)
                {
                    return &ws;
                }
            }
            return nullptr;
        }

        // Matches the native title bar to the in-app theme (Windows 10 1809+ / Windows 11).
        void ApplyNativeTitleBar(HWND hwnd, const ThemeTokens &tokens, bool dark)
        {
            if (!hwnd)
            {
                return;
            }
            const BOOL useDark = dark ? TRUE : FALSE;
            constexpr DWORD kDwmUseImmersiveDarkMode = 20;
            DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode, &useDark, sizeof(useDark));

            constexpr DWORD kDwmCaptionColor = 35; // Windows 11 only; ignored elsewhere
            const COLORREF caption = dark ? tokens.sidebar : tokens.bg;
            DwmSetWindowAttribute(hwnd, kDwmCaptionColor, &caption, sizeof(caption));
        }
    }

    MainWindow *MainWindow::instance_ = nullptr;

    void MainWindow::RegisterMainWindowClass(HINSTANCE instance)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &WndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; // fully custom painted (double buffered)
        // Use the embedded FlowSpace icon (resource ID 1) for the title bar,
        // taskbar and Alt+Tab instead of the generic application icon.
        wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXICON),
                                                GetSystemMetrics(SM_CYICON), 0));
        wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                  GetSystemMetrics(SM_CXSMICON),
                                                  GetSystemMetrics(SM_CYSMICON), 0));
        RegisterClassExW(&wc);
    }

    void MainWindow::Open(HINSTANCE instance, Context context)
    {
        if (instance_)
        {
            ShowWindow(instance_->hwnd_, SW_SHOW);
            SetForegroundWindow(instance_->hwnd_);
            return;
        }
        instance_ = new MainWindow();
        instance_->hinstance_ = instance;
        instance_->ctx_ = std::move(context);
        instance_->ApplySystemTheme();

        instance_->hwnd_ = CreateWindowExW(0, kClassName, L"FlowSpace Settings",
                                           WS_OVERLAPPEDWINDOW,
                                           CW_USEDEFAULT, CW_USEDEFAULT, 1120, 760,
                                           nullptr, nullptr, instance, instance_);
        if (instance_->hwnd_)
        {
            // The window handle only exists now, so match the native frame here
            // as well as in WM_CREATE.
            instance_->ApplySystemTheme();
            ShowWindow(instance_->hwnd_, SW_SHOW);
        }
        else
        {
            delete instance_;
            instance_ = nullptr;
        }
    }

    MainWindow::MainWindow() = default;

    MainWindow::~MainWindow()
    {
        ReleaseBackBuffer();
    }

    void MainWindow::ReleaseBackBuffer()
    {
        if (backDc_)
        {
            DeleteDC(backDc_);
            backDc_ = nullptr;
        }
        if (backBitmap_)
        {
            DeleteObject(backBitmap_);
            backBitmap_ = nullptr;
        }
        backWidth_ = 0;
        backHeight_ = 0;
    }

    bool MainWindow::EnsureBackBuffer(HDC dc, int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return false;
        }
        if (backDc_ && backBitmap_ && backWidth_ == width && backHeight_ == height)
        {
            return true;
        }

        ReleaseBackBuffer();
        HDC screen = GetDC(hwnd_);
        HDC source = screen ? screen : dc;
        backDc_ = CreateCompatibleDC(source);
        backBitmap_ = CreateCompatibleBitmap(source, width, height);
        if (screen)
        {
            ReleaseDC(hwnd_, screen);
        }
        if (!backDc_ || !backBitmap_)
        {
            ReleaseBackBuffer();
            return false;
        }
        SelectObject(backDc_, backBitmap_);
        backWidth_ = width;
        backHeight_ = height;
        return true;
    }

    LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_CREATE)
        {
            auto create = reinterpret_cast<CREATESTRUCTW *>(lParam);
            auto *created = reinterpret_cast<MainWindow *>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
            if (!created)
            {
                return 0;
            }
            // Make the handle available to handlers during creation, before
            // CreateWindowExW has returned to Open().
            created->hwnd_ = hwnd;
            return created->HandleMessage(msg, wParam, lParam);
        }
        auto self = reinterpret_cast<MainWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self)
        {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        if (msg == WM_DESTROY)
        {
            instance_ = nullptr;
            delete self;
            return 0;
        }
        return self->HandleMessage(msg, wParam, lParam);
    }

    LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            const UINT dpi = GetDpiForWindow(hwnd_);
            if (dpi > 0)
            {
                theme_.SetDpiScale(static_cast<double>(dpi) / 96.0);
            }
            theme_.SetMode(ThemeMode::System);
            ApplyNativeTitleBar(hwnd_, theme_.Tokens(), theme_.EffectiveMode() == ThemeMode::Dark);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1; // the back buffer paints every pixel
        case WM_SIZE:
            clientWidth_ = LOWORD(lParam);
            clientHeight_ = HIWORD(lParam);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            Paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto *mmi = reinterpret_cast<MINMAXINFO *>(lParam);
            const UINT dpi = GetDpiForWindow(hwnd_);
            const int minW = MulDiv(860, dpi ? dpi : 96, 96);
            const int minH = MulDiv(580, dpi ? dpi : 96, 96);
            mmi->ptMinTrackSize = {minW, minH};
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetCapture(hwnd_);
            return 0;
        case WM_LBUTTONUP:
        {
            ReleaseCapture();
            if (OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
            {
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        case WM_MOUSELEAVE:
            OnMouseLeave();
            break;
        case WM_MOUSEWHEEL:
            OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_KEYDOWN:
            if (renaming_)
            {
                if (HandleRenameKey(wParam, 0)) InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (capturing_ && HandleCaptureKey(wParam))
            {
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_ESCAPE)
            {
                ShowWindow(hwnd_, SW_HIDE);
                return 0;
            }
            break;
        case WM_CHAR:
            if (renaming_ && HandleRenameKey(0, wParam))
            {
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_DPICHANGED:
        {
            const UINT dpi = HIWORD(wParam);
            theme_.SetDpiScale(dpi > 0 ? static_cast<double>(dpi) / 96.0 : 1.0);
            auto *suggested = reinterpret_cast<RECT *>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_SETTINGCHANGE:
            ApplySystemTheme();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }

    void MainWindow::Paint(HDC dc)
    {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (!EnsureBackBuffer(dc, width, height))
        {
            return;
        }
        clientWidth_ = width;
        clientHeight_ = height;

        hotspots_.clear();
        hotspotClip_ = {};
        Fill(backDc_, client, theme_.Tokens().bg);
        PaintNav(backDc_, client);

        const int margin = theme_.Px(kPageMargin96);
        RECT page{client.left + theme_.SidebarWidth(), client.top, client.right, client.bottom};
        page.left += margin;
        page.right -= margin;
        page.top += margin;
        if (page.right < page.left + theme_.Px(160))
        {
            page.right = page.left + theme_.Px(160);
        }

        RECT header{page.left, page.top, page.right, page.top + theme_.Px(kHeaderHeight96)};
        viewport_ = {page.left, header.bottom, page.right, client.bottom - margin};
        if (viewport_.bottom < viewport_.top)
        {
            viewport_.bottom = viewport_.top;
        }

        const int viewHeight = viewport_.bottom - viewport_.top;
        const int maxScroll = std::max(0, pageContentHeight_ - viewHeight);
        scrollOffset_ = std::clamp(scrollOffset_, 0, maxScroll);
        contentTop_ = viewport_.top - scrollOffset_;
        pageContentHeight_ = 0;

        PaintPageHeader(backDc_, header, page_);

        HRGN clip = CreateRectRgn(viewport_.left, viewport_.top, viewport_.right, viewport_.bottom);
        SelectClipRgn(backDc_, clip);
        hotspotClip_ = viewport_;
        PaintPage(backDc_, page_, RECT{page.left, contentTop_, page.right, client.bottom});
        hotspotClip_ = {};
        SelectClipRgn(backDc_, nullptr);
        DeleteObject(clip);

        PaintScrollbar(backDc_, viewport_);

        BitBlt(dc, 0, 0, width, height, backDc_, 0, 0, SRCCOPY);
    }

    void MainWindow::PaintPageHeader(HDC dc, const RECT &rect, int pageIndex)
    {
        if (pageIndex < 0 || pageIndex >= static_cast<int>(sizeof(kPages) / sizeof(kPages[0])))
        {
            return;
        }
        const auto &t = theme_.Tokens();
        Draw::Text(dc, kPages[pageIndex].title, {rect.left, rect.top, rect.right, rect.top + theme_.Px(32)},
                   theme_.TitleFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
        Draw::Text(dc, kPages[pageIndex].description,
                   {rect.left, rect.top + theme_.Px(34), rect.right, rect.top + theme_.Px(54)},
                   theme_.SmallFont(), t.textDim, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }

    void MainWindow::PaintPage(HDC dc, int pageIndex, const RECT &body)
    {
        switch (pageIndex)
        {
        case 0: PaintPageOverview(dc, body); break;
        case 1: PaintPageWorkspaces(dc, body); break;
        case 2: PaintPageMonitors(dc, body); break;
        case 3: PaintPageShortcuts(dc, body); break;
        case 4: PaintPageBehavior(dc, body); break;
        case 5: PaintPageAppRules(dc, body); break;
        case 6: PaintPageRecovery(dc, body); break;
        case 7: PaintPageAbout(dc, body); break;
        default: break;
        }

        if (renaming_ && renameIndex_ >= 0 && pageIndex == 1 && renameRect_.right > renameRect_.left)
        {
            PaintRenameBox(dc, renameRect_);
        }
    }

    void MainWindow::PaintScrollbar(HDC dc, const RECT &viewport)
    {
        const int viewHeight = viewport.bottom - viewport.top;
        if (viewHeight <= 0 || pageContentHeight_ <= viewHeight)
        {
            return;
        }
        const int maxScroll = pageContentHeight_ - viewHeight;
        const int trackTop = viewport.top + theme_.Px(2);
        const int trackBottom = viewport.bottom - theme_.Px(2);
        const int trackHeight = trackBottom - trackTop;
        if (trackHeight <= 0)
        {
            return;
        }
        const int thumbHeight = std::max(theme_.Px(28), trackHeight * viewHeight / pageContentHeight_);
        const int thumbTop = trackTop + (trackHeight - thumbHeight) * scrollOffset_ / std::max(1, maxScroll);
        RECT thumb{viewport.right + theme_.Px(6), thumbTop,
                   viewport.right + theme_.Px(10), thumbTop + thumbHeight};
        Draw::RoundRect(dc, thumb, theme_.Px(2), theme_.Tokens().border, theme_.Tokens().border, 1);
    }

    void MainWindow::SetContentEnd(int y)
    {
        pageContentHeight_ = std::max(pageContentHeight_, y - contentTop_ + theme_.Px(16));
    }

    void MainWindow::PaintNav(HDC dc, const RECT &client)
    {
        const auto &t = theme_.Tokens();
        auto px = [&](int v) { return theme_.Px(v); };

        RECT nav{client.left, client.top, client.left + theme_.SidebarWidth(), client.bottom};
        Fill(dc, nav, t.sidebar);
        Fill(dc, {nav.right, nav.top, nav.right + 1, nav.bottom}, t.border);

        // Brand: the FlowSpace mark drawn directly so it stays crisp at any DPI,
        // plus the product name with enough vertical room for its descenders.
        const int pad = px(16);
        const int tile = px(36);
        RECT tileRect{nav.left + pad, nav.top + px(16), nav.left + pad + tile, nav.top + px(16) + tile};
        Components::BrandMark(dc, tileRect, theme_);

        const int textLeft = tileRect.right + px(10);
        Draw::Text(dc, L"FlowSpace", {textLeft, nav.top + px(15), nav.right - px(12), nav.top + px(41)},
                   theme_.BrandFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Draw::Text(dc, L"Per-monitor workspaces", {textLeft, nav.top + px(40), nav.right - px(12), nav.top + px(56)},
                   theme_.SmallFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        int y = nav.top + px(84);
        const int rowH = px(34);
        const int rowGap = px(2);
        for (int i = 0; i < 8; ++i)
        {
            const bool hovered = (hoveredId_ == HS_NAV && hoveredIndex_ == i);
            const bool selected = (page_ == i);
            RECT row{nav.left + px(10), y, nav.right - px(10), y + rowH};
            if (selected || hovered)
            {
                Draw::RoundRect(dc, row, px(6), selected ? t.selected : t.hover,
                                selected ? t.selected : t.hover, 1);
            }
            if (selected)
            {
                RECT bar{row.left, row.top + px(8), row.left + px(3), row.bottom - px(8)};
                Draw::RoundRect(dc, bar, px(1), t.accent, t.accent, 1);
            }
            Draw::Icon(dc, kNavGlyphs[i], {row.left + px(12), row.top, row.left + px(30), row.bottom},
                       theme_.IconFont(), selected ? t.accent : (hovered ? t.text : t.textDim));
            Draw::Text(dc, kNavLabels[i], {row.left + px(36), row.top, row.right - px(8), row.bottom},
                       theme_.BodyFont(), selected || hovered ? t.text : t.textDim,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            AddHotspot(HS_NAV, i, row);
            y += rowH + rowGap;
        }

        // Bottom status + version.
        RECT divider{nav.left + px(16), nav.bottom - px(58), nav.right - px(16), nav.bottom - px(57)};
        Components::Divider(dc, divider, theme_);
        const bool paused = ctx_.config->Get().settings.paused;
        RECT dot{nav.left + px(16), nav.bottom - px(40), nav.left + px(22), nav.bottom - px(34)};
        Draw::RoundRect(dc, dot, px(3), paused ? t.warning : t.success, paused ? t.warning : t.success, 1);
        Draw::Text(dc, paused ? L"Paused" : L"Running",
                   {nav.left + px(30), nav.bottom - px(44), nav.right - px(16), nav.bottom - px(28)},
                   theme_.SmallFont(), paused ? t.warning : t.success, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Draw::Text(dc, L"v0.1.0", {nav.left + px(16), nav.bottom - px(26), nav.right - px(16), nav.bottom - px(10)},
                   theme_.SmallFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    void MainWindow::DrawTextAt(HDC dc, const std::wstring &text, RECT rect, HFONT font, COLORREF color, UINT format)
    {
        Draw::Text(dc, text, rect, font, color, format);
    }

    void MainWindow::Fill(HDC dc, const RECT &rect, COLORREF color)
    {
        Draw::Fill(dc, rect, color);
    }

    void MainWindow::AddHotspot(int id, int index, const RECT &rect)
    {
        RECT hit = rect;
        if (hotspotClip_.right > hotspotClip_.left && hotspotClip_.bottom > hotspotClip_.top)
        {
            RECT clipped{};
            if (!IntersectRect(&clipped, &hit, &hotspotClip_))
            {
                return;
            }
            hit = clipped;
        }
        hotspots_.push_back({id, index, hit});
    }

    // =======================================================================
    // Page: Overview (operational dashboard)
    // =======================================================================

    void MainWindow::PaintPageOverview(HDC dc, const RECT &body)
    {
        auto px = [&](int v) { return theme_.Px(v); };
        const auto &t = theme_.Tokens();
        const auto &workspaces = ctx_.engine->GetWorkspaces();
        const auto monitors = ctx_.engine->GetMonitors();

        int y = body.top;
        const int width = body.right - body.left;

        // --- Summary metrics ---
        Draw::Text(dc, L"Summary", {body.left, y, body.right, y + px(22)},
                   theme_.HeadingFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
        y += px(30);

        int activeCount = 0;
        std::string primaryActiveId;
        for (const auto &mon : monitors)
        {
            if (!mon.activeWorkspaceId.empty())
            {
                ++activeCount;
                if (primaryActiveId.empty() || mon.isPrimary)
                {
                    primaryActiveId = mon.activeWorkspaceId;
                }
            }
        }
        const core::Workspace *activeWorkspace = FindWorkspace(workspaces, primaryActiveId);
        const std::wstring activeName = activeWorkspace ? core::Utf8ToWide(activeWorkspace->name) : L"none active";

        struct Metric
        {
            const wchar_t *label;
            std::wstring value;
            std::wstring sub;
            wchar_t glyph;
            COLORREF accent;
        };
        size_t totalWorkspaces = 0;
        for (const auto &mon : monitors)
        {
            totalWorkspaces += ctx_.engine->GetWorkspacesForMonitor(mon.stableId).size();
        }
        Metric metrics[4] = {
            {L"Workspaces", std::to_wstring(totalWorkspaces), L"Total workspaces", GlyphWs,
             workspaces.empty() ? t.accent : WsAccent(workspaces.front())},
            {L"Monitors", std::to_wstring(monitors.size()), L"Connected displays", GlyphMonitor, t.accent},
            {L"Active", std::to_wstring(activeCount), activeName, GlyphCheck,
             activeCount > 0 ? t.success : t.textDim},
            {L"Shortcuts", std::to_wstring(ctx_.config->Get().hotkeys.size()), L"Configured bindings",
             GlyphNavShortcuts, t.accent},
        };

        const int gap = px(12);
        const int columns = width >= px(660) ? 4 : 2;
        const int rows = (4 + columns - 1) / columns;
        const int cardW = (width - gap * (columns - 1)) / columns;
        const int cardH = px(100);
        for (int i = 0; i < 4; ++i)
        {
            const int col = i % columns;
            const int row = i / columns;
            RECT card{body.left + col * (cardW + gap), y + row * (cardH + gap),
                      body.left + col * (cardW + gap) + cardW, y + row * (cardH + gap) + cardH};
            Components::Card(dc, card, theme_, false, px(8));
            Draw::Icon(dc, metrics[i].glyph, {card.left + px(16), card.top + px(14), card.left + px(40), card.top + px(38)},
                       theme_.IconFont(), metrics[i].accent);
            Draw::Text(dc, metrics[i].value, {card.left + px(16), card.top + px(42), card.right - px(16), card.top + px(72)},
                       theme_.TitleFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
            Draw::Text(dc, metrics[i].sub, {card.left + px(16), card.top + px(74), card.right - px(16), card.top + px(96)},
                       theme_.SmallFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        y += rows * cardH + (rows - 1) * gap + px(kSectionGap96);

        // --- Monitor overview ---
        Draw::Text(dc, L"Monitors", {body.left, y, body.right, y + px(22)},
                   theme_.HeadingFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
        y += px(30);

        const int monRowH = px(58);
        for (size_t m = 0; m < monitors.size(); ++m)
        {
            const auto &mon = monitors[m];
            RECT row{body.left, y, body.right, y + monRowH};
            Components::Card(dc, row, theme_, false, px(8));
            Draw::Icon(dc, GlyphMonitor, {row.left + px(16), row.top + px(19), row.left + px(36), row.top + px(39)},
                       theme_.IconFont(), mon.isPrimary ? t.accent : t.textDim);
            const std::wstring name = (mon.isPrimary ? L"Primary \u00B7 " : L"") + MonitorName(mon);
            Draw::Text(dc, name, {row.left + px(46), row.top + px(9), row.right - px(190), row.top + px(30)},
                       theme_.BodyFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
            Draw::Text(dc, MonitorGeometry(mon),
                       {row.left + px(46), row.top + px(30), row.right - px(190), row.top + px(48)},
                       theme_.SmallFont(), t.textDim, DT_LEFT | DT_TOP | DT_SINGLELINE);

            const core::Workspace *monWs = FindWorkspace(workspaces, mon.activeWorkspaceId);
            const std::wstring chipLabel = monWs ? core::Utf8ToWide(monWs->name) : L"Unassigned";
            const int chipW = std::min(px(150), px(96) + px(8) * static_cast<int>(chipLabel.size()) / 2);
            RECT chip{row.right - px(16) - chipW, row.top + px(16), row.right - px(16), row.top + px(42)};
            const COLORREF chipFill = monWs ? WsAccent(*monWs) : t.inputBg;
            Draw::RoundRect(dc, chip, px(6), chipFill, monWs ? chipFill : t.border, 1);
            Draw::Text(dc, chipLabel, chip, theme_.SmallFont(), monWs ? t.textOnAccent : t.textDim,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            const auto monWorkspaces = ctx_.engine->GetWorkspacesForMonitor(mon.stableId);
            if (!monWorkspaces.empty())
            {
                int current = 0;
                for (size_t w = 0; w < monWorkspaces.size(); ++w)
                {
                    if (monWorkspaces[w].id == mon.activeWorkspaceId)
                    {
                        current = static_cast<int>(w);
                        break;
                    }
                }
                const int next = (current + 1) % static_cast<int>(monWorkspaces.size());
                AddHotspot(HS_MON_SWITCH, static_cast<int>(m) * 100 + next, chip);
            }
            y += monRowH + px(kRowGap96);
        }
        if (monitors.empty())
        {
            RECT empty{body.left, y, body.right, y + px(76)};
            Components::Card(dc, empty, theme_, false, px(8));
            Draw::Text(dc, L"No monitors detected.", {empty.left + px(16), empty.top, empty.right - px(16), empty.bottom},
                       theme_.BodyFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            y += px(76) + px(kRowGap96);
        }
        y += px(kSectionGap96 - kRowGap96);

        // --- Quick actions ---
        Draw::Text(dc, L"Quick actions", {body.left, y, body.right, y + px(22)},
                   theme_.HeadingFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
        y += px(30);

        struct QuickAction
        {
            const wchar_t *label;
            wchar_t glyph;
            int page;
        };
        const QuickAction actions[] = {
            {L"Add workspace", GlyphPlus, -1},
            {L"Configure monitors", GlyphMonitor, 2},
            {L"Edit shortcuts", GlyphNavShortcuts, 3},
            {L"Behavior settings", GlyphNavBehavior, 4},
        };
        const int actionCols = width >= px(680) ? 4 : 2;
        const int actionW = (width - gap * (actionCols - 1)) / actionCols;
        for (int i = 0; i < 4; ++i)
        {
            const int col = i % actionCols;
            const int row = i / actionCols;
            RECT btn{body.left + col * (actionW + gap), y + row * px(48),
                     body.left + col * (actionW + gap) + actionW, y + row * px(48) + px(40)};
            const bool hovered = (hoveredId_ == HS_WS_ADD && hoveredIndex_ == i);
            Components::Button(dc, btn, theme_, actions[i].label, theme_.BodyFont(), hovered, false, i == 0);
            AddHotspot(HS_WS_ADD, i, btn);
        }
        y += ((4 + actionCols - 1) / actionCols) * px(48);
        SetContentEnd(y);
    }

    // =======================================================================
    // Page: Workspaces (compact list)
    // =======================================================================

    void MainWindow::PaintPageWorkspaces(HDC dc, const RECT &body)
    {
        auto px = [&](int v) { return theme_.Px(v); };
        const auto &t = theme_.Tokens();
        const auto allWorkspaces = ctx_.engine->GetWorkspaces();
        const auto monitors = ctx_.engine->GetMonitors();

        const int rowH = px(64);
        const int gap = px(kRowGap96);
        const int menuBtn = px(32);

        int y = body.top;

        if (monitors.empty())
        {
            RECT empty{body.left, y, body.right, y + px(120)};
            Components::Card(dc, empty, theme_, false, px(8));
            Components::EmptyState(dc, {empty.left, empty.top + px(18), empty.right, empty.bottom - px(8)},
                                   theme_, GlyphMonitor, L"No monitors detected",
                                   L"Connect a display to manage its workspaces.");
            SetContentEnd(empty.bottom);
            return;
        }

        for (size_t m = 0; m < monitors.size(); ++m)
        {
            const auto &mon = monitors[m];
            const auto list = ctx_.engine->GetWorkspacesForMonitor(mon.stableId);

            // --- Monitor header ---
            const std::wstring monName = (mon.isPrimary ? L"Primary \u00B7 " : L"") + MonitorName(mon);
            Draw::Icon(dc, GlyphMonitor,
                       {body.left + px(2), y + px(2), body.left + px(24), y + px(24)},
                       theme_.IconFont(), mon.isPrimary ? t.accent : t.textDim);
            Draw::Text(dc, monName, {body.left + px(30), y, body.right - px(150), y + px(26)},
                       theme_.HeadingFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            const std::wstring countLabel = std::to_wstring(list.size()) +
                                            (list.size() == 1 ? L" workspace" : L" workspaces");
            Draw::Text(dc, countLabel, {body.right - px(150), y, body.right, y + px(26)},
                       theme_.SmallFont(), t.textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            y += px(32);

            for (size_t w = 0; w < list.size(); ++w)
            {
                const auto &ws = list[w];
                auto it = std::find_if(allWorkspaces.begin(), allWorkspaces.end(),
                                       [&](const core::Workspace &candidate) { return candidate.id == ws.id; });
                if (it == allWorkspaces.end())
                {
                    continue;
                }
                const int index = static_cast<int>(std::distance(allWorkspaces.begin(), it));
                const bool active = mon.activeWorkspaceId == ws.id;
                const bool renamingNow = renaming_ && renameIndex_ == index;
                const bool hovered = hoveredId_ == HS_WS_RENAME && hoveredIndex_ == index;

                RECT row{body.left, y, body.right, y + rowH};
                Components::Card(dc, row, theme_, false, px(8));
                if (hovered || renamingNow)
                {
                    Draw::RoundRect(dc, row, px(8), hovered ? t.selected : t.surface,
                                    hovered ? t.selected : t.surface, 1);
                }

                const COLORREF accent = WsAccent(ws);
                // One shared glyph for every workspace; the accent only highlights the active one.
                Draw::Icon(dc, GlyphNavWorkspaces,
                           {row.left + px(16), row.top + px(21), row.left + px(38), row.top + px(43)},
                           theme_.IconFont(), active ? accent : t.textDim);
                if (active)
                {
                    RECT dot{row.left + px(34), row.top + px(39), row.left + px(42), row.top + px(47)};
                    Draw::RoundRect(dc, dot, px(4), t.success, t.success, 1);
                }

                const int textLeft = row.left + px(48);
                const int textRight = row.right - px(16) - menuBtn - px(14);
                RECT nameRect{textLeft, row.top + px(10), textRight, row.top + px(32)};
                if (renamingNow)
                {
                    renameRect_ = nameRect;
                }
                else
                {
                    Draw::Text(dc, core::Utf8ToWide(ws.name), nameRect, theme_.HeadingFont(), t.text,
                               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }

                std::wstring meta = L"Workspace " + std::to_wstring(w + 1);
                if (active)
                {
                    meta += L"  \u00B7  Active";
                }
                Draw::Text(dc, meta, {textLeft, row.top + px(34), textRight, row.top + px(52)},
                           theme_.SmallFont(), active ? t.success : t.textDim,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                AddHotspot(HS_WS_RENAME, index, {textLeft, row.top + px(6), textRight, row.top + px(56)});

                RECT more{row.right - px(16) - menuBtn, row.top + (rowH - menuBtn) / 2,
                          row.right - px(16), row.top + (rowH + menuBtn) / 2};
                const bool moreHover = hoveredId_ == HS_WS_MENU && hoveredIndex_ == index;
                Draw::RoundRect(dc, more, px(5), moreHover ? t.selected : t.surface,
                                moreHover ? t.selected : t.border, 1);
                Draw::Icon(dc, GlyphMore, more, theme_.IconFont(),
                           moreHover ? t.text : t.textDim);
                AddHotspot(HS_WS_MENU, index, more);

                y += rowH + gap;
            }

            // Per-monitor add button: the hotspot index encodes the monitor.
            RECT add{body.left, y + px(2), body.left + px(188), y + px(2) + px(36)};
            const int addIndex = 100 + static_cast<int>(m);
            const bool addHovered = hoveredId_ == HS_WS_ADD && hoveredIndex_ == addIndex;
            Components::Button(dc, add, theme_, L"+  Add workspace", theme_.BodyFont(), addHovered, false, true);
            AddHotspot(HS_WS_ADD, addIndex, add);

            y = add.bottom + px(kSectionGap96);
        }

        SetContentEnd(y);
    }

    // =======================================================================
    // Page: Monitors (visual arrangement)
    // =======================================================================

    void MainWindow::PaintPageMonitors(HDC dc, const RECT &body)
    {
        auto px = [&](int v) { return theme_.Px(v); };
        const auto &t = theme_.Tokens();
        const auto monitors = ctx_.engine->GetMonitors();

        int y = body.top;

        Draw::Text(dc, L"Arrangement", {body.left, y, body.right, y + px(22)},
                   theme_.HeadingFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
        y += px(30);

        const int panelH = px(240);
        RECT panel{body.left, y, body.right, y + panelH};
        Components::Card(dc, panel, theme_, false, px(8));

        if (monitors.empty())
        {
            Components::EmptyState(dc, {panel.left, panel.top + px(24), panel.right, panel.bottom - px(12)},
                                   theme_, GlyphMonitor, L"No monitors detected",
                                   L"Connect a display to configure its workspace.");
        }
        else
        {
            double minL = 0, minT = 0, maxR = 0, maxB = 0;
            bool first = true;
            for (const auto &mon : monitors)
            {
                const double l = mon.bounds.left, tp = mon.bounds.top;
                const double r = mon.bounds.right, b = mon.bounds.bottom;
                if (first)
                {
                    minL = l; minT = tp; maxR = r; maxB = b; first = false;
                }
                else
                {
                    minL = std::min(minL, l); minT = std::min(minT, tp);
                    maxR = std::max(maxR, r); maxB = std::max(maxB, b);
                }
            }
            const double virtW = std::max(1.0, maxR - minL);
            const double virtH = std::max(1.0, maxB - minT);
            const int pad = px(22);
            const double availW = std::max(1, static_cast<int>(panel.right - panel.left) - pad * 2);
            const double availH = std::max(1, panelH - pad * 2);
            double scale = std::min(availW / virtW, availH / virtH);
            if (!std::isfinite(scale) || scale <= 0.0)
            {
                scale = 1.0;
            }
            const double drawW = virtW * scale;
            const double drawH = virtH * scale;
            const double offX = panel.left + ((panel.right - panel.left) - drawW) / 2.0;
            const double offY = panel.top + (panelH - drawH) / 2.0;

            for (const auto &mon : monitors)
            {
                RECT mr{static_cast<LONG>(std::lround(offX + (mon.bounds.left - minL) * scale)),
                        static_cast<LONG>(std::lround(offY + (mon.bounds.top - minT) * scale)),
                        0, 0};
                mr.right = static_cast<LONG>(std::lround(offX + (mon.bounds.right - minL) * scale));
                mr.bottom = static_cast<LONG>(std::lround(offY + (mon.bounds.bottom - minT) * scale));
                if (mr.right - mr.left < px(24)) mr.right = mr.left + px(24);
                if (mr.bottom - mr.top < px(24)) mr.bottom = mr.top + px(24);

                if (mon.isPrimary)
                {
                    Draw::RoundRect(dc, mr, px(5), t.elevated, t.accent, px(2));
                }
                else
                {
                    Draw::RoundRect(dc, mr, px(5), t.elevated, t.border, 1);
                }

                const int innerW = mr.right - mr.left;
                const int innerH = mr.bottom - mr.top;
                if (innerW >= px(60) && innerH >= px(40))
                {
                    const std::wstring name = MonitorName(mon);
                    Draw::Text(dc, name, {mr.left + px(6), mr.top + px(6), mr.right - px(6), mr.top + px(24)},
                               theme_.SmallFont(), t.text, DT_CENTER | DT_TOP | DT_SINGLELINE);
                    const std::wstring geo = std::to_wstring(mon.bounds.width()) + L" \u00D7 " +
                                             std::to_wstring(mon.bounds.height());
                    Draw::Text(dc, geo, {mr.left + px(6), mr.top + px(26), mr.right - px(6), mr.top + px(44)},
                               theme_.SmallFont(), t.textDim, DT_CENTER | DT_TOP | DT_SINGLELINE);
                    if (mon.isPrimary && innerW >= px(130))
                    {
                        RECT badge{mr.left + px(8), mr.bottom - px(24), mr.left + px(72), mr.bottom - px(8)};
                        Components::Badge(dc, badge, theme_, L"PRIMARY", theme_.SmallFont(), t.accent, t.textOnAccent);
                    }
                }
                else if (innerW >= px(30) && innerH >= px(20))
                {
                    const std::wstring num = std::to_wstring(static_cast<int>(&mon - monitors.data()) + 1);
                    Draw::Text(dc, num, mr, theme_.SmallFont(), t.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
        }
        y += panelH + px(kSectionGap96);

        // --- Monitor list with workspace assignment ---
        Draw::Text(dc, L"Monitors", {body.left, y, body.right, y + px(22)},
                   theme_.HeadingFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
        y += px(30);

        const int rowH = px(64);
        int index = 0;
        for (const auto &mon : monitors)
        {
            RECT row{body.left, y, body.right, y + rowH};
            Components::Card(dc, row, theme_, false, px(8));
            Draw::Icon(dc, GlyphMonitor, {row.left + px(16), row.top + px(22), row.left + px(38), row.top + px(44)},
                       theme_.IconFont(), mon.isPrimary ? t.accent : t.textDim);

            const std::wstring name = (mon.isPrimary ? L"Primary \u00B7 " : L"") + MonitorName(mon);
            Draw::Text(dc, name, {row.left + px(48), row.top + px(10), row.right - px(320), row.top + px(32)},
                       theme_.BodyFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            Draw::Text(dc, MonitorGeometry(mon),
                       {row.left + px(48), row.top + px(32), row.right - px(320), row.top + px(52)},
                       theme_.SmallFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Workspace chips (switch this monitor; only its own list is shown).
            const auto monWorkspaces = ctx_.engine->GetWorkspacesForMonitor(mon.stableId);
            const int chipW = px(26);
            const int chipGap = px(4);
            int chipsRight = row.right - px(30);
            const int chipsLeft = std::max(static_cast<int>(row.left) + px(250), chipsRight - static_cast<int>(monWorkspaces.size()) * (chipW + chipGap));
            int cx = chipsRight - chipW;
            for (int w = static_cast<int>(monWorkspaces.size()) - 1; w >= 0; --w)
            {
                if (cx < chipsLeft)
                {
                    break;
                }
                RECT chip{cx, row.top + px(18), cx + chipW, row.top + px(46)};
                const bool active = monWorkspaces[static_cast<size_t>(w)].id == mon.activeWorkspaceId;
                const COLORREF fill = active ? WsAccent(monWorkspaces[static_cast<size_t>(w)]) : t.inputBg;
                Draw::RoundRect(dc, chip, px(5), fill, active ? fill : t.border, 1);
                Draw::Text(dc, std::to_wstring(w + 1), chip, theme_.SmallFont(),
                           active ? t.textOnAccent : t.textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                AddHotspot(HS_MON_SWITCH, index * 100 + w, chip);
                cx -= chipW + chipGap;
            }

            y += rowH + px(kRowGap96);
            ++index;
        }

        SetContentEnd(y);
    }

    // =======================================================================
    // Page: Shortcuts
    // =======================================================================

    void MainWindow::PaintPageShortcuts(HDC dc, const RECT &body)
    {
        auto px = [&](int v) { return theme_.Px(v); };
        const auto &t = theme_.Tokens();
        auto &hotkeys = ctx_.config->Get().hotkeys;

        int y = body.top;
        const int rowH = px(52);
        const int gap = px(kRowGap96);
        const int toggleW = px(44);
        const int toggleH = px(24);
        int index = 0;

        for (auto &binding : hotkeys)
        {
            RECT row{body.left, y, body.right, y + rowH};
            const bool hovered = (hoveredId_ == HS_SHORT_CAPTURE && hoveredIndex_ == index);
            Components::Card(dc, row, theme_, false, px(8));
            if (hovered)
            {
                Draw::RoundRect(dc, row, px(8), t.selected, t.selected, 1);
            }

            const int toggleLeft = row.right - px(16) - toggleW;
            RECT toggle{toggleLeft, row.top + (rowH - toggleH) / 2, toggleLeft + toggleW,
                        row.top + (rowH + toggleH) / 2};
            const int keyRight = toggle.left - px(16);
            const int keyW = std::min(px(190), static_cast<int>(row.right - row.left) / 2);
            RECT keyBox{keyRight - keyW, row.top + px(8), keyRight, row.bottom - px(8)};

            Draw::Text(dc, ActionName(binding.action),
                       {row.left + px(16), row.top, keyBox.left - px(16), row.bottom},
                       theme_.BodyFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            const bool capturingThis = capturing_ && captureIndex_ == index;
            const std::wstring keys = capturingThis ? L"Press a key\u2026" : platform::HotkeyManager::Describe(binding);
            Draw::RoundRect(dc, keyBox, px(5), capturingThis ? t.accent : t.inputBg,
                            capturingThis ? t.accent : t.border, 1);
            Draw::Text(dc, keys, keyBox, theme_.SmallFont(),
                       capturingThis ? t.textOnAccent : t.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            AddHotspot(HS_SHORT_CAPTURE, index, keyBox);

            Components::Toggle(dc, toggle, theme_, binding.enabled,
                               hoveredId_ == HS_SHORT_TOGGLE && hoveredIndex_ == index);
            AddHotspot(HS_SHORT_TOGGLE, index, toggle);

            y += rowH + gap;
            ++index;
        }

        SetContentEnd(y);
    }

    // =======================================================================
    // Page: Behavior
    // =======================================================================

    void MainWindow::PaintPageBehavior(HDC dc, const RECT &body)
    {
        auto px = [&](int v) { return theme_.Px(v); };
        const auto &t = theme_.Tokens();
        const auto &settings = ctx_.config->Get().settings;

        int y = body.top;
        const int rowH = px(56);
        const int gap = px(6);

        auto section = [&](const wchar_t *label)
        {
            if (y > body.top)
            {
                y += px(10);
            }
            Draw::Text(dc, label, {body.left, y, body.right, y + px(22)},
                       theme_.HeadingFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
            y += px(28);
        };

        auto settingRow = [&](const wchar_t *label, const wchar_t *hint, bool on, int index)
        {
            RECT row{body.left, y, body.right, y + rowH};
            Components::Card(dc, row, theme_, false, px(8));
            Draw::Text(dc, label, {row.left + px(16), row.top + px(9), row.right - px(90), row.top + px(29)},
                       theme_.BodyFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            Draw::Text(dc, hint, {row.left + px(16), row.top + px(29), row.right - px(90), row.top + px(48)},
                       theme_.SmallFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            const int toggleW = px(44);
            const int toggleH = px(24);
            RECT toggle{row.right - px(16) - toggleW, row.top + (rowH - toggleH) / 2,
                        row.right - px(16), row.top + (rowH + toggleH) / 2};
            Components::Toggle(dc, toggle, theme_, on,
                               hoveredId_ == HS_BEHAVIOR_TOGGLE && hoveredIndex_ == index);
            AddHotspot(HS_BEHAVIOR_TOGGLE, index, toggle);
            y += rowH + gap;
        };

        auto cycleRow = [&](const wchar_t *label, const wchar_t *hint, const std::wstring &value, int index)
        {
            RECT row{body.left, y, body.right, y + rowH};
            Components::Card(dc, row, theme_, false, px(8));
            Draw::Text(dc, label, {row.left + px(16), row.top + px(9), row.right - px(200), row.top + px(29)},
                       theme_.BodyFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            Draw::Text(dc, hint, {row.left + px(16), row.top + px(29), row.right - px(200), row.top + px(48)},
                       theme_.SmallFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT box{row.right - px(16) - px(150), row.top + px(15), row.right - px(16), row.top + px(41)};
            const bool hov = hoveredId_ == HS_BEHAVIOR_TOGGLE && hoveredIndex_ == index;
            Draw::RoundRect(dc, box, px(5), hov ? t.selected : t.inputBg, hov ? t.selected : t.border, 1);
            Draw::Text(dc, value, box, theme_.SmallFont(), t.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            AddHotspot(HS_BEHAVIOR_TOGGLE, index, box);
            y += rowH + gap;
        };

        section(L"Workspace switching");
        settingRow(L"Hotkeys follow the mouse monitor", L"Monitor-relative shortcuts act on the display under the cursor.",
                   settings.switchByMouseMonitor, 2);
        settingRow(L"Pause workspace management", L"Keep hotkeys alive but stop hiding and showing windows.",
                   settings.paused, 5);

        section(L"Window management");
        settingRow(L"Hide windows (SW_HIDE)", L"Hidden windows lose their taskbar button. Turn off to minimize instead.",
                   settings.hideMode == core::HideMode::Hide, 0);
        settingRow(L"Group new windows by application", L"A new window inherits the workspace of its app's other windows.",
                   settings.groupWindowsByApplication, 6);

        section(L"Startup");
        settingRow(L"Start with Windows", L"Register FlowSpace in the HKCU Run key.",
                   settings.startWithWindows, 1);

        section(L"Notifications");
        settingRow(L"Show switch notification", L"Display a compact notification after switching.",
                   settings.showOverlay, 7);
        cycleRow(L"Notification animation", L"Cycle: none / slide / fade / scale.",
                 core::Utf8ToWide(settings.overlayAnimation), 8);
        {
            RECT row{body.left, y, body.right, y + rowH};
            Components::Card(dc, row, theme_, false, px(8));
            Draw::Text(dc, L"Notification fade duration", {row.left + px(16), row.top + px(9), row.right - px(240), row.top + px(29)},
                       theme_.BodyFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            Draw::Text(dc, L"Fade duration of the switch notification (0-2000 ms).",
                       {row.left + px(16), row.top + px(29), row.right - px(240), row.top + px(48)},
                       theme_.SmallFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT minus{row.right - px(16) - px(150), row.top + px(15), row.right - px(16) - px(110), row.top + px(41)};
            RECT plus{row.right - px(16) - px(40), row.top + px(15), row.right - px(16), row.top + px(41)};
            RECT value{minus.right + px(4), minus.top, plus.left - px(4), plus.bottom};
            const bool minusHover = hoveredId_ == HS_BEHAVIOR_STEP && hoveredIndex_ == 0;
            const bool plusHover = hoveredId_ == HS_BEHAVIOR_STEP && hoveredIndex_ == 1;
            Draw::RoundRect(dc, minus, px(5), minusHover ? t.selected : t.inputBg, t.border, 1);
            Draw::RoundRect(dc, plus, px(5), plusHover ? t.selected : t.inputBg, t.border, 1);
            Draw::Text(dc, L"\u2212", minus, theme_.BodyFont(), t.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            Draw::Text(dc, L"+", plus, theme_.BodyFont(), t.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            Draw::Text(dc, std::to_wstring(settings.overlayAnimationMs) + L" ms", value,
                       theme_.SmallFont(), t.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            AddHotspot(HS_BEHAVIOR_STEP, 0, minus);
            AddHotspot(HS_BEHAVIOR_STEP, 1, plus);
            y += rowH + gap;
        }

        SetContentEnd(y);
    }

    // =======================================================================
    // Page: App Rules
    // =======================================================================

    void MainWindow::PaintPageAppRules(HDC dc, const RECT &body)
    {
        auto px = [&](int v) { return theme_.Px(v); };
        const auto &t = theme_.Tokens();
        auto &rules = ctx_.config->Get().appRules;
        const auto &workspaces = ctx_.engine->GetWorkspaces();

        int y = body.top;

        RECT add{body.right - px(170), y, body.right, y + px(36)};
        const bool addHovered = hoveredId_ == HS_RULE_ADD && hoveredIndex_ == 0;
        Components::Button(dc, add, theme_, L"+  Add rule", theme_.BodyFont(), addHovered, false, true);
        AddHotspot(HS_RULE_ADD, 0, add);
        y += px(48);

        if (rules.empty())
        {
            RECT empty{body.left, y, body.right, y + px(130)};
            Components::Card(dc, empty, theme_, false, px(8));
            Components::EmptyState(dc, {empty.left, empty.top + px(20), empty.right, empty.bottom - px(8)},
                                   theme_, GlyphApp, L"No app rules yet",
                                   L"Add a rule to assign a matching application to a workspace automatically.");
            y += px(130);
            SetContentEnd(y);
            return;
        }

        const int rowH = px(58);
        const int gap = px(kRowGap96);
        int index = 0;
        for (const auto &rule : rules)
        {
            RECT row{body.left, y, body.right, y + rowH};
            Components::Card(dc, row, theme_, false, px(8));
            Draw::Icon(dc, GlyphApp, {row.left + px(16), row.top + px(19), row.left + px(38), row.top + px(41)},
                       theme_.IconFont(), t.textDim);

            const std::wstring exe = rule.exeName.empty() ? L"(no executable)" : rule.exeName;
            const core::Workspace *target = FindWorkspace(workspaces, rule.targetWorkspaceId);
            const std::wstring targetName = target ? core::Utf8ToWide(target->name) : L"(deleted workspace)";
            Draw::Text(dc, exe, {row.left + px(48), row.top + px(9), row.right - px(160), row.top + px(31)},
                       theme_.BodyFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            Draw::Text(dc, L"Target: " + targetName + (rule.enabled ? L"" : L"  \u00B7  Disabled"),
                       {row.left + px(48), row.top + px(31), row.right - px(160), row.top + px(51)},
                       theme_.SmallFont(), rule.enabled ? t.textDim : t.warning, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            const int toggleW = px(44);
            const int toggleH = px(24);
            const int toggleLeft = row.right - px(16) - px(30) - px(16) - toggleW;
            RECT toggle{toggleLeft, row.top + (rowH - toggleH) / 2, toggleLeft + toggleW,
                        row.top + (rowH + toggleH) / 2};
            Components::Toggle(dc, toggle, theme_, rule.enabled,
                               hoveredId_ == HS_RULE_TOGGLE && hoveredIndex_ == index);
            AddHotspot(HS_RULE_TOGGLE, index, toggle);

            RECT del{row.right - px(16) - px(30), row.top + (rowH - px(30)) / 2,
                     row.right - px(16), row.top + (rowH + px(30)) / 2};
            const bool delHover = hoveredId_ == HS_RULE_DELETE && hoveredIndex_ == index;
            Draw::RoundRect(dc, del, px(5), delHover ? t.error : t.surface, delHover ? t.error : t.border, 1);
            Draw::Icon(dc, GlyphDelete, del, theme_.IconFont(), delHover ? t.textOnAccent : t.error);
            AddHotspot(HS_RULE_DELETE, index, del);

            y += rowH + gap;
            ++index;
        }
        SetContentEnd(y);
    }

    // =======================================================================
    // Page: Recovery
    // =======================================================================

    void MainWindow::PaintPageRecovery(HDC dc, const RECT &body)
    {
        auto px = [&](int v) { return theme_.Px(v); };
        const auto &t = theme_.Tokens();
        const auto entries = ctx_.recovery->Entries();

        int y = body.top;

        // Status card.
        RECT status{body.left, y, body.right, y + px(76)};
        Components::Card(dc, status, theme_, false, px(8));
        Draw::Icon(dc, entries.empty() ? GlyphCheck : GlyphWarning,
                   {status.left + px(18), status.top + px(20), status.left + px(44), status.top + px(46)},
                   theme_.IconFont(), entries.empty() ? t.success : t.warning);
        Draw::Text(dc, entries.empty() ? L"Recovery is clean" : L"Hidden windows are pending restoration",
                   {status.left + px(56), status.top + px(14), status.right - px(140), status.top + px(38)},
                   theme_.BodyFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Draw::Text(dc, entries.empty() ? L"No hidden windows are currently journaled."
                                       : L"Use Reveal all windows to restore them before they are lost.",
                   {status.left + px(56), status.top + px(38), status.right - px(140), status.top + px(60)},
                   theme_.SmallFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const std::wstring countLabel = std::to_wstring(entries.size()) + (entries.empty() ? L" hidden" : L" pending");
        RECT badge{status.right - px(128), status.top + px(24), status.right - px(16), status.top + px(52)};
        Components::Badge(dc, badge, theme_, countLabel, theme_.SmallFont(),
                          entries.empty() ? t.selected : t.warning,
                          entries.empty() ? t.success : t.textOnAccent);
        y += px(76) + px(16);

        RECT revealAll{body.left, y, body.left + px(190), y + px(36)};
        const bool revealHover = hoveredId_ == HS_RECOVERY_REVEAL && hoveredIndex_ < 0;
        Components::Button(dc, revealAll, theme_, L"Reveal all windows", theme_.BodyFont(), revealHover,
                           false, !entries.empty());
        AddHotspot(HS_RECOVERY_REVEAL, -1, revealAll);
        y += px(36) + px(kSectionGap96);

        if (entries.empty())
        {
            RECT empty{body.left, y, body.right, y + px(132)};
            Components::Card(dc, empty, theme_, false, px(8));
            Components::EmptyState(dc, {empty.left, empty.top + px(18), empty.right, empty.bottom - px(8)},
                                   theme_, GlyphReveal, L"No hidden windows",
                                   L"Nothing is currently hidden by FlowSpace. The recovery journal is empty.");
            y += px(132) + px(kSectionGap96);
        }
        else
        {
            Draw::Text(dc, L"Hidden windows", {body.left, y, body.right, y + px(22)},
                       theme_.HeadingFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
            y += px(28);
            int index = 0;
            for (const auto &entry : entries)
            {
                RECT row{body.left, y, body.right, y + px(46)};
                Components::Card(dc, row, theme_, false, px(8));
                std::wstring title = entry.title.empty() ? L"(untitled window)" : entry.title;
                std::wstring label = title + L"  \u2014  " + entry.executableName +
                                     L"  [PID " + std::to_wstring(entry.processId) + L"]";
                Draw::Text(dc, label, {row.left + px(16), row.top, row.right - px(120), row.bottom},
                           theme_.SmallFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                RECT btn{row.right - px(96), row.top + px(8), row.right - px(12), row.bottom - px(8)};
                const bool hov = hoveredId_ == HS_RECOVERY_REVEAL && hoveredIndex_ == index;
                Components::Button(dc, btn, theme_, L"Reveal", theme_.SmallFont(), hov);
                AddHotspot(HS_RECOVERY_REVEAL, index, btn);
                y += px(46) + px(kRowGap96);
                ++index;
            }
            y += px(kSectionGap96 - kRowGap96);
        }

        // Diagnostics.
        Draw::Text(dc, L"Diagnostics", {body.left, y, body.right, y + px(22)},
                   theme_.HeadingFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
        y += px(30);

        wchar_t configPath[MAX_PATH]{};
        wchar_t journalPath[MAX_PATH]{};
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\FlowSpace\\config.json", configPath, MAX_PATH);
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\FlowSpace\\journal.json", journalPath, MAX_PATH);

        auto diagRow = [&](const wchar_t *label, const std::wstring &value)
        {
            RECT row{body.left, y, body.right, y + px(40)};
            Components::Card(dc, row, theme_, false, px(6));
            Draw::Text(dc, label, {row.left + px(12), row.top, row.left + px(84), row.bottom},
                       theme_.SmallFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            Draw::Text(dc, value, {row.left + px(88), row.top, row.right - px(12), row.bottom},
                       theme_.SmallFont(), t.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            y += px(40) + px(6);
        };
        diagRow(L"Config", configPath);
        diagRow(L"Journal", journalPath);
        diagRow(L"Log", L"%LOCALAPPDATA%\\FlowSpace\\logs\\flowspace.log");

        SetContentEnd(y);
    }

    // =======================================================================
    // Page: About
    // =======================================================================

    void MainWindow::PaintPageAbout(HDC dc, const RECT &body)
    {
        auto px = [&](int v) { return theme_.Px(v); };
        const auto &t = theme_.Tokens();

        int y = body.top;
        const int width = std::min(static_cast<int>(body.right - body.left), px(640));
        RECT card{body.left, y, body.left + width, y + px(320)};
        Components::Card(dc, card, theme_, false, px(10));

        RECT tile{card.left + px(24), card.top + px(24), card.left + px(58), card.top + px(58)};
        Components::BrandMark(dc, tile, theme_);
        Draw::Text(dc, L"FlowSpace", {tile.right + px(14), card.top + px(22), card.right - px(24), card.top + px(46)},
                   theme_.HeadingFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
        Draw::Text(dc, L"Version 0.1.0  \u00B7  Native Win32  \u00B7  x64",
                   {tile.right + px(14), card.top + px(46), card.right - px(24), card.top + px(64)},
                   theme_.SmallFont(), t.textDim, DT_LEFT | DT_TOP | DT_SINGLELINE);

        Components::Divider(dc, {card.left + px(24), card.top + px(78), card.right - px(24), card.top + px(79)}, theme_);

        auto paragraph = [&](const wchar_t *text, int top, int height)
        {
            Draw::TextWrapped(dc, text, {card.left + px(24), card.top + px(top), card.right - px(24), card.top + px(top + height)},
                              theme_.SmallFont(), t.textDim);
        };
        paragraph(L"Independent per-monitor workspaces for Windows 10 and 11. Each display keeps its own "
                  L"workspace and hidden windows.",
                  94, 40);
        paragraph(L"Windows are hidden with ShowWindowAsync(SW_HIDE) and journaled to disk before hiding, so "
                  L"the recovery journal can restore them after an abnormal shutdown.",
                  140, 44);
        paragraph(L"Built with native C++23 and Win32; no administrator rights required.",
                  190, 40);

        Components::Divider(dc, {card.left + px(24), card.bottom - px(56), card.right - px(24), card.bottom - px(55)}, theme_);
        Draw::Text(dc, L"Settings, workspace state and the recovery journal live in %LOCALAPPDATA%\\FlowSpace.",
                   {card.left + px(24), card.bottom - px(48), card.right - px(24), card.bottom - px(20)},
                   theme_.SmallFont(), t.textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SetContentEnd(card.bottom);
    }

    // =======================================================================
    // Interaction
    // =======================================================================

    void MainWindow::OnMouseMove(int x, int y)
    {
        int newHoverId = -1;
        int newHoverIndex = -1;
        for (const auto &hs : hotspots_)
        {
            if (PtInRect(&hs.rect, {x, y}))
            {
                newHoverId = hs.id;
                newHoverIndex = hs.index;
                break;
            }
        }
        if (newHoverId != hoveredId_ || newHoverIndex != hoveredIndex_)
        {
            hoveredId_ = newHoverId;
            hoveredIndex_ = newHoverIndex;

            if (!trackMouse_.cbSize)
            {
                trackMouse_.cbSize = sizeof(trackMouse_);
                trackMouse_.dwFlags = TME_LEAVE;
                trackMouse_.hwndTrack = hwnd_;
                _TrackMouseEvent(&trackMouse_);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void MainWindow::OnMouseLeave()
    {
        if (hoveredId_ != -1 || hoveredIndex_ != -1)
        {
            hoveredId_ = -1;
            hoveredIndex_ = -1;
            trackMouse_ = TRACKMOUSEEVENT{};
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void MainWindow::OnMouseWheel(int delta)
    {
        const int viewHeight = viewport_.bottom - viewport_.top;
        const int maxScroll = std::max(0, pageContentHeight_ - viewHeight);
        if (maxScroll <= 0)
        {
            return;
        }
        const int step = theme_.Px(48);
        const int next = std::clamp(scrollOffset_ - (delta / WHEEL_DELTA) * step, 0, maxScroll);
        if (next != scrollOffset_)
        {
            scrollOffset_ = next;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    bool MainWindow::OnClick(int x, int y)
    {
        const POINT pt{x, y};

        if (renaming_)
        {
            CommitRename(true);
            return true;
        }
        if (capturing_)
        {
            capturing_ = false;
            return true;
        }

        for (auto it = hotspots_.rbegin(); it != hotspots_.rend(); ++it)
        {
            if (!PtInRect(&it->rect, pt))
            {
                continue;
            }
            const int id = it->id;
            const int index = it->index;

            switch (id)
            {
            case HS_NAV:
                if (page_ != index)
                {
                    page_ = index;
                    scrollOffset_ = 0;
                    pageContentHeight_ = 0;
                    hoveredId_ = -1;
                    hoveredIndex_ = -1;
                }
                return true;

            case HS_WS_ADD:
            {
                const auto monitors = ctx_.engine->GetMonitors();
                if (index >= 100)
                {
                    const size_t monitorIndex = static_cast<size_t>(index - 100);
                    if (monitorIndex < monitors.size())
                    {
                        ctx_.engine->CreateWorkspace("Workspace", monitors[monitorIndex].stableId);
                        SaveConfig();
                    }
                    return true;
                }
                if (index == 0)
                {
                    // Overview quick action / fallback: add to the primary (or first) monitor.
                    std::wstring target;
                    for (const auto &mon : monitors)
                    {
                        if (mon.isPrimary)
                        {
                            target = mon.stableId;
                            break;
                        }
                    }
                    if (target.empty() && !monitors.empty())
                    {
                        target = monitors.front().stableId;
                    }
                    if (!target.empty())
                    {
                        ctx_.engine->CreateWorkspace("Workspace", target);
                        SaveConfig();
                    }
                }
                else if (index >= 1 && index <= 3)
                {
                    page_ = index + 1;
                    scrollOffset_ = 0;
                    pageContentHeight_ = 0;
                    hoveredId_ = -1;
                    hoveredIndex_ = -1;
                }
                return true;
            }
            case HS_WS_MENU:
                ShowWorkspaceMenu(index);
                return true;
            case HS_WS_RENAME:
            case HS_WS_DUPLICATE:
            case HS_WS_RESET:
            case HS_WS_DELETE:
            case HS_WS_ACCENT:
                RunWorkspaceAction(id, index);
                return true;

            case HS_SHORT_TOGGLE:
            {
                auto &hotkeys = ctx_.config->Get().hotkeys;
                if (index < static_cast<int>(hotkeys.size()))
                {
                    hotkeys[index].enabled = !hotkeys[index].enabled;
                    SaveConfig();
                }
                return true;
            }
            case HS_SHORT_CAPTURE:
                BeginCapture(index);
                return true;

            case HS_BEHAVIOR_TOGGLE:
            {
                auto &settings = ctx_.config->Get().settings;
                switch (index)
                {
                case 0: settings.hideMode = settings.hideMode == core::HideMode::Hide ? core::HideMode::Minimize : core::HideMode::Hide; break;
                case 1: settings.startWithWindows = !settings.startWithWindows; break;
                case 2: settings.switchByMouseMonitor = !settings.switchByMouseMonitor; break;
                case 3: settings.paused = !settings.paused; break;
                case 4: settings.groupWindowsByApplication = !settings.groupWindowsByApplication; break;
                case 5: settings.showOverlay = !settings.showOverlay; break;
                case 8:
                {
                    static constexpr const char *styles[] = {"slide", "fade", "scale", "none"};
                    int pos = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        if (settings.overlayAnimation == styles[i])
                        {
                            pos = i;
                            break;
                        }
                    }
                    settings.overlayAnimation = styles[(pos + 1) % 4];
                    break;
                }
                }
                SaveConfig();
                return true;
            }
            case HS_BEHAVIOR_STEP:
            {
                auto &settings = ctx_.config->Get().settings;
                settings.overlayAnimationMs = std::clamp(settings.overlayAnimationMs + (index == 1 ? 20 : -20), 0, 2000);
                SaveConfig();
                return true;
            }

            case HS_RULE_TOGGLE:
            {
                auto &rules = ctx_.config->Get().appRules;
                if (index < static_cast<int>(rules.size()))
                {
                    rules[index].enabled = !rules[index].enabled;
                    SaveConfig();
                }
                return true;
            }
            case HS_RULE_DELETE:
            {
                auto &rules = ctx_.config->Get().appRules;
                if (index < static_cast<int>(rules.size()))
                {
                    rules.erase(rules.begin() + index);
                    SaveConfig();
                }
                return true;
            }
            case HS_RULE_ADD:
            {
                // "+ Add rule" is clicked while Settings is the foreground window, so use
                // the foreground window captured when Settings opened (the app you were
                // using before opening Settings).
                auto fg = ctx_.engine->GetLastFocusedWindowInfo();
                if (!fg.has_value())
                {
                    fg = ctx_.engine->GetForegroundWindowInfo();
                }
                auto &rules = ctx_.config->Get().appRules;
                core::AppRule rule;
                rule.id = "rule-" + std::to_string(GetTickCount64());
                rule.enabled = true;
                rule.exeName = fg->executableName;

                // Target the active workspace on the SAME monitor the app is on (not the
                // first workspace, and not the primary monitor).
                rule.monitorPreference = core::MonitorPreference::Specific;
                rule.monitorStableId = fg->monitorStableId;
                if (auto activeWs = ctx_.engine->GetActiveWorkspaceId(fg->monitorStableId); activeWs.has_value())
                {
                    rule.targetWorkspaceId = *activeWs;
                }
                else
                {
                    const auto targetWorkspaces = ctx_.engine->GetWorkspacesForMonitor(fg->monitorStableId);
                    rule.targetWorkspaceId = targetWorkspaces.empty() ? "" : targetWorkspaces.front().id;
                }
                spdlog::info("Add rule: exe='{}' monitor='{}' target='{}' (active workspace)",
                             std::string(rule.exeName.begin(), rule.exeName.end()),
                             std::string(rule.monitorStableId.begin(), rule.monitorStableId.end()),
                             rule.targetWorkspaceId);
                rules.push_back(rule);
                SaveConfig();
                return true;
            }

            case HS_RECOVERY_REVEAL:
                if (index < 0)
                {
                    ctx_.engine->RevealAllWindows();
                }
                else
                {
                    const auto entries = ctx_.recovery->Entries();
                    if (index < static_cast<int>(entries.size()))
                    {
                        ctx_.engine->RevealSingleWindow(entries[index].hwnd);
                    }
                }
                return true;

            case HS_MON_SWITCH:
            {
                const int monitorIndex = index / 100;
                const int wsIndex = index % 100;
                ctx_.engine->SwitchMonitorToWorkspaceIndex(monitorIndex, wsIndex);
                return true;
            }
            }
        }
        return false;
    }

    void MainWindow::SaveConfig()
    {
        if (ctx_.engine)
        {
            ctx_.engine->PersistWorkspacesToConfig();
        }
        ctx_.config->Save();
        if (ctx_.onConfigSaved)
        {
            ctx_.onConfigSaved();
        }
    }

    void MainWindow::RunWorkspaceAction(int id, int workspaceIndex)
    {
        if (workspaceIndex < 0 || workspaceIndex >= static_cast<int>(ctx_.engine->GetWorkspaces().size()))
        {
            return;
        }

        switch (id)
        {
        case HS_WS_RENAME:
            BeginRename(workspaceIndex);
            return;
        case HS_WS_DUPLICATE:
            ctx_.engine->DuplicateWorkspaceIndex(workspaceIndex);
            SaveConfig();
            break;
        case HS_WS_RESET:
            ctx_.engine->ResetWorkspaceIndex(workspaceIndex);
            SaveConfig();
            break;
        case HS_WS_DELETE:
            ctx_.engine->DeleteWorkspaceIndex(workspaceIndex);
            SaveConfig();
            break;
        case HS_WS_ACCENT:
        {
            auto &ws = ctx_.engine->WorkspaceAt(workspaceIndex);
            std::wstring current = core::Utf8ToWide(ws.accentColor);
            const int n = sizeof(ThemeTokens::kAccentPresets) / sizeof(ThemeTokens::kAccentPresets[0]);
            int pos = 0;
            for (int i = 0; i < n; ++i)
            {
                if (_wcsicmp(current.c_str(), ThemeTokens::kAccentPresets[i]) == 0)
                {
                    pos = i;
                    break;
                }
            }
            ws.accentColor = core::WideToUtf8(ThemeTokens::kAccentPresets[(pos + 1) % n]);
            SaveConfig();
            break;
        }
        default:
            return;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void MainWindow::ShowWorkspaceMenu(int workspaceIndex)
    {
        const auto workspaces = ctx_.engine->GetWorkspaces();
        if (workspaceIndex < 0 || workspaceIndex >= static_cast<int>(workspaces.size()))
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }
        AppendMenuW(menu, MF_STRING, HS_WS_RENAME, L"Rename");
        AppendMenuW(menu, MF_STRING, HS_WS_ACCENT, L"Change accent color");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, HS_WS_DUPLICATE, L"Duplicate workspace");
        AppendMenuW(menu, MF_STRING, HS_WS_RESET, L"Reset appearance");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, HS_WS_DELETE, L"Delete workspace");

        POINT pt{};
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd_);
        const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                           pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
        if (command != 0)
        {
            RunWorkspaceAction(command, workspaceIndex);
        }
    }

    void MainWindow::BeginRename(int workspaceIndex)
    {
        const auto &workspaces = ctx_.engine->GetWorkspaces();
        if (workspaceIndex < 0 || workspaceIndex >= static_cast<int>(workspaces.size()))
        {
            return;
        }
        renaming_ = true;
        renameIndex_ = workspaceIndex;
        renameBuffer_ = core::Utf8ToWide(workspaces[workspaceIndex].name);
        SetFocus(hwnd_);
        SetForegroundWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void MainWindow::CommitRename(bool apply)
    {
        if (!renaming_)
        {
            return;
        }
        if (apply && renameIndex_ >= 0 && !renameBuffer_.empty())
        {
            ctx_.engine->RenameWorkspaceIndex(renameIndex_, core::WideToUtf8(renameBuffer_));
            SaveConfig();
        }
        renaming_ = false;
        renameIndex_ = -1;
        renameBuffer_.clear();
        renameRect_ = {};
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    bool MainWindow::HandleRenameKey(WPARAM vk, WPARAM ch)
    {
        if (vk == VK_ESCAPE)
        {
            CommitRename(false);
            return true;
        }
        if (vk == VK_RETURN)
        {
            CommitRename(true);
            return true;
        }
        if (vk == VK_BACK)
        {
            if (!renameBuffer_.empty())
            {
                renameBuffer_.pop_back();
            }
            return true;
        }
        if (ch >= 0x20 && ch != 127)
        {
            renameBuffer_.push_back(static_cast<wchar_t>(ch));
            return true;
        }
        return false;
    }

    void MainWindow::BeginCapture(int bindingIndex)
    {
        capturing_ = true;
        captureIndex_ = bindingIndex;
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
    }

    bool MainWindow::HandleCaptureKey(WPARAM vk)
    {
        auto &hotkeys = ctx_.config->Get().hotkeys;
        if (captureIndex_ < 0 || captureIndex_ >= static_cast<int>(hotkeys.size()))
        {
            capturing_ = false;
            return false;
        }
        auto &binding = hotkeys[captureIndex_];

        if (vk == VK_ESCAPE)
        {
            capturing_ = false;
            return true;
        }

        static constexpr WPARAM modifierKeys[] = {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN};
        bool isModifier = false;
        for (WPARAM m : modifierKeys)
        {
            if (vk == m)
            {
                isModifier = true;
                break;
            }
        }
        if (isModifier)
        {
            return false;
        }

        std::wstring mods;
        if (GetKeyState(VK_CONTROL) & 0x8000) mods += mods.empty() ? L"Ctrl" : L"+Ctrl";
        if (GetKeyState(VK_MENU) & 0x8000) mods += mods.empty() ? L"Alt" : L"+Alt";
        if (GetKeyState(VK_SHIFT) & 0x8000) mods += mods.empty() ? L"Shift" : L"+Shift";
        if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods += mods.empty() ? L"Win" : L"+Win";

        std::wstring keyName = platform::HotkeyManager::KeyName(static_cast<UINT>(vk));
        if (keyName.empty())
        {
            if (vk >= '1' && vk <= '9')
            {
                keyName = std::wstring(L"D") + static_cast<wchar_t>(vk);
            }
            else
            {
                return false;
            }
        }

        binding.modifiers = mods;
        binding.key = keyName;
        capturing_ = false;
        SaveConfig();
        return true;
    }

    void MainWindow::PaintRenameBox(HDC dc, const RECT &rect)
    {
        const auto &t = theme_.Tokens();
        Draw::RoundRect(dc, rect, theme_.Px(5), t.inputBg, t.accent, 1);
        RECT textRect{rect.left + theme_.Px(4), rect.top + theme_.Px(2),
                      rect.right - theme_.Px(4), rect.bottom - theme_.Px(2)};
        Draw::Text(dc, renameBuffer_, textRect, theme_.HeadingFont(), t.text,DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        HGDIOBJ oldFont = SelectObject(dc, theme_.HeadingFont());
        SIZE size{};
        GetTextExtentPoint32W(dc, renameBuffer_.c_str(), static_cast<int>(renameBuffer_.length()), &size);
        SelectObject(dc, oldFont);
        int caretX = textRect.left + size.cx;
        if (caretX >= textRect.right - 2)
        {
            caretX = textRect.right - 2;
        }
        Draw::Line(dc, caretX, rect.top + theme_.Px(4), caretX, rect.bottom - theme_.Px(4), t.text, 1);
    }

    void MainWindow::NotifyStateChanged()
    {
        if (hwnd_)
        {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void MainWindow::ApplySystemTheme()
    {
        theme_.SetMode(ThemeMode::System);
        ApplyNativeTitleBar(hwnd_, theme_.Tokens(), theme_.EffectiveMode() == ThemeMode::Dark);
        if (hwnd_)
        {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

} // namespace flowspace::ui
