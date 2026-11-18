#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "core/StringUtils.h"

#include <spdlog/spdlog.h>
#include <windowsx.h>
#include <commdlg.h>
#include <algorithm>
#include <string>

namespace flowspace::ui
{
    namespace
    {
        constexpr wchar_t kClassName[] = L"FlowSpaceMainWindow";
        constexpr int kNavWidth = 190;
        constexpr int kPageMargin = 24;

        // Hotspot ids (index selects the row/workspace/binding).
        constexpr int HS_NAV = 100;              // index = page
        constexpr int HS_WS_CARD = 200;          // index = workspace
        constexpr int HS_WS_RENAME = 201;
        constexpr int HS_WS_DUPLICATE = 202;
        constexpr int HS_WS_DELETE = 203;
        constexpr int HS_WS_RESET = 204;
        constexpr int HS_WS_WALLPAPER = 205;
        constexpr int HS_WS_ACCENT = 206;
        constexpr int HS_WS_ICON = 207;
        constexpr int HS_WS_ADD = 208;
        constexpr int HS_SHORT_TOGGLE = 300;     // index = hotkey binding
        constexpr int HS_SHORT_CAPTURE = 301;
        constexpr int HS_BEHAVIOR_TOGGLE = 400;  // index = behavior row
        constexpr int HS_BEHAVIOR_STEP = 401;    // +/- for numeric rows
        constexpr int HS_RULE_TOGGLE = 500;      // index = rule
        constexpr int HS_RULE_DELETE = 501;
        constexpr int HS_RULE_ADD = 502;
        constexpr int HS_RECOVERY_REVEAL = 600;  // index = journal entry; -1 = reveal all
        constexpr int HS_MON_WALLPAPER = 700;    // index = monitor: reset to original wallpaper
        constexpr int HS_MON_SWITCH = 701;       // index = monitorIndex*100 + workspaceIndex

        struct BehaviorRow
        {
            const wchar_t *label;
            const wchar_t *hint;
        };
        constexpr BehaviorRow kBehaviorRows[] = {
            {L"Hide windows (SW_HIDE)", L"Hidden windows lose their taskbar button. Turn off to minimize instead."},
            {L"Start with Windows", L"Register FlowSpace in the HKCU Run key."},
            {L"Hotkeys follow the mouse monitor", L"Monitor-relative shortcuts act on the display under the cursor."},
            {L"Wallpaper management", L"Apply workspace wallpapers through IDesktopWallpaper."},
            {L"Restore original wallpaper on exit", L"Put the user's wallpapers back when FlowSpace closes."},
            {L"Pause workspace management", L"Keep hotkeys alive but stop hiding/showing windows."},
            {L"Group new windows by application", L"A new window inherits the workspace of its app's other windows."},
            {L"Show switch notification", L"Display a compact notification after switching."},
            {L"Notification animation", L"Cycle: fade / none."},
            {L"Notification fade duration", L"Fade duration of the switch notification (80-200 ms)."},
        };

        std::wstring AnimationName(const std::string &style)
        {
            return core::Utf8ToWide(style);
        }
    }

    MainWindow *MainWindow::instance_ = nullptr;

    void MainWindow::RegisterMainWindowClass(HINSTANCE instance)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &WndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(Theme::Bg);
        RegisterClassW(&wc);
    }

    void MainWindow::Open(HINSTANCE instance, Context context)
    {
        if (instance_)
        {
            ShowWindow(instance_->hwnd_, SW_SHOWNOACTIVATE);
            SetForegroundWindow(instance_->hwnd_);
            return;
        }
        instance_ = new MainWindow();
        instance_->hinstance_ = instance;
        instance_->ctx_ = std::move(context);

        instance_->hwnd_ = CreateWindowExW(0, kClassName, L"FlowSpace Settings",
                                           WS_OVERLAPPEDWINDOW,
                                           CW_USEDEFAULT, CW_USEDEFAULT, 980, 640,
                                           nullptr, nullptr, instance, instance_);
        if (instance_->hwnd_)
        {
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
        auto del = [](HFONT &f)
        {
            if (f)
            {
                DeleteObject(f);
                f = nullptr;
            }
        };
        del(fontTitle_);
        del(fontH2_);
        del(fontBody_);
        del(fontSmall_);
        del(fontIcon_);
    }

    LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_CREATE)
        {
            auto create = reinterpret_cast<CREATESTRUCTW *>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return 0;
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
        case WM_SIZE:
        {
            clientWidth_ = LOWORD(lParam);
            clientHeight_ = HIWORD(lParam);
            InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        }
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
            mmi->ptMinTrackSize = {760, 520};
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
                InvalidateRect(hwnd_, nullptr, TRUE);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (renaming_)
            {
                if (HandleRenameKey(wParam, 0))
                {
                    InvalidateRect(hwnd_, nullptr, TRUE);
                }
                return 0;
            }
            if (capturing_ && HandleCaptureKey(wParam))
            {
                InvalidateRect(hwnd_, nullptr, TRUE);
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
                InvalidateRect(hwnd_, nullptr, TRUE);
            }
            return 0;
        case WM_DPICHANGED:
        {
            auto del = [](HFONT &f)
            {
                if (f)
                {
                    DeleteObject(f);
                    f = nullptr;
                }
            };
            del(fontTitle_);
            del(fontH2_);
            del(fontBody_);
            del(fontSmall_);
            del(fontIcon_);
            InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }

    // --- drawing helpers ---------------------------------------------------------

    void MainWindow::Fill(HDC dc, const RECT &rect, COLORREF color)
    {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }

    void MainWindow::DrawTextAt(HDC dc, const std::wstring &text, RECT rect, HFONT font, COLORREF color, UINT format)
    {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        HGDIOBJ old = SelectObject(dc, font);
        DrawTextW(dc, text.c_str(), -1, &rect, format | DT_END_ELLIPSIS);
        SelectObject(dc, old);
    }

    void MainWindow::DrawToggle(HDC dc, const RECT &rect, bool on)
    {
        const COLORREF track = on ? Theme::Accent : RGB(70, 76, 86);
        const int h = rect.bottom - rect.top;
        const int r = h / 2;
        HBRUSH brush = CreateSolidBrush(track);
        HGDIOBJ old = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, rect.left, rect.top, rect.left + h, rect.bottom);
        Ellipse(dc, rect.right - h, rect.top, rect.right, rect.bottom);
        RECT middle{rect.left + r, rect.top, rect.right - r, rect.bottom};
        FillRect(dc, &middle, brush);
        SelectObject(dc, oldPen);
        SelectObject(dc, old);
        DeleteObject(brush);

        // Knob.
        const int knobR = r - 3;
        const int cx = on ? rect.right - r : rect.left + r;
        HBRUSH knob = CreateSolidBrush(RGB(240, 242, 245));
        old = SelectObject(dc, knob);
        oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, cx - knobR, rect.top + 3, cx + knobR, rect.bottom - 3);
        SelectObject(dc, oldPen);
        SelectObject(dc, old);
        DeleteObject(knob);
    }

    void MainWindow::AddHotspot(int id, int index, const RECT &rect)
    {
        hotspots_.push_back({id, index, rect});
    }

    // --- painting ----------------------------------------------------------------

    void MainWindow::Paint(HDC dc)
    {
        hotspots_.clear();
        scrollOffset_ = 0;

        // Fonts are rebuilt lazily per device context DPI.
        if (!fontBody_)
        {
            fontTitle_ = Theme::CreateUiFont(16, true);
            fontH2_ = Theme::CreateUiFont(12, true);
            fontBody_ = Theme::CreateUiFont(9);
            fontSmall_ = Theme::CreateUiFont(8);
            fontIcon_ = Theme::CreateIconFont(12);
        }

        RECT client;
        GetClientRect(hwnd_, &client);
        Fill(dc, client, Theme::Bg);

        PaintNav(dc, client);

        RECT page{client.left + kNavWidth, client.top, client.right, client.bottom};
        page.left += kPageMargin;
        page.right -= kPageMargin;
        page.top += kPageMargin;

        switch (page_)
        {
        case 0: PaintPageOverview(dc, page); break;
        case 1: PaintPageWorkspaces(dc, page); break;
        case 2: PaintPageMonitors(dc, page); break;
        case 3: PaintPageShortcuts(dc, page); break;
        case 4: PaintPageBehavior(dc, page); break;
        case 5: PaintPageAppRules(dc, page); break;
        case 6: PaintPageRecovery(dc, page); break;
        case 7: PaintPageAbout(dc, page); break;
        }

        if (renaming_)
        {
            PaintRenameBox(dc);
        }
    }

    void MainWindow::PaintNav(HDC dc, const RECT &client)
    {
        RECT nav{client.left, client.top, client.left + kNavWidth, client.bottom};
        Fill(dc, nav, Theme::Sidebar);

        static constexpr const wchar_t *pages[] = {
            L"Overview", L"Workspaces", L"Monitors", L"Shortcuts",
            L"Behavior", L"App Rules", L"Recovery", L"About",
        };

        RECT title{nav.left + 18, nav.top + 18, nav.right - 12, nav.top + 46};
        DrawTextAt(dc, L"FlowSpace", title, fontTitle_, Theme::Text, DT_LEFT | DT_SINGLELINE);
        RECT sub{nav.left + 18, nav.top + 46, nav.right - 12, nav.top + 66};
        DrawTextAt(dc, L"Workspace manager", sub, fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);

        int y = nav.top + 84;
        for (int i = 0; i < 8; ++i)
        {
            RECT row{nav.left + 10, y, nav.right - 10, y + 34};
            if (page_ == i)
            {
                Fill(dc, row, Theme::Card);
                RECT bar{row.left, row.top, row.left + 3, row.bottom};
                Fill(dc, bar, Theme::Accent);
            }
            DrawTextAt(dc, pages[i], RECT{row.left + 12, row.top, row.right, row.bottom},
                       fontBody_, page_ == i ? Theme::Text : Theme::TextDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            AddHotspot(HS_NAV, i, row);
            y += 38;
        }

        // Pause indicator at the bottom of the nav.
        RECT status{nav.left + 12, nav.bottom - 46, nav.right - 12, nav.bottom - 26};
        const bool paused = ctx_.config->Get().settings.paused;
        DrawTextAt(dc, paused ? L"● Paused" : L"● Running", status, fontSmall_,
                   paused ? RGB(229, 83, 75) : RGB(46, 160, 67), DT_LEFT | DT_SINGLELINE);
    }

    void MainWindow::PaintPageOverview(HDC dc, const RECT &page)
    {
        const auto &config = ctx_.config->Get();
        DrawTextAt(dc, L"Overview", RECT{page.left, page.top, page.right, page.top + 30},
                   fontTitle_, Theme::Text, DT_LEFT | DT_SINGLELINE);

        // Workspace strip.
        int y = page.top + 48;
        const auto &workspaces = ctx_.engine->GetWorkspaces();
        const auto monitors = ctx_.engine->GetMonitors();

        DrawTextAt(dc, L"Workspaces", RECT{page.left, y, page.right, y + 22}, fontH2_, Theme::Text, DT_LEFT | DT_SINGLELINE);
        y += 30;

        int x = page.left;
        for (const auto &ws : workspaces)
        {
            RECT card{x, y, x + 120, y + 64};
            Fill(dc, card, Theme::Card);
            const COLORREF accent = Theme::FromHex(core::Utf8ToWide(ws.accentColor));
            RECT bar{card.left, card.top, card.right, card.top + 4};
            Fill(dc, bar, accent);

            // Icon glyph.
            wchar_t glyph[2] = {Glyphs::GlyphFor(core::Utf8ToWide(ws.icon)), 0};
            DrawTextAt(dc, glyph, RECT{card.left, card.top + 8, card.right, card.top + 34},
                       fontIcon_, accent, DT_CENTER | DT_SINGLELINE);
            DrawTextAt(dc, core::Utf8ToWide(ws.name), RECT{card.left + 6, card.top + 34, card.right - 6, card.bottom - 6},
                       fontSmall_, Theme::Text, DT_CENTER | DT_SINGLELINE);
            x += 128;
        }
        RECT addCard{x, y, x + 120, y + 64};
        Fill(dc, addCard, Theme::CardHover);
        DrawTextAt(dc, L"+ Add", addCard, fontBody_, Theme::TextDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        AddHotspot(HS_WS_ADD, 0, addCard);

        y += 84;

        // Monitor status.
        DrawTextAt(dc, L"Monitors", RECT{page.left, y, page.right, y + 22}, fontH2_, Theme::Text, DT_LEFT | DT_SINGLELINE);
        y += 28;
        for (const auto &mon : monitors)
        {
            RECT card{page.left, y, page.right, y + 58};
            Fill(dc, card, Theme::Card);
            std::wstring line = mon.isPrimary ? L"Primary — " : L"";
            line += mon.friendlyName.empty() ? L"Display" : mon.friendlyName;
            line += L"   (" + std::to_wstring(mon.bounds.width()) + L"×" + std::to_wstring(mon.bounds.height()) + L")";
            DrawTextAt(dc, line, RECT{card.left + 14, card.top + 6, card.right - 14, card.top + 30},
                       fontBody_, Theme::Text, DT_LEFT | DT_SINGLELINE);

            std::string activeWsId = mon.activeWorkspaceId;
            std::wstring activeName = L"—";
            for (const auto &ws : workspaces)
            {
                if (ws.id == activeWsId)
                {
                    activeName = core::Utf8ToWide(ws.name);
                    break;
                }
            }
            DrawTextAt(dc, L"Active: " + activeName, RECT{card.left + 14, card.top + 30, card.right - 14, card.bottom - 6},
                       fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);
            y += 66;
        }

        // Journal status.
        y += 8;
        const size_t pending = ctx_.recovery->Count();
        std::wstring journalLine = pending == 0
                                       ? L"Recovery journal: no hidden windows pending."
                                       : L"Recovery journal: " + std::to_wstring(pending) + L" window(s) hidden by FlowSpace.";
        DrawTextAt(dc, journalLine, RECT{page.left, y, page.right, y + 22}, fontBody_,
                   pending ? RGB(240, 136, 62) : Theme::TextDim, DT_LEFT | DT_SINGLELINE);
    }

    void MainWindow::PaintPageWorkspaces(HDC dc, const RECT &page)
    {
        DrawTextAt(dc, L"Workspaces", RECT{page.left, page.top, page.right, page.top + 30},
                   fontTitle_, Theme::Text, DT_LEFT | DT_SINGLELINE);
        DrawTextAt(dc, L"Click a name to rename. Duplicate copies appearance; Reset clears wallpaper/appearance.",
                   RECT{page.left, page.top + 32, page.right, page.top + 50}, fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);

        int y = page.top + 60;
        int index = 0;
        for (const auto &ws : ctx_.engine->GetWorkspaces())
        {
            RECT card{page.left, y, page.right, y + 92};
            Fill(dc, card, Theme::Card);
            const COLORREF accent = Theme::FromHex(core::Utf8ToWide(ws.accentColor));
            RECT bar{card.left, card.top, card.left + 4, card.bottom};
            Fill(dc, bar, accent);

            const bool selected = renaming_ && renameIndex_ == index;

            // Name.
            DrawTextAt(dc, (selected ? renameBuffer_ : core::Utf8ToWide(ws.name)),
                       RECT{card.left + 20, card.top + 10, card.left + 260, card.top + 34},
                       fontH2_, Theme::Text, DT_LEFT | DT_SINGLELINE);
            AddHotspot(HS_WS_RENAME, index, RECT{card.left + 16, card.top + 8, card.left + 280, card.top + 36});

            // Number.
            std::wstring num = index < 9 ? std::to_wstring(index + 1) : L"·";
            DrawTextAt(dc, L"#" + num, RECT{card.left + 20, card.top + 36, card.left + 120, card.top + 56},
                       fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);

            // Wallpaper.
            std::wstring wp = ws.defaultWallpaperPath.empty()
                                  ? L"No wallpaper"
                                  : (ws.defaultWallpaperPath.size() > 48
                                         ? L"…" + ws.defaultWallpaperPath.substr(ws.defaultWallpaperPath.size() - 48)
                                         : ws.defaultWallpaperPath);
            DrawTextAt(dc, L"Wallpaper: " + wp, RECT{card.left + 20, card.top + 54, card.left + 520, card.top + 76},
                       fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);

            // Action buttons on the right.
            struct Btn
            {
                int id;
                const wchar_t *label;
            };
            const Btn buttons[] = {
                {HS_WS_WALLPAPER, L"Wallpaper…"},
                {HS_WS_DUPLICATE, L"Duplicate"},
                {HS_WS_RESET, L"Reset"},
                {HS_WS_DELETE, L"Delete"},
            };
            int bx = card.right - 12;
            for (int b = 3; b >= 0; --b)
            {
                RECT btn{bx - 86, card.top + 12, bx, card.top + 38};
                Fill(dc, btn, Theme::CardHover);
                DrawTextAt(dc, buttons[b].label, btn, fontSmall_, Theme::Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                AddHotspot(buttons[b].id, index, btn);
                bx -= 92;
            }

            // Accent + icon pickers (cycle through presets).
            RECT accentBtn{card.right - 490, card.top + 44, card.right - 490 + 150, card.top + 70};
            Fill(dc, accentBtn, Theme::CardHover);
            RECT swatch{accentBtn.left + 6, accentBtn.top + 5, accentBtn.left + 26, accentBtn.bottom - 5};
            Fill(dc, swatch, accent);
            DrawTextAt(dc, L"Accent", RECT{accentBtn.left + 32, accentBtn.top, accentBtn.right, accentBtn.bottom},
                       fontSmall_, Theme::Text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            AddHotspot(HS_WS_ACCENT, index, accentBtn);

            RECT iconBtn{accentBtn.right + 8, card.top + 44, accentBtn.right + 158, card.top + 70};
            Fill(dc, iconBtn, Theme::CardHover);
            wchar_t glyph[2] = {Glyphs::GlyphFor(core::Utf8ToWide(ws.icon)), 0};
            DrawTextAt(dc, glyph, RECT{iconBtn.left + 4, iconBtn.top, iconBtn.left + 26, iconBtn.bottom},
                       fontIcon_, accent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextAt(dc, L"Icon", RECT{iconBtn.left + 30, iconBtn.top, iconBtn.right, iconBtn.bottom},
                       fontSmall_, Theme::Text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            AddHotspot(HS_WS_ICON, index, iconBtn);

            AddHotspot(HS_WS_CARD, index, card);
            y += 100;
            ++index;
        }

        // Add workspace button.
        RECT add{page.left, y, page.left + 120, y + 36};
        Fill(dc, add, Theme::CardHover);
        DrawTextAt(dc, L"+ Add workspace", add, fontBody_, Theme::Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        AddHotspot(HS_WS_ADD, 1, add);
    }

    void MainWindow::PaintPageMonitors(HDC dc, const RECT &page)
    {
        DrawTextAt(dc, L"Monitors", RECT{page.left, page.top, page.right, page.top + 30},
                   fontTitle_, Theme::Text, DT_LEFT | DT_SINGLELINE);
        DrawTextAt(dc, L"Each monitor keeps an independent active workspace. Hotkeys act on the monitor under the cursor when enabled in Behavior.",
                   RECT{page.left, page.top + 32, page.right, page.top + 50}, fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);

        int y = page.top + 60;
        int index = 0;
        for (const auto &mon : ctx_.engine->GetMonitors())
        {
            RECT card{page.left, y, page.right, y + 96};
            Fill(dc, card, Theme::Card);

            std::wstring name = (mon.isPrimary ? L"Primary — " : L"") + (mon.friendlyName.empty() ? L"Display" : mon.friendlyName);
            DrawTextAt(dc, name, RECT{card.left + 16, card.top + 10, card.right - 16, card.top + 34},
                       fontH2_, Theme::Text, DT_LEFT | DT_SINGLELINE);

            // Geometry + stable id (truncated).
            std::wstring geo = std::to_wstring(mon.bounds.width()) + L"×" + std::to_wstring(mon.bounds.height())
                             + L"  at " + std::to_wstring(mon.bounds.left) + L"," + std::to_wstring(mon.bounds.top)
                             + L"   DPI " + std::to_wstring(mon.dpiX);
            DrawTextAt(dc, geo, RECT{card.left + 16, card.top + 36, card.right - 16, card.top + 56},
                       fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);
            std::wstring stableId = mon.stableId.size() > 64
                                        ? L"…" + mon.stableId.substr(mon.stableId.size() - 64)
                                        : mon.stableId;
            DrawTextAt(dc, stableId, RECT{card.left + 16, card.top + 56, card.right - 16, card.top + 76},
                       fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);

            // Wallpaper restore button for this monitor.
            RECT btn{card.right - 200, card.top + 14, card.right - 16, card.top + 40};
            Fill(dc, btn, Theme::CardHover);
            DrawTextAt(dc, L"Restore original", btn, fontSmall_, Theme::Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            AddHotspot(HS_MON_WALLPAPER, index, btn);

            // Active workspace switch strip.
            int x = card.left + 16;
            int wsIndex = 0;
            for (const auto &ws : ctx_.engine->GetWorkspaces())
            {
                const bool active = ws.id == mon.activeWorkspaceId;
                RECT chip{x, card.top + 60, x + 34, card.bottom - 12};
                const COLORREF accent = Theme::FromHex(core::Utf8ToWide(ws.accentColor));
                if (active)
                {
                    Fill(dc, chip, accent);
                }
                else
                {
                    Fill(dc, chip, Theme::CardHover);
                }
                std::wstring num = wsIndex < 9 ? std::to_wstring(wsIndex + 1) : L"·";
                DrawTextAt(dc, num, chip, fontBody_, active ? RGB(255, 255, 255) : Theme::TextDim,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                AddHotspot(HS_MON_SWITCH, index * 100 + wsIndex, chip);
                x += 40;
                ++wsIndex;
            }

            y += 104;
            ++index;
        }
        (void)index;
    }

    void MainWindow::PaintPageShortcuts(HDC dc, const RECT &page)
    {
        DrawTextAt(dc, L"Shortcuts", RECT{page.left, page.top, page.right, page.top + 30},
                   fontTitle_, Theme::Text, DT_LEFT | DT_SINGLELINE);
        DrawTextAt(dc, L"Click a key combination and press the new shortcut. Toggle to enable/disable a binding.",
                   RECT{page.left, page.top + 32, page.right, page.top + 50}, fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);

        int y = page.top + 60;
        int index = 0;
        for (auto &binding : ctx_.config->Get().hotkeys)
        {
            RECT row{page.left, y, page.right, y + 44};
            Fill(dc, row, index % 2 ? Theme::Bg : Theme::Card);

            // Action name.
            static constexpr const wchar_t *actionNames[] = {
                L"Previous workspace", L"Next workspace", L"Switch to workspace (number)",
                L"Move window to previous", L"Move window to next", L"Move window to workspace (number)",
                L"Open switcher", L"Reveal all windows", L"Open settings",
            };
            const int actionIdx = std::min(static_cast<int>(binding.action), 8);
            DrawTextAt(dc, actionNames[actionIdx], RECT{row.left + 12, row.top, row.left + 320, row.bottom},
                       fontBody_, Theme::Text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Key combination (click to capture).
            RECT keyBox{row.left + 330, row.top + 7, row.left + 530, row.bottom - 7};
            Fill(dc, keyBox, capturing_ && captureIndex_ == index ? Theme::Accent : Theme::Input);
            std::wstring keys = (capturing_ && captureIndex_ == index)
                                    ? L"Press a key…"
                                    : platform::HotkeyManager::Describe(binding);
            DrawTextAt(dc, keys, keyBox, fontSmall_, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            AddHotspot(HS_SHORT_CAPTURE, index, keyBox);

            // Toggle.
            RECT toggle{row.right - 70, row.top + 12, row.right - 26, row.bottom - 12};
            DrawToggle(dc, toggle, binding.enabled);
            AddHotspot(HS_SHORT_TOGGLE, index, toggle);

            y += 50;
            ++index;
        }
    }

    void MainWindow::PaintPageBehavior(HDC dc, const RECT &page)
    {
        DrawTextAt(dc, L"Behavior", RECT{page.left, page.top, page.right, page.top + 30},
                   fontTitle_, Theme::Text, DT_LEFT | DT_SINGLELINE);

        int y = page.top + 48;
        int index = 0;
        for (const auto &rowDef : kBehaviorRows)
        {
            RECT row{page.left, y, page.right, y + 52};
            Fill(dc, row, index % 2 ? Theme::Bg : Theme::Card);

            DrawTextAt(dc, rowDef.label, RECT{row.left + 12, row.top + 6, row.right - 110, row.top + 28},
                       fontBody_, Theme::Text, DT_LEFT | DT_SINGLELINE);
            DrawTextAt(dc, rowDef.hint, RECT{row.left + 12, row.top + 28, row.right - 110, row.bottom - 4},
                       fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);

            const auto &settings = ctx_.config->Get().settings;

            if (index == 8) // animation style: cycle value
            {
                RECT valueBox{row.right - 170, row.top + 12, row.right - 40, row.bottom - 12};
                Fill(dc, valueBox, Theme::Input);
                DrawTextAt(dc, AnimationName(settings.overlayAnimation), valueBox, fontSmall_, Theme::Text,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                AddHotspot(HS_BEHAVIOR_TOGGLE, index, valueBox);
            }
            else if (index == 9) // animation duration: - / + steppers
            {
                RECT minus{row.right - 170, row.top + 12, row.right - 130, row.bottom - 12};
                RECT plus{row.right - 70, row.top + 12, row.right - 30, row.bottom - 12};
                Fill(dc, minus, Theme::Input);
                Fill(dc, plus, Theme::Input);
                DrawTextAt(dc, L"−", minus, fontBody_, Theme::Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                DrawTextAt(dc, L"+", plus, fontBody_, Theme::Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                RECT value{minus.right + 2, row.top + 12, plus.left - 2, row.bottom - 12};
                DrawTextAt(dc, std::to_wstring(settings.overlayAnimationMs) + L" ms", value, fontSmall_, Theme::Text,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                AddHotspot(HS_BEHAVIOR_STEP, 0, minus);
                AddHotspot(HS_BEHAVIOR_STEP, 1, plus);
            }
            else
            {
                RECT toggle{row.right - 70, row.top + 12, row.right - 26, row.bottom - 12};
                bool on = false;
                switch (index)
                {
                case 0: on = settings.hideMode == core::HideMode::Hide; break;
                case 1: on = settings.startWithWindows; break;
                case 2: on = settings.switchByMouseMonitor; break;
                case 3: on = settings.wallpaperManagementEnabled; break;
                case 4: on = settings.restoreWallpaperOnExit; break;
                case 5: on = settings.paused; break;
                case 6: on = settings.groupWindowsByApplication; break;
                case 7: on = settings.showOverlay; break;
                }
                DrawToggle(dc, toggle, on);
                AddHotspot(HS_BEHAVIOR_TOGGLE, index, toggle);
            }

            y += 58;
            ++index;
        }
    }

    void MainWindow::PaintPageAppRules(HDC dc, const RECT &page)
    {
        DrawTextAt(dc, L"App Rules", RECT{page.left, page.top, page.right, page.top + 30},
                   fontTitle_, Theme::Text, DT_LEFT | DT_SINGLELINE);
        DrawTextAt(dc, L"New windows of a matching executable are assigned to the target workspace automatically. First enabled match wins.",
                   RECT{page.left, page.top + 32, page.right, page.top + 50}, fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);

        int y = page.top + 60;
        int index = 0;
        for (auto &rule : ctx_.config->Get().appRules)
        {
            RECT row{page.left, y, page.right, y + 48};
            Fill(dc, row, index % 2 ? Theme::Bg : Theme::Card);

            std::wstring target = L"(deleted workspace)";
            for (const auto &ws : ctx_.engine->GetWorkspaces())
            {
                if (ws.id == rule.targetWorkspaceId)
                {
                    target = core::Utf8ToWide(ws.name);
                    break;
                }
            }
            std::wstring label = rule.exeName.empty() ? L"(no executable)" : rule.exeName;
            DrawTextAt(dc, label + L"   →   " + target, RECT{row.left + 12, row.top, row.right - 160, row.bottom},
                       fontBody_, Theme::Text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT del{row.right - 100, row.top + 10, row.right - 12, row.bottom - 10};
            Fill(dc, del, Theme::CardHover);
            DrawTextAt(dc, L"Delete", del, fontSmall_, Theme::Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            AddHotspot(HS_RULE_DELETE, index, del);

            RECT toggle{row.right - 160, row.top + 12, row.right - 116, row.bottom - 12};
            DrawToggle(dc, toggle, rule.enabled);
            AddHotspot(HS_RULE_TOGGLE, index, toggle);

            y += 54;
            ++index;
        }

        RECT add{page.left, y, page.left + 140, y + 36};
        Fill(dc, add, Theme::CardHover);
        DrawTextAt(dc, L"+ Add rule (foreground app)", add, fontSmall_, Theme::Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        AddHotspot(HS_RULE_ADD, 0, add);
    }

    void MainWindow::PaintPageRecovery(HDC dc, const RECT &page)
    {
        DrawTextAt(dc, L"Recovery & Diagnostics", RECT{page.left, page.top, page.right, page.top + 30},
                   fontTitle_, Theme::Text, DT_LEFT | DT_SINGLELINE);

        const auto entries = ctx_.recovery->Entries();
        int y = page.top + 48;

        RECT revealAll{page.left, y, page.left + 200, y + 34};
        Fill(dc, revealAll, entries.empty() ? Theme::Card : Theme::CardHover);
        DrawTextAt(dc, L"Reveal all windows", revealAll, fontBody_, Theme::Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        AddHotspot(HS_RECOVERY_REVEAL, -1, revealAll);
        y += 46;

        if (entries.empty())
        {
            DrawTextAt(dc, L"No windows are currently hidden by FlowSpace.",
                       RECT{page.left, y, page.right, y + 24}, fontBody_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);
        }
        else
        {
            DrawTextAt(dc, L"Hidden windows (click Reveal to restore one):",
                       RECT{page.left, y, page.right, y + 24}, fontBody_, Theme::Text, DT_LEFT | DT_SINGLELINE);
            y += 28;
            int index = 0;
            for (const auto &entry : entries)
            {
                RECT row{page.left, y, page.right, y + 42};
                Fill(dc, row, index % 2 ? Theme::Bg : Theme::Card);
                std::wstring title = entry.title.empty() ? L"(untitled window)" : entry.title;
                std::wstring label = title + L"  —  " + entry.executableName
                                   + L"  [PID " + std::to_wstring(entry.processId) + L"]";
                DrawTextAt(dc, label, RECT{row.left + 12, row.top, row.right - 120, row.bottom},
                           fontSmall_, Theme::Text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                RECT btn{row.right - 100, row.top + 7, row.right - 12, row.bottom - 7};
                Fill(dc, btn, Theme::CardHover);
                DrawTextAt(dc, L"Reveal", btn, fontSmall_, Theme::Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                AddHotspot(HS_RECOVERY_REVEAL, index, btn);
                y += 48;
                ++index;
            }
        }

        // Diagnostics block.
        y += 20;
        DrawTextAt(dc, L"Diagnostics", RECT{page.left, y, page.right, y + 24}, fontH2_, Theme::Text, DT_LEFT | DT_SINGLELINE);
        y += 30;
        wchar_t configPath[MAX_PATH]{};
        wchar_t journalPath[MAX_PATH]{};
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\FlowSpace\\config.json", configPath, MAX_PATH);
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\FlowSpace\\journal.json", journalPath, MAX_PATH);
        DrawTextAt(dc, std::wstring(L"Config: ") + configPath, RECT{page.left, y, page.right, y + 22},
                   fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);
        y += 22;
        DrawTextAt(dc, std::wstring(L"Journal: ") + journalPath, RECT{page.left, y, page.right, y + 22},
                   fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);
        y += 22;
        DrawTextAt(dc, std::wstring(L"Log: %LOCALAPPDATA%\\FlowSpace\\logs\\flowspace.log"),
                   RECT{page.left, y, page.right, y + 22}, fontSmall_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);
    }

    void MainWindow::PaintPageAbout(HDC dc, const RECT &page)
    {
        DrawTextAt(dc, L"About FlowSpace", RECT{page.left, page.top, page.right, page.top + 30},
                   fontTitle_, Theme::Text, DT_LEFT | DT_SINGLELINE);
        int y = page.top + 56;
        const std::wstring lines[] = {
            L"FlowSpace v0.1 — per-monitor independent workspaces for Windows.",
            L"",
            L"Every display keeps its own active workspace. Hiding windows uses",
            L"ShowWindowAsync(SW_HIDE); the recovery journal can restore them even",
            L"after an abnormal shutdown (validated by process identity).",
            L"",
            L"Wallpapers are managed per monitor through the documented",
            L"IDesktopWallpaper COM interface and restored on exit.",
            L"",
            L"Built with MinGW (C++23). No administrator rights required.",
        };
        for (const auto &line : lines)
        {
            DrawTextAt(dc, line, RECT{page.left, y, page.right, y + 22}, fontBody_, Theme::TextDim, DT_LEFT | DT_SINGLELINE);
            y += 22;
        }
    }

    void MainWindow::PaintRenameBox(HDC dc)
    {
        // The rename text is painted inline in the workspace card; draw a caret box around it.
        (void)dc;
    }

    // --- interaction -------------------------------------------------------------

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
            capturing_ = false; // click cancels capture
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
                    return true;
                }
                return false;

            case HS_WS_ADD:
            {
                ctx_.engine->CreateWorkspace("Workspace");
                SaveConfig();
                return true;
            }
            case HS_WS_RENAME:
                BeginRename(index);
                return true;
            case HS_WS_DUPLICATE:
                ctx_.engine->DuplicateWorkspaceIndex(index);
                SaveConfig();
                return true;
            case HS_WS_RESET:
                ctx_.engine->ResetWorkspaceIndex(index);
                SaveConfig();
                return true;
            case HS_WS_DELETE:
                ctx_.engine->DeleteWorkspaceIndex(index);
                SaveConfig();
                return true;
            case HS_WS_WALLPAPER:
            {
                std::wstring file = PickImageFile();
                if (!file.empty())
                {
                    ctx_.engine->SetWorkspaceWallpaperIndex(index, file);
                    SaveConfig();
                }
                return true;
            }
            case HS_WS_ACCENT:
            {
                // Cycle accent presets.
                const auto &presets = ctx_.engine->GetWorkspaces();
                if (index < static_cast<int>(presets.size()))
                {
                    auto &ws = ctx_.engine->WorkspaceAt(index);
                    std::wstring current = core::Utf8ToWide(ws.accentColor);
                    int n = sizeof(Theme::kAccentPresets) / sizeof(Theme::kAccentPresets[0]);
                    int pos = 0;
                    for (int i = 0; i < n; ++i)
                    {
                        if (_wcsicmp(current.c_str(), Theme::kAccentPresets[i]) == 0)
                        {
                            pos = i;
                            break;
                        }
                    }
                    ws.accentColor = core::WideToUtf8(Theme::kAccentPresets[(pos + 1) % n]);
                    SaveConfig();
                }
                return true;
            }
            case HS_WS_ICON:
            {
                auto &ws = ctx_.engine->WorkspaceAt(index);
                const int n = sizeof(Glyphs::kIcons) / sizeof(Glyphs::kIcons[0]);
                int pos = 0;
                std::wstring current = core::Utf8ToWide(ws.icon);
                for (int i = 0; i < n; ++i)
                {
                    if (current == Glyphs::kIcons[i].id)
                    {
                        pos = i;
                        break;
                    }
                }
                ws.icon = core::WideToUtf8(Glyphs::kIcons[(pos + 1) % n].id);
                SaveConfig();
                return true;
            }

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
                case 3: settings.wallpaperManagementEnabled = !settings.wallpaperManagementEnabled; break;
                case 4: settings.restoreWallpaperOnExit = !settings.restoreWallpaperOnExit; break;
                case 5: settings.paused = !settings.paused; break;
                case 6: settings.groupWindowsByApplication = !settings.groupWindowsByApplication; break;
                case 7: settings.showOverlay = !settings.showOverlay; break;
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
                // Rule from the app the user was in before opening Settings (the foreground
                // window right now is this Settings window), targeting the first workspace.
                auto fg = ctx_.engine->GetLastExternalForegroundWindow();
                auto &rules = ctx_.config->Get().appRules;
                core::AppRule rule;
                rule.id = "rule-" + std::to_string(GetTickCount64());
                rule.enabled = true;
                rule.exeName = fg.executableName;
                rule.targetWorkspaceId = ctx_.engine->GetWorkspaces().empty() ? "" : ctx_.engine->GetWorkspaces().front().id;
                if (!rule.exeName.empty())
                {
                    rules.push_back(rule);
                    SaveConfig();
                }
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

            case HS_MON_WALLPAPER:
                ctx_.engine->RestoreOriginalWallpaperForMonitorIndex(index);
                return true;

            case HS_MON_SWITCH:
            {
                const int monitorIndex = index / 100;
                const int wsIndex2 = index % 100;
                ctx_.engine->SwitchMonitorToWorkspaceIndex(monitorIndex, wsIndex2);
                return true;
            }
            }
        }
        return false;
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

        // Escape cancels.
        if (vk == VK_ESCAPE)
        {
            capturing_ = false;
            return true;
        }

        // Modifiers alone cannot complete a shortcut.
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
            return false; // wait for a non-modifier key
        }

        // Build the modifier list from current keyboard state.
        std::wstring mods;
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            mods += mods.empty() ? L"Ctrl" : L"+Ctrl";
        }
        if (GetKeyState(VK_MENU) & 0x8000)
        {
            mods += mods.empty() ? L"Alt" : L"+Alt";
        }
        if (GetKeyState(VK_SHIFT) & 0x8000)
        {
            mods += mods.empty() ? L"Shift" : L"+Shift";
        }
        if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        {
            mods += mods.empty() ? L"Win" : L"+Win";
        }

        // Key name: reuse the hotkey manager's name table via Describe-compatible naming.
        std::wstring keyName = platform::HotkeyManager::KeyName(static_cast<UINT>(vk));
        if (keyName.empty())
        {
            // Map digits to D1..D9 names.
            if (vk >= '1' && vk <= '9')
            {
                keyName = std::wstring(L"D") + static_cast<wchar_t>(vk);
            }
            else
            {
                return false; // unsupported key; keep waiting
            }
        }

        binding.modifiers = mods;
        binding.key = keyName;
        capturing_ = false;
        SaveConfig();
        return true;
    }

    void MainWindow::SaveConfig()
    {
        ctx_.config->Save();
        if (ctx_.onConfigSaved)
        {
            ctx_.onConfigSaved(); // re-applies hotkeys + startup entry
        }
    }

    void MainWindow::NotifyStateChanged()
    {
        if (hwnd_)
        {
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
    }

    std::wstring MainWindow::PickImageFile()
    {
        wchar_t file[MAX_PATH] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter = L"Images\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.jxr\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn))
        {
            return file;
        }
        return {};
    }
}
