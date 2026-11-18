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

        struct PopupState
        {
            std::vector<SwitcherPopup::Item> items;
            double scale = 1.0;
            int margin = 0;   // room for the soft shadow
            int cardW = 0, cardH = 0;
            int rowH = 0, padding = 0;
            int width = 0, height = 0; // physical window size (card + shadow margins)
            int x = 0, y = 0;          // physical screen coords of the window
            int hovered = -1;
            int result = 0;

            HDC memDc = nullptr;
            HBITMAP bmp = nullptr;
            HGDIOBJ oldBmp = nullptr;
        };

        int Scaled(int logical, double scale)
        {
            return static_cast<int>(logical * scale + 0.5);
        }

        void Layout(PopupState &state, const core::MonitorInfo &monitor)
        {
            state.scale = visual::ScaleFor(monitor);
            const double s = state.scale;
            state.margin = Scaled(12, s);
            state.cardW = Scaled(280, s);
            state.rowH = Scaled(40, s);
            state.padding = Scaled(8, s);

            // Adaptive height with a sane ceiling (10 visible rows at 100% scaling).
            const int rows = std::clamp(static_cast<int>(state.items.size()), 1, 10);
            state.cardH = state.padding * 2 + state.rowH * rows;
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

        int HitTest(const PopupState &state, int x, int y)
        {
            const RECT card = CardRect(state);
            if (x < card.left || x >= card.right || y < card.top || y >= card.bottom)
            {
                return -1;
            }
            const int row = (y - card.top - state.padding) / state.rowH;
            return (row >= 0 && row < static_cast<int>(state.items.size())) ? row : -1;
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

        // Renders the whole panel into a cached 32bpp DIB. Hover changes re-render this
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
            const uint32_t opaque = 0xFF000000u |
                                    (static_cast<uint32_t>(GetBValue(panel)) << 16) |
                                    (static_cast<uint32_t>(GetGValue(panel)) << 8) |
                                    static_cast<uint32_t>(GetRValue(panel));
            for (int y = card.top; y < card.bottom; ++y)
            {
                uint32_t *row = pixels + static_cast<size_t>(y) * state.width + card.left;
                std::fill(row, row + state.cardW, opaque);
            }

            HDC dc = state.memDc;
            const double s = state.scale;
            SetBkMode(dc, TRANSPARENT);

            for (size_t i = 0; i < state.items.size(); ++i)
            {
                const auto &item = state.items[i];
                const int rowTop = card.top + state.padding + static_cast<int>(i) * state.rowH;
                const int rowBottom = rowTop + state.rowH;
                const bool hovered = state.hovered == static_cast<int>(i);
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

                if (item.number > 0)
                {
                    HFONT numFont = Theme::CreateUiFont(9, false, true, s);
                    HGDIOBJ oldNumFont = SelectObject(dc, numFont);
                    SetTextColor(dc, active ? accent : Theme::TextDim);
                    RECT numRect{nameRight + Scaled(4, s), rowTop,
                                 card.right - Scaled(14, s), rowBottom};
                    const std::wstring number = std::to_wstring(item.number);
                    DrawTextW(dc, number.c_str(), -1, &numRect,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(dc, oldNumFont);
                    DeleteObject(numFont);
                }
            }

            GdiFlush();
            visual::ApplyCardAlpha(pixels, state.width, state.height, card,
                                   Scaled(12, s), state.margin, 242, s, Theme::Border);
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
                const int visibleRows = (state->cardH - state->padding * 2) / state->rowH;
                for (size_t i = 0; i < state->items.size() && static_cast<int>(i) < visibleRows; ++i)
                {
                    if (state->items[i].active)
                    {
                        state->hovered = static_cast<int>(i);
                        break;
                    }
                }
                Refresh(hwnd, *state);
                return 0;
            }
            case WM_MOUSEMOVE:
            {
                const int hit = HitTest(*state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (hit != state->hovered)
                {
                    state->hovered = hit;
                    Refresh(hwnd, *state);
                }
                return 0;
            }
            case WM_LBUTTONUP:
            {
                const int hit = HitTest(*state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (hit >= 0)
                {
                    state->result = hit + 1;
                    PostMessageW(hwnd, kDoneMessage, static_cast<WPARAM>(state->result), 0);
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
                    const int rows = std::min(static_cast<int>(state->items.size()),
                                              (state->cardH - state->padding * 2) / state->rowH);
                    if (rows > 0)
                    {
                        const int next = std::clamp(state->hovered + delta, 0, rows - 1);
                        if (next != state->hovered)
                        {
                            state->hovered = next;
                            Refresh(hwnd, *state);
                        }
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
        MSG msg;
        int result = 0;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (msg.message == kDoneMessage)
            {
                result = state ? state->result : 0;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        DestroyWindow(hwnd);
        return result;
    }
}
