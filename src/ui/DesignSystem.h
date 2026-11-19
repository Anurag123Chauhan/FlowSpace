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
        // Surfaces.
        COLORREF bg = RGB(17, 19, 24);       // #111318 application background
        COLORREF sidebar = RGB(22, 25, 31);  // #16191F left navigation
        COLORREF surface = RGB(28, 32, 40);  // #1C2028 primary surface
        COLORREF elevated = RGB(37, 43, 53); // #252B35 elevated surface
        COLORREF border = RGB(48, 55, 68);   // #303744 border

        // Text.
        COLORREF text = RGB(241, 243, 247);    // #F1F3F7 primary text
        COLORREF textDim = RGB(146, 155, 170); // #929BAA secondary text
        COLORREF textOnAccent = RGB(255, 255, 255);

        // Interactive.
        COLORREF accent = RGB(87, 155, 255); // #579BFF accent blue
        COLORREF accentHover = RGB(74, 143, 240);
        COLORREF accentActive = RGB(54, 128, 224);
        COLORREF inputBg = RGB(24, 27, 32); // input field background
        COLORREF inputBorder = RGB(48, 55, 68);

        // States.
        COLORREF hover = RGB(42, 49, 60);    // subtle hover on surfaces
        COLORREF selected = RGB(51, 63, 80); // selected row background
        COLORREF disabled = RGB(70, 76, 86);
        COLORREF disabledText = RGB(110, 118, 128);

        // Semantic.
        COLORREF success = RGB(46, 160, 67);
        COLORREF warning = RGB(240, 136, 62);
        COLORREF error = RGB(229, 83, 75);

        // Workspace accent presets (user-configurable accents).
        static constexpr const wchar_t *kAccentPresets[] = {
            L"#4389D8",
            L"#2EA043",
            L"#D29922",
            L"#A371F7",
            L"#DB61A2",
            L"#E5534B",
            L"#F0883E",
            L"#3FB950",
            L"#39C5CF",
            L"#58A6FF",
            L"#BC8CFF",
            L"#8B949E",
        };
    };

    // Light theme tokens (overrides the dark defaults).
    struct LightThemeTokens
    {
        COLORREF bg = RGB(245, 246, 248);       // #F5F6F8
        COLORREF sidebar = RGB(255, 255, 255);  // #FFFFFF
        COLORREF surface = RGB(255, 255, 255);  // #FFFFFF
        COLORREF elevated = RGB(238, 241, 245); // #EEF1F5
        COLORREF border = RGB(225, 229, 235);   // #E1E5EB
        COLORREF text = RGB(32, 38, 49);        // #202631
        COLORREF textDim = RGB(105, 117, 134);  // #697586
        COLORREF textOnAccent = RGB(255, 255, 255);
        COLORREF accent = RGB(40, 120, 219); // #2878DB
        COLORREF accentHover = RGB(33, 106, 193);
        COLORREF accentActive = RGB(26, 89, 164);
        COLORREF inputBg = RGB(255, 255, 255);
        COLORREF inputBorder = RGB(225, 229, 235);
        COLORREF hover = RGB(235, 239, 244);
        COLORREF selected = RGB(226, 232, 240);
        COLORREF disabled = RGB(229, 233, 238);
        COLORREF disabledText = RGB(169, 176, 185);
        COLORREF success = RGB(46, 160, 67);
        COLORREF warning = RGB(240, 136, 62);
        COLORREF error = RGB(229, 83, 75);
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
