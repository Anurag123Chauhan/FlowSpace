#include "ui/SwitchNotification.h"
#include "ui/SwitcherVisual.h"
#include "ui/Theme.h"
#include "core/StringUtils.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace flowspace::ui
{
    namespace
    {
        constexpr wchar_t kClassName[] = L"FlowSpaceSwitchNotification";
        constexpr UINT_PTR kTimerId = 1;
        constexpr int kFrameMs = 16;
        constexpr int kVisibleMs = 800;

        enum class Phase
        {
            FadeIn,
            Hold,
            FadeOut
        };

        struct NotificationState
        {
            core::Workspace workspace;
            int workspaceNumber = 0;
            bool animate = true;
            int fadeMs = 160;
            Phase phase = Phase::FadeIn;
            ULONGLONG startTick = 0;

            double scale = 1.0;
            int margin = 0;
            int cardW = 0, cardH = 0;
            int width = 0, height = 0;
            int x = 0, y = 0;
            std::wstring monitorId;

            HDC memDc = nullptr;
            HBITMAP bmp = nullptr;
            HGDIOBJ oldBmp = nullptr;
        };

        // At most one notification per monitor; rapid switching replaces the old one.
        std::unordered_map<std::wstring, HWND> &LiveNotifications()
        {
            static std::unordered_map<std::wstring, HWND> live;
            return live;
        }

        int Scaled(int logical, double scale)
        {
            return static_cast<int>(logical * scale + 0.5);
        }

        double SmoothStep(double p)
        {
            return p * p * (3.0 - 2.0 * p);
        }

        double EaseOutCubic(double p)
        {
            const double q = 1.0 - p;
            return 1.0 - q * q * q;
        }

        void DestroyBitmap(NotificationState &state)
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

        void RenderCard(NotificationState &state)
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

            const RECT card{state.margin, state.margin, state.margin + state.cardW, state.margin + state.cardH};
            const COLORREF panel = Theme::Sidebar;
            const uint32_t opaque = 0xFF000000u | (static_cast<uint32_t>(GetBValue(panel)) << 16) | (static_cast<uint32_t>(GetGValue(panel)) << 8) | static_cast<uint32_t>(GetRValue(panel));
            for (int y = card.top; y < card.bottom; ++y)
            {
                uint32_t *row = pixels + static_cast<size_t>(y) * state.width + card.left;
                std::fill(row, row + state.cardW, opaque);
            }

            HDC dc = state.memDc;
            const double s = state.scale;
            const COLORREF accent = Theme::FromHex(core::Utf8ToWide(state.workspace.accentColor));
            const int centerY = (card.top + card.bottom) / 2;
            SetBkMode(dc, TRANSPARENT);

            // Monitor icon on the left.
            const int iconSize = Scaled(30, s);
            visual::DrawMonitorIcon(dc, card.left + Scaled(20, s), centerY - Scaled(13, s), iconSize, accent, s);

            // Vertical separator.
            RECT separator{card.left + Scaled(64, s), card.top + Scaled(18, s), card.left + Scaled(65, s), card.bottom - Scaled(18, s)};
            HBRUSH sepBrush = CreateSolidBrush(Theme::Border);
            FillRect(dc, &separator, sepBrush);
            DeleteObject(sepBrush);

            // Name and secondary line.
            const int textLeft = card.left + Scaled(78, s);
            const int textRight = card.right - Scaled(58, s);
            HFONT nameFont = Theme::CreateUiFont(11, false, true, s);
            HGDIOBJ oldFont = SelectObject(dc, nameFont);
            SetTextColor(dc, Theme::Text);
            RECT nameRect{textLeft, card.top + Scaled(13, s), textRight, card.top + Scaled(44, s)};
            DrawTextW(dc, core::Utf8ToWide(state.workspace.name).c_str(), -1, &nameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, oldFont);
            DeleteObject(nameFont);

            std::wstring secondary = L"Switched to Workspace";
            if (state.workspaceNumber > 0)
            {
                secondary += L" " + std::to_wstring(state.workspaceNumber);
            }
            HFONT subFont = Theme::CreateUiFont(8, false, false, s);
            HGDIOBJ oldSubFont = SelectObject(dc, subFont);
            SetTextColor(dc, Theme::TextDim);
            RECT subRect{textLeft, card.top + Scaled(44, s), card.right - Scaled(20, s), card.bottom - Scaled(12, s)};
            DrawTextW(dc, secondary.c_str(), -1, &subRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, oldSubFont);
            DeleteObject(subFont);

            // Subtle success check on the right.
            const int checkX = card.right - Scaled(30, s);
            const int checkR = Scaled(8, s);
            HBRUSH checkBrush = CreateSolidBrush(accent);
            HGDIOBJ oldBrush = SelectObject(dc, checkBrush);
            Ellipse(dc, checkX - checkR, centerY - checkR, checkX + checkR, centerY + checkR);
            SelectObject(dc, oldBrush);
            DeleteObject(checkBrush);

            HPEN checkPen = CreatePen(PS_SOLID, std::max(1, Scaled(1, s)), Theme::Sidebar);
            HGDIOBJ oldPen = SelectObject(dc, checkPen);
            MoveToEx(dc, checkX - Scaled(4, s), centerY, nullptr);
            LineTo(dc, checkX - Scaled(1, s), centerY + Scaled(3, s));
            LineTo(dc, checkX + Scaled(4, s), centerY - Scaled(3, s));
            SelectObject(dc, oldPen);
            DeleteObject(checkPen);

            GdiFlush();
            visual::ApplyCardAlpha(pixels, state.width, state.height, card, Scaled(14, s), state.margin, 240, s, Theme::Border);
        }

        void PresentFrame(HWND hwnd, const NotificationState &state, double alpha, double zoom)
        {
            if (!state.memDc)
            {
                return;
            }
            const int w = std::max(1, static_cast<int>(state.width * zoom + 0.5));
            const int h = std::max(1, static_cast<int>(state.height * zoom + 0.5));
            POINT dst{state.x + (state.width - w) / 2, state.y + (state.height - h) / 2};
            POINT src{0, 0};
            SIZE size{w, h};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(std::clamp(alpha, 0.0, 1.0) * 255.0 + 0.5), AC_SRC_ALPHA};
            UpdateLayeredWindow(hwnd, nullptr, &dst, &size, state.memDc, &src, 0, &blend, ULW_ALPHA);
        }

        void Animate(HWND hwnd, NotificationState &state, int elapsed)
        {
            if (!state.animate)
            {
                PresentFrame(hwnd, state, 1.0, 1.0);
                if (elapsed >= kVisibleMs)
                {
                    KillTimer(hwnd, kTimerId);
                    DestroyWindow(hwnd);
                }
                return;
            }

            const int fade = state.fadeMs;
            const int hold = kVisibleMs - fade * 2;
            if (elapsed < fade)
            {
                const double p = std::clamp(static_cast<double>(elapsed) / fade, 0.0, 1.0);
                state.phase = Phase::FadeIn;
                PresentFrame(hwnd, state, SmoothStep(p), 0.96 + 0.04 * EaseOutCubic(p));
                return;
            }
            if (elapsed < fade + hold)
            {
                state.phase = Phase::Hold;
                PresentFrame(hwnd, state, 1.0, 1.0);
                return;
            }
            if (elapsed < kVisibleMs)
            {
                const double p = std::clamp(static_cast<double>(elapsed - fade - hold) / fade, 0.0, 1.0);
                state.phase = Phase::FadeOut;
                PresentFrame(hwnd, state, 1.0 - SmoothStep(p), 1.0);
                return;
            }
            KillTimer(hwnd, kTimerId);
            DestroyWindow(hwnd);
        }

        LRESULT CALLBACK NotificationWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            auto *state = reinterpret_cast<NotificationState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg)
            {
            case WM_CREATE:
            {
                auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
                state = static_cast<NotificationState *>(create->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
                RenderCard(*state);
                state->startTick = GetTickCount64();
                Animate(hwnd, *state, 0);
                // The timer is installed by Show() once the window exists, so a failure
                // can be handled without destroying the window inside WM_CREATE.
                return 0;
            }
            case WM_TIMER:
            {
                if (state)
                {
                    Animate(hwnd, *state, static_cast<int>(GetTickCount64() - state->startTick));
                }
                return 0;
            }
            case WM_DESTROY:
            {
                KillTimer(hwnd, kTimerId);
                if (state)
                {
                    DestroyBitmap(*state);
                    LiveNotifications().erase(state->monitorId);
                    delete state;
                }
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                return 0;
            }
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    void RegisterSwitchNotificationClass(HINSTANCE instance)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = NotificationWndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
    }

    void SwitchNotification::Show(HINSTANCE instance, const core::MonitorInfo &monitor, const core::Workspace &workspace, int workspaceNumber, const std::string &animationStyle, int animationMs)
    {
        auto *state = new NotificationState();
        state->workspace = workspace;
        state->workspaceNumber = workspaceNumber;
        state->monitorId = monitor.stableId;
        state->animate = animationStyle != "none";
        state->fadeMs = state->animate ? std::clamp(animationMs, 80, 200) : 0;

        state->scale = visual::ScaleFor(monitor);
        const double s = state->scale;
        state->margin = Scaled(12, s);
        state->cardW = Scaled(320, s);
        state->cardH = Scaled(80, s);
        state->width = state->cardW + state->margin * 2;
        state->height = state->cardH + state->margin * 2;
        state->x = monitor.workArea.left + std::max(0, (monitor.workArea.width() - state->cardW) / 2) - state->margin;

        // Bottom-center, just above the taskbar (workArea already excludes it).
        // The window rect includes the shadow margin, so offset by one margin here.
        const int bottomGap = Scaled(24, s);
        state->y = monitor.workArea.top + monitor.workArea.height() - state->cardH - state->margin - bottomGap;
        state->x = std::max(monitor.workArea.left, state->x);
        state->y = std::max(monitor.workArea.top, state->y);

        auto &live = LiveNotifications();
        if (auto it = live.find(state->monitorId); it != live.end() && IsWindow(it->second))
        {
            DestroyWindow(it->second);
        }
        live[state->monitorId] = nullptr;

        constexpr DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
        HWND hwnd = CreateWindowExW(exStyle, kClassName, L"", WS_POPUP, state->x, state->y, state->width, state->height, nullptr, nullptr, instance, state);
        if (!hwnd)
        {
            live.erase(state->monitorId);
            delete state;
            return;
        }
        live[state->monitorId] = hwnd;

        // Without WM_TIMER the notification would never animate or dismiss itself.
        // Fail cleanly instead of leaving a stuck window on screen.
        if (!SetTimer(hwnd, kTimerId, kFrameMs, nullptr))
        {
            DestroyWindow(hwnd); // WM_DESTROY frees the bitmap, state and monitor entry
            return;
        }
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
}
