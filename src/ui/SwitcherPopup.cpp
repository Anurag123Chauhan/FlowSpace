#include "ui/SwitcherPopup.h"
#include "ui/SwitcherVisual.h"
#include "ui/Theme.h"
#include "core/StringUtils.h"

#include <Windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cstdint>
#include <string>

namespace flowspace::ui
{
    namespace
    {
        constexpr wchar_t kClassName[] = L"FlowSpaceSwitcherPopup";
        constexpr UINT kDoneMessage = WM_APP + 0x5F5; // private: ends the nested popup loop
        constexpr int kRowsPerPage = 9;               // + a footer keeps the card about the same height

        enum class FooterHover
        {
            None,
            Prev,
            Next
        };

        enum class HitKind
        {
            None,
            Row,
            PrevPage,
            NextPage
        };

        struct HitTarget
        {
            HitKind kind = HitKind::None;
            int index = -1; // global item index when kind == Row
        };

        struct PopupState
        {
            std::vector<SwitcherPopup::Item> items;
            double scale = 1.0;
            int margin = 0; // room for the soft shadow
            int cardW = 0, cardH = 0;
            int rowH = 0, padding = 0;
            int width = 0, height = 0; // physical window size (card + shadow margins)
            int x = 0, y = 0;          // physical screen coords of the window

            int rowsPerPage = kRowsPerPage;
            int page = 0;
            int pageStart = 0;
            int visibleRows = 0;
            int footerH = 0;
            bool hasPages = false; // more workspaces than fit on one page

            int hovered = -1; // global item index, -1 when over no row
            FooterHover hoveredFooter = FooterHover::None;
            int result = 0;

            HDC memDc = nullptr;
            HBITMAP bmp = nullptr;
            HGDIOBJ oldBmp = nullptr;
        };

        int Scaled(int logical, double scale)
        {
            return static_cast<int>(logical * scale + 0.5);
        }

        int PageCount(const PopupState &state)
        {
            return (static_cast<int>(state.items.size()) + state.rowsPerPage - 1) / state.rowsPerPage;
        }

        int RowsOnPage(const PopupState &state, int start)
        {
            return std::min(state.rowsPerPage, static_cast<int>(state.items.size()) - start);
        }

        void SetPage(PopupState &state, int page)
        {
            state.page = std::clamp(page, 0, PageCount(state) - 1);
            state.pageStart = state.page * state.rowsPerPage;
            state.visibleRows = RowsOnPage(state, state.pageStart);
        }

        void Layout(PopupState &state, const core::MonitorInfo &monitor)
        {
            state.scale = visual::ScaleFor(monitor);
            const double s = state.scale;
            state.margin = Scaled(12, s);
            state.cardW = Scaled(280, s);
            state.rowH = Scaled(40, s);
            state.padding = Scaled(8, s);

            // Up to nine rows, then paginate with a Prev/Next footer. With nine or fewer
            // workspaces the footer is omitted, so the compact single-page UI is unchanged.
            state.hasPages = state.items.size() > static_cast<size_t>(kRowsPerPage);
            state.rowsPerPage = state.hasPages ? kRowsPerPage : static_cast<int>(state.items.size());
            state.footerH = state.hasPages ? Scaled(34, s) : 0;
            SetPage(state, 0);

            state.cardH = state.padding * 2 + state.rowH * state.rowsPerPage + state.footerH;
            state.width = state.cardW + state.margin * 2;
            state.height = state.cardH + state.margin * 2;

            const int workW = monitor.workArea.width();
            const int workH = monitor.workArea.height();
            state.x = monitor.workArea.left + std::max(0, (workW - state.cardW) / 2) - state.margin;
            state.y = monitor.workArea.top + std::max(0, (workH - state.cardH) / 2) - state.margin;
            state.x = std::max(monitor.workArea.left, state.x);
            state.y = std::max(monitor.workArea.top, state.y);
        }

        RECT CardRect(const PopupState &state)
        {
            return RECT{state.margin, state.margin,
                        state.margin + state.cardW, state.margin + state.cardH};
        }

        RECT FooterRect(const PopupState &state)
        {
            const RECT card = CardRect(state);
            return RECT{card.left, card.bottom - state.footerH, card.right, card.bottom};
        }

        void FooterButtons(const PopupState &state, RECT &prev, RECT &next)
        {
            const RECT footer = FooterRect(state);
            const int buttonW = Scaled(74, state.scale);
            const int buttonH = Scaled(24, state.scale);
            const int top = footer.top + (state.footerH - buttonH) / 2;
            const int edge = Scaled(8, state.scale);
            prev = RECT{footer.left + edge, top, footer.left + edge + buttonW, top + buttonH};
            next = RECT{footer.right - edge - buttonW, top, footer.right - edge, top + buttonH};
        }

        HitTarget HitTest(const PopupState &state, int x, int y)
        {
            HitTarget hit;
            const RECT card = CardRect(state);
            if (x < card.left || x >= card.right || y < card.top || y >= card.bottom)
            {
                return hit;
            }

            if (state.hasPages && y >= card.bottom - state.footerH)
            {
                RECT prev{}, next{};
                FooterButtons(state, prev, next);
                const POINT point{x, y};
                if (state.page > 0 && PtInRect(&prev, point))
                {
                    hit.kind = HitKind::PrevPage;
                }
                else if (state.page + 1 < PageCount(state) && PtInRect(&next, point))
                {
                    hit.kind = HitKind::NextPage;
                }
                return hit;
            }

            const int listTop = card.top + state.padding;
            if (y >= listTop && y < listTop + state.visibleRows * state.rowH)
            {
                hit.kind = HitKind::Row;
                hit.index = state.pageStart + (y - listTop) / state.rowH;
            }
            return hit;
        }

        void DestroyBitmap(PopupState &state)
        {
            if (!state.memDc)
            {
                return;
            }
            if (state.oldBmp)
            {
                SelectObject(state.memDc, state.oldBmp);
                state.oldBmp = nullptr;
            }
            if (state.bmp)
            {
                DeleteObject(state.bmp);
                state.bmp = nullptr;
            }
            DeleteDC(state.memDc);
            state.memDc = nullptr;
        }

        void DrawFooterButton(HDC dc, const RECT &rect, const wchar_t *text, bool enabled, bool hovered, double scale)
        {
            if (enabled && hovered)
            {
                HBRUSH brush = CreateSolidBrush(Theme::CardHover);
                HGDIOBJ oldBrush = SelectObject(dc, brush);
                HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
                RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, Scaled(6, scale), Scaled(6, scale));
                SelectObject(dc, oldPen);
                SelectObject(dc, oldBrush);
                DeleteObject(brush);
            }

            HFONT font = Theme::CreateUiFont(9, false, true, scale);
            HGDIOBJ oldFont = SelectObject(dc, font);
            SetTextColor(dc, enabled ? (hovered ? Theme::Text : Theme::TextDim) : visual::Tint(Theme::Sidebar, Theme::TextDim, 0.25));
            RECT textRect = rect;
            DrawTextW(dc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, oldFont);
            DeleteObject(font);
        }

        // Renders the current page into a cached 32bpp DIB. Hover/page changes re-render this
        // bitmap; UpdateLayeredWindow then presents it without any WM_PAINT flicker.
        void RenderCard(PopupState &state)
        {
            DestroyBitmap(state);

            HDC screenDc = GetDC(nullptr);
            state.memDc = CreateCompatibleDC(screenDc);
            ReleaseDC(nullptr, screenDc);

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = state.width;
            bmi.bmiHeader.biHeight = -state.height; // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void *bits = nullptr;
            state.bmp = CreateDIBSection(state.memDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!state.bmp || !bits)
            {
                DestroyBitmap(state);
                return;
            }
            state.oldBmp = SelectObject(state.memDc, state.bmp);

            auto *pixels = static_cast<uint32_t *>(bits);
            std::fill(pixels, pixels + static_cast<size_t>(state.width) * state.height, 0u);

            const RECT card = CardRect(state);
            const COLORREF panel = Theme::Sidebar;
            const uint32_t opaque = 0xFF000000u | (static_cast<uint32_t>(GetBValue(panel)) << 16) | (static_cast<uint32_t>(GetGValue(panel)) << 8) | static_cast<uint32_t>(GetRValue(panel));
            for (int y = card.top; y < card.bottom; ++y)
            {
                uint32_t *row = pixels + static_cast<size_t>(y) * state.width + card.left;
                std::fill(row, row + state.cardW, opaque);
            }

            HDC dc = state.memDc;
            const double s = state.scale;
            SetBkMode(dc, TRANSPARENT);

            for (int slot = 0; slot < state.visibleRows; ++slot)
            {
                const int index = state.pageStart + slot;
                const auto &item = state.items[static_cast<size_t>(index)];
                const int rowTop = card.top + state.padding + slot * state.rowH;
                const int rowBottom = rowTop + state.rowH;
                const bool hovered = state.hovered == index;
                const bool active = item.active;
                const COLORREF accent = Theme::FromHex(core::Utf8ToWide(item.workspace.accentColor));

                if (hovered || active)
                {
                    COLORREF bg = hovered ? Theme::CardHover : Theme::Card;
                    if (active)
                    {
                        bg = visual::Tint(bg, accent, hovered ? 0.28 : 0.16);
                    }
                    HBRUSH brush = CreateSolidBrush(bg);
                    HGDIOBJ oldBrush = SelectObject(dc, brush);
                    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
                    RoundRect(dc, card.left + Scaled(4, s), rowTop + Scaled(2, s),
                              card.right - Scaled(4, s), rowBottom - Scaled(2, s),
                              Scaled(8, s), Scaled(8, s));
                    SelectObject(dc, oldPen);
                    SelectObject(dc, oldBrush);
                    DeleteObject(brush);
                }

                // Subtle active indicator on the left edge of the row.
                if (active)
                {
                    RECT bar{card.left + Scaled(8, s), rowTop + Scaled(11, s),
                             card.left + Scaled(11, s), rowBottom - Scaled(11, s)};
                    HBRUSH barBrush = CreateSolidBrush(accent);
                    FillRect(dc, &bar, barBrush);
                    DeleteObject(barBrush);
                }

                const int iconLeft = card.left + Scaled(16, s);
                const int iconSize = Scaled(24, s);
                const int iconTop = rowTop + (state.rowH - iconSize) / 2;
                const COLORREF iconColor = (active || hovered)
                                               ? accent
                                               : visual::Tint(Theme::TextDim, accent, 0.45);
                visual::DrawMonitorIcon(dc, iconLeft, iconTop, iconSize, iconColor, s);

                const int nameLeft = iconLeft + iconSize + Scaled(10, s);
                const int nameRight = card.right - Scaled(40, s);
                HFONT nameFont = Theme::CreateUiFont(10, false, active || hovered, s);
                HGDIOBJ oldFont = SelectObject(dc, nameFont);
                SetTextColor(dc, Theme::Text);
                RECT nameRect{nameLeft, rowTop, nameRight, rowBottom};
                DrawTextW(dc, core::Utf8ToWide(item.workspace.name).c_str(), -1, &nameRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(dc, oldFont);
                DeleteObject(nameFont);

                // Show the workspace's 1-based number, including those past the first page.
                HFONT numFont = Theme::CreateUiFont(9, false, true, s);
                HGDIOBJ oldNumFont = SelectObject(dc, numFont);
                SetTextColor(dc, active ? accent : Theme::TextDim);
                RECT numRect{nameRight + Scaled(4, s), rowTop,
                             card.right - Scaled(14, s), rowBottom};
                const std::wstring number = std::to_wstring(index + 1);
                DrawTextW(dc, number.c_str(), -1, &numRect,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, oldNumFont);
                DeleteObject(numFont);
            }

            if (state.hasPages)
            {
                const RECT footer = FooterRect(state);
                HBRUSH lineBrush = CreateSolidBrush(Theme::Border);
                RECT divider{card.left + Scaled(8, s), footer.top, card.right - Scaled(8, s), footer.top + 1};
                FillRect(dc, &divider, lineBrush);
                DeleteObject(lineBrush);

                RECT prev{}, next{};
                FooterButtons(state, prev, next);
                const int pages = PageCount(state);
                DrawFooterButton(dc, prev, L"‹ Prev", state.page > 0, state.hoveredFooter == FooterHover::Prev, s);
                DrawFooterButton(dc, next, L"Next ›", state.page + 1 < pages, state.hoveredFooter == FooterHover::Next, s);

                HFONT labelFont = Theme::CreateUiFont(8, false, false, s);
                HGDIOBJ oldLabelFont = SelectObject(dc, labelFont);
                SetTextColor(dc, Theme::TextDim);
                const std::wstring label = std::to_wstring(state.page + 1) + L" / " + std::to_wstring(pages);
                RECT labelRect{prev.right, footer.top, next.left, footer.bottom};
                DrawTextW(dc, label.c_str(), -1, &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, oldLabelFont);
                DeleteObject(labelFont);
            }

            GdiFlush();
            visual::ApplyCardAlpha(pixels, state.width, state.height, card, Scaled(12, s), state.margin, 242, s, Theme::Border);
        }

        void Present(HWND hwnd, const PopupState &state)
        {
            if (!state.memDc)
            {
                return;
            }
            POINT dst{state.x, state.y};
            POINT src{0, 0};
            SIZE size{state.width, state.height};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(hwnd, nullptr, &dst, &size, state.memDc, &src, 0, &blend, ULW_ALPHA);
        }

        void Refresh(HWND hwnd, PopupState &state)
        {
            RenderCard(state);
            Present(hwnd, state);
        }

        LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            auto *state = reinterpret_cast<PopupState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg)
            {
            case WM_CREATE:
            {
                auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
                state = static_cast<PopupState *>(create->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

                // Open on the page containing the active workspace and highlight it.
                for (size_t i = 0; i < state->items.size(); ++i)
                {
                    if (state->items[i].active)
                    {
                        const int activeIndex = static_cast<int>(i);
                        SetPage(*state, activeIndex / state->rowsPerPage);
                        state->hovered = activeIndex;
                        break;
                    }
                }
                Refresh(hwnd, *state);
                return 0;
            }
            case WM_MOUSEMOVE:
            {
                const HitTarget hit = HitTest(*state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                int newHovered = -1;
                FooterHover newFooter = FooterHover::None;
                if (hit.kind == HitKind::Row)
                {
                    newHovered = hit.index;
                }
                else if (hit.kind == HitKind::PrevPage)
                {
                    newFooter = FooterHover::Prev;
                }
                else if (hit.kind == HitKind::NextPage)
                {
                    newFooter = FooterHover::Next;
                }
                if (newHovered != state->hovered || newFooter != state->hoveredFooter)
                {
                    state->hovered = newHovered;
                    state->hoveredFooter = newFooter;
                    Refresh(hwnd, *state);
                }
                return 0;
            }
            case WM_LBUTTONUP:
            {
                const HitTarget hit = HitTest(*state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (hit.kind == HitKind::Row)
                {
                    state->result = hit.index + 1;
                    PostMessageW(hwnd, kDoneMessage, static_cast<WPARAM>(state->result), 0);
                }
                else if (hit.kind == HitKind::PrevPage || hit.kind == HitKind::NextPage)
                {
                    const int delta = hit.kind == HitKind::PrevPage ? -1 : 1;
                    SetPage(*state, state->page + delta);
                    state->hovered = state->pageStart;
                    state->hoveredFooter = FooterHover::None;
                    Refresh(hwnd, *state);
                }
                return 0;
            }
            case WM_KEYDOWN:
            {
                if (wParam == VK_ESCAPE)
                {
                    PostMessageW(hwnd, kDoneMessage, 0, 0);
                    return 0;
                }
                if (wParam >= '1' && wParam <= '9')
                {
                    const int n = static_cast<int>(wParam - '0');
                    for (size_t i = 0; i < state->items.size(); ++i)
                    {
                        if (state->items[i].number == n)
                        {
                            state->result = static_cast<int>(i) + 1;
                            PostMessageW(hwnd, kDoneMessage, static_cast<WPARAM>(state->result), 0);
                            break;
                        }
                    }
                    return 0;
                }
                if (wParam == VK_UP || wParam == VK_DOWN)
                {
                    const int delta = wParam == VK_UP ? -1 : 1;
                    const int pageEnd = state->pageStart + state->visibleRows;
                    int next = state->hovered;
                    if (next < state->pageStart || next >= pageEnd)
                    {
                        next = delta > 0 ? state->pageStart : pageEnd - 1;
                    }
                    else
                    {
                        next += delta;
                    }

                    // Cross into the adjacent page at the list boundary.
                    if (next < state->pageStart && state->page > 0)
                    {
                        SetPage(*state, state->page - 1);
                        next = state->pageStart + state->visibleRows - 1;
                    }
                    else if (next >= pageEnd && state->page + 1 < PageCount(*state))
                    {
                        SetPage(*state, state->page + 1);
                        next = state->pageStart;
                    }
                    next = std::clamp(next, state->pageStart, state->pageStart + state->visibleRows - 1);
                    if (next != state->hovered)
                    {
                        state->hovered = next;
                        state->hoveredFooter = FooterHover::None;
                        Refresh(hwnd, *state);
                    }
                    return 0;
                }
                if (wParam == VK_PRIOR || wParam == VK_NEXT ||
                    wParam == VK_LEFT || wParam == VK_RIGHT)
                {
                    const int delta = (wParam == VK_PRIOR || wParam == VK_LEFT) ? -1 : 1;
                    const int target = state->page + delta;
                    if (target >= 0 && target < PageCount(*state))
                    {
                        SetPage(*state, target);
                        state->hovered = delta > 0 ? state->pageStart : state->pageStart + state->visibleRows - 1;
                        state->hoveredFooter = FooterHover::None;
                        Refresh(hwnd, *state);
                    }
                    return 0;
                }
                if (wParam == VK_RETURN && state->hovered >= 0)
                {
                    state->result = state->hovered + 1;
                    PostMessageW(hwnd, kDoneMessage, static_cast<WPARAM>(state->result), 0);
                    return 0;
                }
                return 0;
            }
            case WM_ACTIVATE:
            {
                if (LOWORD(wParam) == WA_INACTIVE)
                {
                    PostMessageW(hwnd, kDoneMessage, 0, 0); // clicking elsewhere dismisses
                }
                return 0;
            }
            case WM_DESTROY:
            {
                DestroyBitmap(*state);
                delete state;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                return 0;
            }
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    void RegisterSwitcherClass(HINSTANCE instance)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PopupWndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
    }

    int SwitcherPopup::Show(HINSTANCE instance, const core::MonitorInfo &monitor, std::vector<Item> items)
    {
        if (items.empty())
        {
            return 0;
        }
        auto *state = new PopupState();
        state->items = std::move(items);
        Layout(*state, monitor);

        // Layered + per-pixel alpha gives rounded corners, translucency and a soft shadow
        // without a custom frame. The popup stays activatable so keyboard input works.
        constexpr DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
        HWND hwnd = CreateWindowExW(exStyle, kClassName, L"FlowSpace Switcher", WS_POPUP,
                                    state->x, state->y, state->width, state->height,
                                    nullptr, nullptr, instance, state);
        if (!hwnd)
        {
            delete state;
            return 0;
        }
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);

        // Local message loop; ends on selection, Escape, or focus loss via kDoneMessage.
        MSG msg{};
        int result = 0;
        for (;;)
        {
            const BOOL status = GetMessageW(&msg, nullptr, 0, 0);
            if (status > 0)
            {
                if (msg.message == kDoneMessage)
                {
                    result = state ? state->result : 0;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                continue;
            }
            // GetMessageW consumes WM_QUIT. Repost it so the outer application
            // loop still observes the shutdown request (status == 0).
            if (status == 0)
            {
                PostQuitMessage(static_cast<int>(msg.wParam));
            }
            // status < 0 is a retrieval error; there is no quit code to forward.
            break;
        }
        DestroyWindow(hwnd);
        return result;
    }
}
