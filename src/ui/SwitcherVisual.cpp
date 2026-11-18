#include "ui/SwitcherVisual.h"

#include <algorithm>
#include <cmath>

namespace flowspace::ui::visual
{
    double ScaleFor(const core::MonitorInfo &monitor)
    {
        return std::max(1.0, std::max(monitor.dpiX, monitor.dpiY) / 96.0);
    }

    COLORREF Tint(COLORREF base, COLORREF accent, double amount)
    {
        auto mix = [amount](int a, int b)
        { return static_cast<int>(a + (b - a) * amount + 0.5); };
        return RGB(mix(GetRValue(base), GetRValue(accent)),
                   mix(GetGValue(base), GetGValue(accent)),
                   mix(GetBValue(base), GetBValue(accent)));
    }

    void DrawMonitorIcon(HDC dc, int left, int top, int size, COLORREF color, double scale)
    {
        const int penWidth = std::max(1, static_cast<int>(1.4 * scale + 0.5));
        HPEN pen = CreatePen(PS_SOLID, penWidth, color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

        const int right = left + size;
        const int screenBottom = top + std::max(6, static_cast<int>(size * 0.72 + 0.5));
        Rectangle(dc, left, top, right, screenBottom);

        const int standX = (left + right) / 2;
        const int standBottom = screenBottom + std::max(2, static_cast<int>(size * 0.14 + 0.5));
        MoveToEx(dc, standX, screenBottom, nullptr);
        LineTo(dc, standX, standBottom);

        const int halfBase = std::max(3, static_cast<int>(size * 0.28 + 0.5));
        MoveToEx(dc, standX - halfBase, standBottom, nullptr);
        LineTo(dc, standX + halfBase, standBottom);

        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    void ApplyCardAlpha(uint32_t *pixels, int width, int height, const RECT &card,
                        int radius, int shadowMargin, int panelAlpha, double scale, COLORREF border)
    {
        const double cx = (card.left + card.right) * 0.5;
        const double cy = (card.top + card.bottom) * 0.5;
        const double halfW = (card.right - card.left) * 0.5;
        const double halfH = (card.bottom - card.top) * 0.5;
        const double r = std::max(1.0, static_cast<double>(radius));
        const double borderPx = std::max(1.0, scale);
        const double shadowDecay = std::max(1.0, shadowMargin / 3.0);

        for (int y = 0; y < height; ++y)
        {
            uint32_t *row = pixels + static_cast<size_t>(y) * width;
            for (int x = 0; x < width; ++x)
            {
                // Signed distance to the rounded rectangle (negative = inside).
                const double qx = std::abs(x + 0.5 - cx) - (halfW - r);
                const double qy = std::abs(y + 0.5 - cy) - (halfH - r);
                const double mx = std::max(qx, 0.0), my = std::max(qy, 0.0);
                const double d = std::sqrt(mx * mx + my * my) + std::min(std::max(qx, qy), 0.0) - r;

                if (d <= 0.5)
                {
                    const double cover = std::clamp(0.5 - d, 0.0, 1.0);
                    if (cover <= 0.0)
                    {
                        row[x] = 0;
                        continue;
                    }
                    const int a = static_cast<int>(panelAlpha * cover + 0.5);
                    uint32_t c = row[x];
                    int rr = GetRValue(c), gg = GetGValue(c), bb = GetBValue(c);
                    if (d > -borderPx)
                    {
                        rr = GetRValue(border);
                        gg = GetGValue(border);
                        bb = GetBValue(border);
                    }
                    // UpdateLayeredWindow expects premultiplied alpha.
                    row[x] = (static_cast<uint32_t>(a) << 24) |
                             (static_cast<uint32_t>(bb * a / 255) << 16) |
                             (static_cast<uint32_t>(gg * a / 255) << 8) |
                             static_cast<uint32_t>(rr * a / 255);
                }
                else
                {
                    const double sd = d - 0.5;
                    if (sd >= shadowMargin)
                    {
                        row[x] = 0;
                        continue;
                    }
                    const int a = static_cast<int>(64.0 * std::exp(-sd / shadowDecay) + 0.5);
                    row[x] = static_cast<uint32_t>(a) << 24; // black shadow
                }
            }
        }
    }
}
