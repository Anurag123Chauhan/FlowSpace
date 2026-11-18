#include "ui/Theme.h"
#include "core/StringUtils.h"

namespace flowspace::ui
{
    COLORREF Theme::FromHex(const std::wstring &hex)
    {
        uint32_t rgb = 0;
        if (core::TryParseHexColor(hex, rgb))
        {
            return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        }
        return Accent;
    }

    COLORREF Theme::Adjust(COLORREF c, double factor)
    {
        auto channel = [&](int v) -> int
        {
            const int target = factor >= 0 ? 255 : 0;
            const double a = factor >= 0 ? factor : -factor;
            return static_cast<int>(v + (target - v) * a);
        };
        return RGB(channel(GetRValue(c)), channel(GetGValue(c)), channel(GetBValue(c)));
    }

    namespace
    {
        int FontDpi(double dpiScale)
        {
            if (dpiScale > 0.0)
            {
                return static_cast<int>(96.0 * dpiScale + 0.5);
            }
            HDC dc = GetDC(nullptr);
            const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
            ReleaseDC(nullptr, dc);
            return dpi;
        }
    }

    HFONT Theme::CreateUiFont(int pointSize, bool bold, bool semibold, double dpiScale)
    {
        // "Segoe UI Variable Text" ships with Windows 11; fall back to Segoe UI.
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(pointSize, FontDpi(dpiScale), 72);
        lf.lfWeight = bold ? FW_BOLD : (semibold ? FW_SEMIBOLD : FW_NORMAL);
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        return CreateFontIndirectW(&lf);
    }

    HFONT Theme::CreateIconFont(int pointSize, double dpiScale)
    {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(pointSize, FontDpi(dpiScale), 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons"); // Windows 11
        HFONT font = CreateFontIndirectW(&lf);
        if (font)
        {
            // If the font family does not exist, GDI silently substitutes; verify by name
            // enumeration is overkill for v1 — "Segoe MDL2 Assets" is the Windows 10 fallback
            // and has identical codepoints for our set.
            return font;
        }
        wcscpy_s(lf.lfFaceName, L"Segoe MDL2 Assets");
        return CreateFontIndirectW(&lf);
    }
}

namespace flowspace::ui::Glyphs
{
    wchar_t GlyphFor(const std::wstring &iconId)
    {
        for (const auto &icon : kIcons)
        {
            if (iconId == icon.id)
            {
                return icon.glyph;
            }
        }
        return 0xE80F; // Home glyph as generic fallback
    }
}
