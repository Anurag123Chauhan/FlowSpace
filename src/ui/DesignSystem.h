#pragma once

#include "core/Models.h"

#include <Windows.h>
#include <string>
#include <vector>

namespace flowspace::ui
{

    // ---------------------------------------------------------------------------
    // Theme tokens — the single source of truth for colors, spacing and typography.
    // All geometry is expressed at 96 DPI and scaled by Theme::DpiScale() at draw time.
    // ---------------------------------------------------------------------------

    enum class ThemeMode
    {
        Light,
        Dark,
        System,
    };

    struct ThemeTokens
    {
        // Surfaces — Neutral Graphite (desaturated, warm-leaning greys; no blue tint).
        COLORREF bg = RGB(26, 26, 26);           // #1A1A1A application background
        COLORREF sidebar = RGB(22, 22, 22);      // #161616 left navigation
        COLORREF surface = RGB(34, 34, 34);      // #222222 primary surface
        COLORREF elevated = RGB(42, 42, 42);     // #2A2A2A elevated surface
        COLORREF border = RGB(58, 58, 58);       // #3A3A3A neutral grey border

        // Text.
        COLORREF text = RGB(232, 232, 232);      // #E8E8E8 primary text
        COLORREF textDim = RGB(154, 154, 154);   // #9A9A9A secondary text
        COLORREF textOnAccent = RGB(255, 255, 255);

        // Interactive — Muted Bronze accent (desaturated brown-orange).
        COLORREF accent = RGB(168, 126, 90);     // #A87E5A muted bronze
        COLORREF accentHover = RGB(150, 110, 78);
        COLORREF accentActive = RGB(130, 95, 68);
        COLORREF inputBg = RGB(38, 38, 38);      // input field background
        COLORREF inputBorder = RGB(58, 58, 58);

        // States.
        COLORREF hover = RGB(46, 46, 46);        // subtle hover on surfaces
        COLORREF selected = RGB(51, 51, 51);     // selected row background
        COLORREF disabled = RGB(70, 70, 70);
        COLORREF disabledText = RGB(128, 128, 128);

        // Semantic — kept distinct and readable in both themes.
        COLORREF success = RGB(46, 160, 67);
        COLORREF warning = RGB(210, 135, 60);
        COLORREF error = RGB(196, 78, 70);

        // Workspace accent presets (user-configurable accents) — softer, less saturated.
        static constexpr const wchar_t *kAccentPresets[] = {
            L"#A87E5A", L"#8A9A7E", L"#C9A25E", L"#9A7AA0", L"#B87A7A", L"#D16B5A",
            L"#D49A6E", L"#7E9FA0", L"#8FA8C4", L"#A09C7E", L"#C48FA0", L"#8A8A8A",
        };
    };

    // Light theme tokens (overrides the dark defaults).
    struct LightThemeTokens
    {
        // Surfaces — Warm Stone (warm off-whites; no cool blue-grey).
        COLORREF bg = RGB(245, 241, 235);        // #F5F1EB warm off-white
        COLORREF sidebar = RGB(255, 255, 255);   // #FFFFFF
        COLORREF surface = RGB(255, 255, 255);   // #FFFFFF
        COLORREF elevated = RGB(238, 230, 221);  // #EEE6DD warm stone
        COLORREF border = RGB(216, 208, 196);    // #D8D0C4 warm neutral grey
        COLORREF text = RGB(42, 42, 42);         // #2A2A2A
        COLORREF textDim = RGB(122, 122, 122);   // #7A7A7A
        COLORREF textOnAccent = RGB(255, 255, 255);
        COLORREF accent = RGB(168, 126, 90);     // #A87E5A muted bronze
        COLORREF accentHover = RGB(150, 110, 78);
        COLORREF accentActive = RGB(130, 95, 68);
        COLORREF inputBg = RGB(255, 255, 255);
        COLORREF inputBorder = RGB(216, 208, 196);
        COLORREF hover = RGB(245, 238, 230);     // #F5EEE6
        COLORREF selected = RGB(238, 230, 221);  // #EEE6DD
        COLORREF disabled = RGB(235, 230, 224);  // #EBE6E0
        COLORREF disabledText = RGB(178, 178, 178);
        COLORREF success = RGB(46, 160, 67);
        COLORREF warning = RGB(210, 135, 60);
        COLORREF error = RGB(196, 78, 70);
    };

    // ---------------------------------------------------------------------------
    // Theme — runtime theme state (mode, DPI scale, fonts, resolved tokens).
    // ---------------------------------------------------------------------------

    class AppTheme
    {
    public:
        AppTheme();

        ~AppTheme();

        // Resolve the effective theme for drawing (system mode reads the Windows
        // apps light/dark preference). Call SetMode() to switch manually.
        void SetMode(ThemeMode mode);
        ThemeMode Mode() const { return mode_; }
        ThemeMode EffectiveMode() const { return effectiveMode_; }

        // DPI scale relative to 96 DPI. Fonts/spacing are scaled by this.
        void SetDpiScale(double scale);
        double DpiScale() const { return dpiScale_; }

        // Converts a 96-DPI design pixel value to the current DPI.
        int Px(int px96) const { return Scale(px96); }

        const ThemeTokens &Tokens() const { return tokens_; }

        // Fonts (created lazily at the current DPI).
        HFONT BrandFont() const;
        HFONT TitleFont() const;
        HFONT HeadingFont() const;
        HFONT BodyFont() const;
        HFONT SmallFont() const;
        HFONT IconFont() const;

        // Spacing values at the current DPI.
        int Space4() const { return Scale(4); }
        int Space8() const { return Scale(8); }
        int Space12() const { return Scale(12); }
        int Space16() const { return Scale(16); }
        int Space20() const { return Scale(20); }
        int Space24() const { return Scale(24); }
        int Space32() const { return Scale(32); }

        // Sidebar width (fixed).
        int SidebarWidth() const { return Scale(208); }

        // Parse "#RRGGBB" to COLORREF (falls back to accent).
        static COLORREF FromHex(const std::wstring &hex);

    private:
        int Scale(int px96) const;
        void RebuildFonts();
        void ResolveSystemMode();

        ThemeMode mode_ = ThemeMode::Dark;
        ThemeMode effectiveMode_ = ThemeMode::Dark;
        ThemeTokens tokens_;
        double dpiScale_ = 1.0;

        mutable HFONT fontBrand_ = nullptr;
        mutable HFONT fontTitle_ = nullptr;
        mutable HFONT fontHeading_ = nullptr;
        mutable HFONT fontBody_ = nullptr;
        mutable HFONT fontSmall_ = nullptr;
        mutable HFONT fontIcon_ = nullptr;
        mutable bool fontsDirty_ = true;
    };

    // ---------------------------------------------------------------------------
    // Drawing primitives (all in the design system's coordinate space, DPI scaled).
    // ---------------------------------------------------------------------------

    namespace Draw
    {
        void Fill(HDC dc, const RECT &rect, COLORREF color);
        void Rect(HDC dc, const RECT &rect, COLORREF color, int width = 1);
        void RoundRect(HDC dc, const RECT &rect, int radius, COLORREF fillColor,
                       COLORREF borderColor, int borderWidth = 1);
        void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1);
        void Text(HDC dc, const std::wstring &text, const RECT &rect, HFONT font, COLORREF color,
                  UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        void TextWrapped(HDC dc, const std::wstring &text, const RECT &rect, HFONT font,
                         COLORREF color);

        // Draws a monochrome line icon glyph from Segoe Fluent Icons / MDL2.
        void Icon(HDC dc, wchar_t glyph, const RECT &rect, HFONT font, COLORREF color);

        // Measures text width with the given font.
        SIZE MeasureText(HDC dc, const std::wstring &text, HFONT font);
    }

    // ---------------------------------------------------------------------------
    // Reusable components. Each draws itself into the given rect and reports the
    // interactive rect (for hit-testing) via `outRect` when non-null.
    // Components are stateless: the caller owns interaction state (hovered/pressed).
    // ---------------------------------------------------------------------------

    namespace Components
    {
        struct Recti
        {
            int left, top, right, bottom;
        };

        // A card / panel with optional border and rounded corners.
        void Card(HDC dc, const RECT &rect, const AppTheme &theme, bool elevated = false, int radius = 6);

        // The FlowSpace logo mark: a white rounded square over an accent square
        // with an accent dot. Drawn directly (not bitmap-scaled) so it stays
        // crisp at any size and DPI.
        void BrandMark(HDC dc, const RECT &rect, const AppTheme &theme);

        // Divider line between sections.
        void Divider(HDC dc, const RECT &rect, const AppTheme &theme);

        // Text button. Returns the content rect (for hit-test).
        RECT Button(HDC dc, const RECT &rect, const AppTheme &theme, const std::wstring &label, HFONT font, bool hovered = false, bool selected = false, bool accent = false);

        // Icon-only button (square).
        RECT IconButton(HDC dc, const RECT &rect, const AppTheme &theme, wchar_t glyph, HFONT iconFont, bool hovered = false, bool selected = false, bool accent = false);

        // Toggle switch.
        RECT Toggle(HDC dc, const RECT &rect, const AppTheme &theme, bool on, bool hovered = false);

        // Badge / status chip.
        RECT Badge(HDC dc, const RECT &rect, const AppTheme &theme, const std::wstring &label, HFONT font, COLORREF fill, COLORREF text);

        // Section heading + optional secondary description.
        void SectionHeader(HDC dc, const RECT &rect, const AppTheme &theme, const std::wstring &heading, const std::wstring &description = L"");

        // Empty state (icon + heading + description).
        void EmptyState(HDC dc, const RECT &rect, const AppTheme &theme, wchar_t glyph, const std::wstring &heading, const std::wstring &description);
    }

}
