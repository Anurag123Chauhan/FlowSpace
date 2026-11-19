#pragma once

#include <Windows.h>
#include <string>

namespace flowspace::ui
{
    // Dark theme palette for the settings UI. Light theme support is deferred; the desktop
    // overlay and workspace cards always follow the workspace's own appearance.
    namespace Theme
    {
        constexpr COLORREF Bg = RGB(23, 25, 29);           // main background
        constexpr COLORREF Sidebar = RGB(16, 18, 21);      // left navigation
        constexpr COLORREF Card = RGB(30, 33, 39);         // card / panel surface
        constexpr COLORREF CardHover = RGB(38, 42, 49);
        constexpr COLORREF Border = RGB(42, 46, 54);
        constexpr COLORREF Text = RGB(232, 234, 237);      // primary text
        constexpr COLORREF TextDim = RGB(154, 160, 166);   // secondary text
        constexpr COLORREF Input = RGB(24, 27, 32);        // input field background
        constexpr COLORREF Accent = RGB(67, 137, 216);     // app accent

        // Workspace accent colors offered by the picker.
        inline constexpr const wchar_t *kAccentPresets[] = {
            L"#4389D8", L"#2EA043", L"#D29922", L"#A371F7", L"#DB61A2", L"#E5534B",
            L"#F0883E", L"#3FB950", L"#39C5CF", L"#58A6FF", L"#BC8CFF", L"#8B949E",
        };

        // Parse "#RRGGBB" / "#AARRGGBB" to COLORREF; returns RGB(67,137,216) on failure.
        COLORREF FromHex(const std::wstring &hex);

        // Mixes a color toward white/black by factor (-1..1).
        COLORREF Adjust(COLORREF c, double factor);

        // Fonts. Sizes are in points. Pass the monitor's DPI scale (1.0 == 96 DPI) for
        // per-monitor UI; 0 uses the system DPI for legacy callers.
        HFONT CreateUiFont(int pointSize, bool bold = false, bool semibold = false, double dpiScale = 0.0);
        HFONT CreateIconFont(int pointSize, double dpiScale = 0.0); // Segoe Fluent Icons / Segoe MDL2 Assets
    }

    // Workspace icon identifiers mapped to Segoe MDL2 Assets glyphs (Win10) with a
    // Segoe Fluent Icons (Win11) preference.
    namespace Glyphs
    {
        struct IconDef
        {
            const wchar_t *id;   // stable identifier stored in config
            const wchar_t *name; // human-readable, shown in the picker
            wchar_t glyph;       // codepoint in the icon font
        };

        // NOTE: glyphs are Segoe MDL2 Assets codepoints.
        inline constexpr IconDef kIcons[] = {
            {L"grid", L"Grid", 0xE8A9},
            {L"code", L"Code", 0xE943},
            {L"book", L"Research", 0xE736},
            {L"home", L"Home", 0xE80F + 0}, // Home
            {L"chat", L"Communication", 0xE8BD},
            {L"mail", L"Mail", 0xE715},
            {L"globe", L"Web", 0xE774},
            {L"star", L"Favorites", 0xE735},
            {L"music", L"Music", 0xE8D6},
            {L"video", L"Entertainment", 0xE786},
            {L"briefcase", L"Work", 0xE7E9},
            {L"camera", L"Photos", 0xE722},
            {L"gamepad", L"Games", 0xE7FC},
            {L"chart", L"Monitoring", 0xE9D2},
            {L"doc", L"Documentation", 0xE8A5},
        };

        wchar_t GlyphFor(const std::wstring &iconId);
    }
}
