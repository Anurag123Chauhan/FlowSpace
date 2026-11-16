#include "ui/SwitcherPopup.h"
#include "ui/Theme.h"
#include "core/StringUtils.h"

#include <Windows.h>
#include <windowsx.h>
#include <algorithm>
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
            int itemHeight = 0;
            int width = 0;
            int height = 0;
            int hovered = -1;
            int result = 0;
            HFONT uiFont = nullptr;
            HFONT subFont = nullptr;
            HFONT iconFont = nullptr;
        };

        void Layout(PopupState &state)
        {
            const int itemH = 52;
            const int width = 320;
            state.itemHeight = itemH;
            state.width = width;
            state.height = static_cast<int>(state.items.size()) * itemH + 8;
        }

        int HitTest(const PopupState &state, int x, int y)
        {
            if (x < 0 || x > state.width)
            {
                return -1;
            }
            const int idx = y / state.itemHeight;
            return (idx >= 0 && idx < static_cast<int>(state.items.size())) ? idx : -1;
        }

        void Paint(HWND hwnd, PopupState &state)
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);

            // Proper background:
            HBRUSH bgBrush = CreateSolidBrush(Theme::Sidebar);
            FillRect(dc, &client, bgBrush);
            DeleteObject(bgBrush);

            // 1px outline: the borderless popup needs some edge definition.
            HBRUSH border = CreateSolidBrush(Theme::Border);
            FrameRect(dc, &client, border);
            DeleteObject(border);

            SetBkMode(dc, TRANSPARENT);

            for (size_t i = 0; i < state.items.size(); ++i)
            {
                const auto &item = state.items[i];
                RECT row{0, static_cast<LONG>(i * state.itemHeight), client.right,
                         static_cast<LONG>((i + 1) * state.itemHeight)};

                const bool hovered = state.hovered == static_cast<int>(i);
                const bool active = item.active;
                HBRUSH rowBrush = CreateSolidBrush(hovered ? Theme::CardHover : Theme::Card);
                FillRect(dc, &row, rowBrush);
                DeleteObject(rowBrush);

                // Active indicator: accent bar on the left.
                if (active)
                {
                    RECT bar{0, row.top, 4, row.bottom};
                    const COLORREF accent = Theme::FromHex(core::Utf8ToWide(item.workspace.accentColor));
                    HBRUSH barBrush = CreateSolidBrush(accent);
                    FillRect(dc, &bar, barBrush);
                    DeleteObject(barBrush);
                }

                // Number badge.
                std::wstring numberText = item.number > 0 ? std::to_wstring(item.number) : L"·";
                RECT badge{12, row.top + 10, 44, row.top + 42};
                HBRUSH badgeBrush = CreateSolidBrush(Theme::FromHex(core::Utf8ToWide(item.workspace.accentColor)));
                FillRect(dc, &badge, badgeBrush);
                DeleteObject(badgeBrush);
                HFONT badgeFont = Theme::CreateUiFont(10, true);
                SetTextColor(dc, RGB(255, 255, 255));
                SelectObject(dc, badgeFont);
                DrawTextW(dc, numberText.c_str(), -1, &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                DeleteObject(badgeFont);

                // Name.
                HFONT nameFont = Theme::CreateUiFont(11, false, true);
                SelectObject(dc, nameFont);
                SetTextColor(dc, Theme::Text);
                RECT nameRect{56, row.top + 6, client.right - 12, row.top + 30};
                DrawTextW(dc, core::Utf8ToWide(item.workspace.name).c_str(), -1, &nameRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                DeleteObject(nameFont);

                // Sub-line.
                HFONT subFont = Theme::CreateUiFont(8, false);
                SelectObject(dc, subFont);
                SetTextColor(dc, Theme::TextDim);
                RECT subRect{56, row.top + 28, client.right - 12, row.bottom - 6};
                std::wstring sub = active ? L"Active" : L"";
                DrawTextW(dc, sub.c_str(), -1, &subRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                DeleteObject(subFont);
            }

            EndPaint(hwnd, &ps);
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
                Layout(*state);
                // Start with the active workspace highlighted so Enter immediately confirms it.
                for (size_t i = 0; i < state->items.size(); ++i)
                {
                    if (state->items[i].active)
                    {
                        state->hovered = static_cast<int>(i);
                        break;
                    }
                }
                return 0;
            }
            case WM_PAINT:
                Paint(hwnd, *state);
                return 0;
            case WM_MOUSEMOVE:
            {
                const int hit = HitTest(*state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (hit != state->hovered)
                {
                    state->hovered = hit;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            case WM_LBUTTONUP:
            {
                const int hit = HitTest(*state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (hit >= 0)
                {
                    state->result = hit + 1;
                    PostMessageW(hwnd, kDoneMessage, static_cast<WPARAM>(hit + 1), 0);
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
                    state->hovered = std::clamp(state->hovered + delta, 0, static_cast<int>(state->items.size()) - 1);
                    InvalidateRect(hwnd, nullptr, FALSE);
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
        wc.hCursor = LoadCursor(nullptr, IDC_HAND);
        RegisterClassW(&wc);
    }

    int SwitcherPopup::Show(HINSTANCE instance, const core::MonitorInfo &monitor, std::vector<Item> items)
    {
        auto *state = new PopupState();
        state->items = std::move(items);
        // Size/position must be known BEFORE window creation; Layout used to run in
        // WM_CREATE, so the popup was created 320x400 and mis-centered vertically.
        Layout(*state);

        const int x = monitor.workArea.left + (monitor.workArea.width() - state->width) / 2;
        const int y = monitor.workArea.top + (monitor.workArea.height() - state->height) / 2;

        // Borderless WS_POPUP: no title bar and no DWM open/close animation, so it appears
        // and disappears instantly. It MUST be activatable (no WS_EX_NOACTIVATE) to receive
        // keyboard input; the app already filters its own windows out of management.
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                    kClassName, L"FlowSpace Switcher", WS_POPUP,
                                    x, y, state->width, state->height, nullptr, nullptr, instance, state);
        if (!hwnd)
        {
            delete state;
            return 0;
        }
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd); // activation gives the popup keyboard focus
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
