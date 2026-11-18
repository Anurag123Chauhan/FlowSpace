#pragma once

#include "core/Models.h"
#include <Windows.h>
#include <cstdint>

namespace flowspace::ui::visual
{
    // Effective DPI scale of the monitor the UI is positioned on (96 DPI == 1.0).
    double ScaleFor(const core::MonitorInfo &monitor);

    // Mixes an accent color into a base color by amount (0..1).
    COLORREF Tint(COLORREF base, COLORREF accent, double amount);

    // Draws the same minimal monitor outline used by both switcher surfaces.
    // `size` and `scale` are in physical pixels / DPI scale respectively.
    void DrawMonitorIcon(HDC dc, int left, int top, int size, COLORREF color, double scale);

    // Finishes a 32bpp top-down DIB: rounds the card corners, draws a 1px border,
    // adds a soft drop shadow and premultiplies alpha for UpdateLayeredWindow.
    // The card area must already be filled with opaque pixels before this call.
    void ApplyCardAlpha(uint32_t *pixels, int width, int height, const RECT &card, int radius, int shadowMargin, int panelAlpha, double scale, COLORREF border);
}
