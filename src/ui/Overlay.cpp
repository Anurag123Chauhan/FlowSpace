#include "ui/Overlay.h"
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
        constexpr wchar_t kClassName[] = L"FlowSpaceOverlay";
        constexpr UINT_PTR kTimerId = 1;
        constexpr int kHoldMs = 260;

        enum class OverlayPhase
        {
            Enter,
            Hold,
            FadeOut
        };

        struct OverlayState
        {
            core::Workspace workspace;
            int workspaceNumber = 0; // 1-based; 0 = not addressable by number
            std::wstring animationStyle;
            int animationMs = 180;
            OverlayPhase phase = OverlayPhase::Enter;
            ULONGLONG phaseStartTick = 0;
            int width = 0, height = 0; // physical pixels
            int x = 0, y = 0;          // physical screen coords
            std::wstring monitorId;    // for the live-overlay registry (one per monitor)
            COLORREF bg = RGB(20, 24, 30);
            COLORREF bgDark = RGB(14, 17, 22);
            COLORREF accent = RGB(67, 137, 216);
            COLORREF text = RGB(255, 255, 255);
            wchar_t glyph = L'W';

            // Cached full-size premultiplied ARGB card bitmap (rendered once on creation).
            HDC memDc = nullptr;
            HBITMAP bmp = nullptr;
            HGDIOBJ oldBmp = nullptr;
        };

        // One overlay window per monitor at most: rapid switching replaces the running
        // overlay instead of stacking cards on top of each other.
        std::unordered_map<std::wstring, HWND> &LiveOverlays()
        {
            static std::unordered_map<std::wstring, HWND> live;
            return live;
        }

        double EaseOutCubic(double p)
        {
            const double q = 1.0 - p;
            return 1.0 - q * q * q;
        }

        void DestroyCachedBitmap(OverlayState &state)
        {
            if (state.memDc)
            {
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
        }

        // Renders the overlay card ONCE into a cached 32bpp premultiplied ARGB DIB.
        // Subsequent animation frames only update position and SourceConstantAlpha via
        // UpdateLayeredWindow — zero pixel loops or GDI allocations per frame.
        void RenderCardOnce(OverlayState &state)
        {
            DestroyCachedBitmap(state);

            const int width = state.width;
            const int height = state.height;

            HDC screenDc = GetDC(nullptr);
            state.memDc = CreateCompatibleDC(screenDc);
            ReleaseDC(nullptr, screenDc);

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height; // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void *bits = nullptr;
            state.bmp = CreateDIBSection(state.memDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!state.bmp || !bits)
            {
                DestroyCachedBitmap(state);
                return;
            }
            state.oldBmp = SelectObject(state.memDc, state.bmp);

            auto *pixels = static_cast<uint32_t *>(bits);

            // Background: direct-memory vertical gradient (no per-scanline CreateSolidBrush).
            const int r0 = GetRValue(state.bg), g0 = GetGValue(state.bg), b0 = GetBValue(state.bg);
            const int r1 = GetRValue(state.bgDark), g1 = GetGValue(state.bgDark), b1 = GetBValue(state.bgDark);
            const int denom = std::max(1, height - 1);
            for (int y = 0; y < height; ++y)
            {
                const int r = r0 + (r1 - r0) * y / denom;
                const int g = g0 + (g1 - g0) * y / denom;
                const int b = b0 + (b1 - b0) * y / denom;
                const uint32_t rowColor = 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
                                          (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
                uint32_t *row = pixels + static_cast<size_t>(y) * width;
                std::fill(row, row + width, rowColor);
            }

            // Pattern overlay.
            if (state.workspace.overlayPatternOpacity > 0.01)
            {
                const std::wstring pattern = state.workspace.overlayPattern;
                const COLORREF blend = Theme::Adjust(state.bg, 1.0 - state.workspace.overlayPatternOpacity * 3.0);
                const int step = std::max(8, height / 12);
                if (pattern == L"grid" || pattern == L"blueprint")
                {
                    HPEN pen = CreatePen(PS_SOLID, 1, blend);
                    HGDIOBJ oldPen = SelectObject(state.memDc, pen);
                    for (int x = 0; x <= width; x += step)
                    {
                        MoveToEx(state.memDc, x, 0, nullptr);
                        LineTo(state.memDc, x, height);
                    }
                    for (int y = 0; y <= height; y += step)
                    {
                        MoveToEx(state.memDc, 0, y, nullptr);
                        LineTo(state.memDc, width, y);
                    }
                    SelectObject(state.memDc, oldPen);
                    DeleteObject(pen);
                }
                else if (pattern == L"diagonal" || pattern == L"minimal")
                {
                    HPEN pen = CreatePen(PS_SOLID, 1, blend);
                    HGDIOBJ oldPen = SelectObject(state.memDc, pen);
                    for (int d = -height; d < width + height; d += step * 2)
                    {
                        MoveToEx(state.memDc, d, 0, nullptr);
                        LineTo(state.memDc, d + height, height);
                    }
                    SelectObject(state.memDc, oldPen);
                    DeleteObject(pen);
                }
                else if (pattern == L"dots")
                {
                    HBRUSH dot = CreateSolidBrush(blend);
                    for (int y = step / 2; y < height; y += step)
                    {
                        for (int x = step / 2; x < width; x += step)
                        {
                            RECT r{x - 1, y - 1, x + 1, y + 1};
                            FillRect(state.memDc, &r, dot);
                        }
                    }
                    DeleteObject(dot);
                }
            }

            // Accent bar on the left edge.
            RECT accentBar{0, 0, std::max(4, height / 40), height};
            HBRUSH accentBrush = CreateSolidBrush(state.accent);
            FillRect(state.memDc, &accentBar, accentBrush);
            DeleteObject(accentBrush);

            // Text content.
            SetBkMode(state.memDc, TRANSPARENT);
            SetTextColor(state.memDc, state.text);

            HDC refDc = GetDC(nullptr);
            const double fontScale = GetDeviceCaps(refDc, LOGPIXELSX) / 96.0;
            ReleaseDC(nullptr, refDc);

            HGDIOBJ oldFont = GetCurrentObject(state.memDc, OBJ_FONT);

            // Number badge (top-center) when the workspace is addressable by number.
            if (state.workspaceNumber > 0)
            {
                const int badgeSize = static_cast<int>(28 * fontScale);
                const int badgeX = width / 2 - badgeSize / 2;
                const int badgeY = static_cast<int>(12 * fontScale);
                HBRUSH badgeBrush = CreateSolidBrush(state.accent);
                RECT badgeRect{badgeX, badgeY, badgeX + badgeSize, badgeY + badgeSize};
                FillRect(state.memDc, &badgeRect, badgeBrush);
                DeleteObject(badgeBrush);

                HFONT badgeFont = Theme::CreateUiFont(10, true);
                SelectObject(state.memDc, badgeFont);
                RECT textRect{badgeRect.left - 8, badgeRect.top - 2, badgeRect.right + 8, badgeRect.bottom + 2};
                const std::wstring badgeText = std::to_wstring(state.workspaceNumber);
                DrawTextW(state.memDc, badgeText.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                DeleteObject(badgeFont);
            }

            // Icon glyph.
            HFONT glyphFont = Theme::CreateIconFont(std::max(16, static_cast<int>(height * 0.30 / fontScale)));
            SelectObject(state.memDc, glyphFont);
            const wchar_t glyphStr[2] = {state.glyph, 0};
            RECT glyphRect{0, static_cast<int>(height * 0.10), width, static_cast<int>(height * 0.52)};
            DrawTextW(state.memDc, glyphStr, -1, &glyphRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DeleteObject(glyphFont);

            // Workspace name.
            HFONT nameFont = Theme::CreateUiFont(std::max(12, static_cast<int>(height * 0.085 / fontScale)), true);
            SelectObject(state.memDc, nameFont);
            RECT nameRect{width / 10, static_cast<int>(height * 0.55), width - width / 10, static_cast<int>(height * 0.82)};
            DrawTextW(state.memDc, core::Utf8ToWide(state.workspace.name).c_str(), -1, &nameRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            DeleteObject(nameFont);

            SelectObject(state.memDc, oldFont);

            // Ensure GDI has flushed all drawing into the DIB before we touch pixels directly.
            GdiFlush();

            // Analytical rounded-rectangle alpha mask (replaces 300k PtInRegion GDI calls).
            // GDI text/fill leaves alpha=0 on drawn pixels, so we set alpha=255 inside
            // the rounded card and 0 outside.
            const int radius = std::min(24, height / 8);
            const int r2 = radius * radius;
            const int rightInner = width - radius;
            const int bottomInner = height - radius;

            for (int y = 0; y < height; ++y)
            {
                uint32_t *row = pixels + static_cast<size_t>(y) * width;
                const bool inCornerY = (y < radius) || (y >= bottomInner);
                if (!inCornerY)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        row[x] |= 0xFF000000u;
                    }
                    continue;
                }

                const int cy = (y < radius) ? (radius - 1) : bottomInner;
                const int dy = y - cy;
                const int dy2 = dy * dy;

                for (int x = 0; x < width; ++x)
                {
                    if (x >= radius && x < rightInner)
                    {
                        row[x] |= 0xFF000000u;
                    }
                    else
                    {
                        const int cx = (x < radius) ? (radius - 1) : rightInner;
                        const int dx = x - cx;
                        if (dx * dx + dy2 <= r2)
                        {
                            row[x] |= 0xFF000000u;
                        }
                        else
                        {
                            row[x] = 0;
                        }
                    }
                }
            }
        }

        void PresentFrame(HWND hwnd, const OverlayState &state, int x, int y, double alpha)
        {
            if (!state.memDc)
            {
                return;
            }
            POINT ptDst{x, y};
            POINT ptSrc{0, 0};
            SIZE size{state.width, state.height};
            const BYTE alphaByte = static_cast<BYTE>(std::clamp(alpha, 0.0, 1.0) * 255.0);
            BLENDFUNCTION bf{AC_SRC_OVER, 0, alphaByte, AC_SRC_ALPHA};
            UpdateLayeredWindow(hwnd, nullptr, &ptDst, &size, state.memDc, &ptSrc, 0, &bf, ULW_ALPHA);
        }

        LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            auto *state = reinterpret_cast<OverlayState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg)
            {
            case WM_CREATE:
            {
                auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
                state = static_cast<OverlayState *>(create->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

                RenderCardOnce(*state);
                state->phaseStartTick = GetTickCount64();

                if (state->animationStyle == L"none" || state->animationMs <= 0)
                {
                    state->phase = OverlayPhase::Hold;
                    PresentFrame(hwnd, *state, state->x, state->y, 1.0);
                }
                else
                {
                    state->phase = OverlayPhase::Enter;
                    const int offset = (state->animationStyle == L"slide") ? 36 : 0;
                    PresentFrame(hwnd, *state, state->x - offset, state->y, 0.25);
                }
                SetTimer(hwnd, kTimerId, 15, nullptr);
                return 0;
            }
            case WM_TIMER:
            {
                if (!state)
                {
                    return 0;
                }
                const ULONGLONG now = GetTickCount64();
                const int elapsed = static_cast<int>(now - state->phaseStartTick);

                switch (state->phase)
                {
                case OverlayPhase::Enter:
                {
                    const int duration = std::max(1, state->animationMs);
                    const double p = std::min(1.0, static_cast<double>(elapsed) / duration);
                    const double pe = EaseOutCubic(p);
                    const double alpha = 0.25 + 0.75 * pe;
                    const int offset = (state->animationStyle == L"slide")
                                           ? static_cast<int>((1.0 - pe) * 36)
                                           : 0;
                    PresentFrame(hwnd, *state, state->x - offset, state->y, alpha);

                    if (p >= 1.0)
                    {
                        state->phase = OverlayPhase::Hold;
                        state->phaseStartTick = now;
                    }
                    break;
                }
                case OverlayPhase::Hold:
                {
                    if (elapsed >= kHoldMs)
                    {
                        if (state->animationStyle == L"none" || state->animationMs <= 0)
                        {
                            KillTimer(hwnd, kTimerId);
                            DestroyWindow(hwnd);
                        }
                        else
                        {
                            state->phase = OverlayPhase::FadeOut;
                            state->phaseStartTick = now;
                        }
                    }
                    break;
                }
                case OverlayPhase::FadeOut:
                {
                    const int fadeOut = std::clamp(state->animationMs, 100, 160);
                    const double p = std::min(1.0, static_cast<double>(elapsed) / fadeOut);
                    const double alpha = (1.0 - p) * (1.0 - p);
                    PresentFrame(hwnd, *state, state->x, state->y, alpha);

                    if (p >= 1.0)
                    {
                        KillTimer(hwnd, kTimerId);
                        DestroyWindow(hwnd);
                    }
                    break;
                }
                }
                return 0;
            }
            case WM_DESTROY:
            {
                KillTimer(hwnd, kTimerId);
                if (state)
                {
                    DestroyCachedBitmap(*state);
                    auto &live = LiveOverlays();
                    live.erase(state->monitorId);
                    delete state;
                }
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                return 0;
            }
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    void OverlayWindow::RegisterOverlayClass(HINSTANCE instance)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
    }

    void OverlayWindow::Show(HINSTANCE instance, const core::MonitorInfo &monitor, const core::Workspace &workspace,
                             int workspaceNumber, const std::string &animationStyle, int animationMs)
    {
        auto *state = new OverlayState();
        state->workspace = workspace;
        state->workspaceNumber = workspaceNumber;
        state->animationStyle = core::Utf8ToWide(animationStyle);
        state->animationMs = std::max(0, animationMs);
        state->monitorId = monitor.stableId;

        // Only one overlay per monitor: kill the running one so the new card plays its
        // full animation instead of stacking behind it.
        auto &live = LiveOverlays();
        if (auto it = live.find(state->monitorId); it != live.end() && IsWindow(it->second))
        {
            DestroyWindow(it->second);
        }
        live[state->monitorId] = nullptr; // placeholder until the window exists

        state->accent = Theme::FromHex(core::Utf8ToWide(workspace.accentColor));
        if (!workspace.overlayBackgroundColor.empty())
        {
            uint32_t argb = 0;
            if (core::TryParseHexColor8(workspace.overlayBackgroundColor, argb))
            {
                state->bg = RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
            }
        }
        if (state->bg == RGB(20, 24, 30) && workspace.overlayBackgroundColor.empty())
        {
            state->bg = Theme::Adjust(state->accent, -0.75);
        }
        state->bgDark = Theme::Adjust(state->bg, -0.35);
        state->glyph = Glyphs::GlyphFor(core::Utf8ToWide(workspace.icon));

        // Size: fraction of the monitor work area (physical pixels; PMv2 DPI-aware process).
        constexpr double wf = 0.42, hf = 0.34;
        state->width = std::max(280, static_cast<int>(monitor.workArea.width() * wf));
        state->height = std::max(180, static_cast<int>(monitor.workArea.height() * hf));
        state->x = monitor.workArea.left + (monitor.workArea.width() - state->width) / 2;
        state->y = monitor.workArea.top + (monitor.workArea.height() - state->height) / 2;

        constexpr DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
        HWND hwnd = CreateWindowExW(exStyle, kClassName, L"", WS_POPUP,
                                    state->x, state->y, state->width, state->height,
                                    nullptr, nullptr, instance, state);
        if (!hwnd)
        {
            live.erase(state->monitorId);
            delete state;
            return;
        }
        live[state->monitorId] = hwnd;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
}
