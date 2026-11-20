#pragma once

#include <Windows.h>
#include <string>

namespace flowspace::ui
{
    // Dark palette for switcher/notification overlays. Neutral graphite surfaces
    // with a muted bronze accent; mirrors ui::ThemeTokens so overlays stay
    // consistent with the settings window.
    namespace Theme
    {
        constexpr COLORREF Bg = RGB(26, 26, 26);           // #1A1A1A main background
        constexpr COLORREF Sidebar = RGB(22, 22, 22);      // #161616 panel surface
        constexpr COLORREF Card = RGB(34, 34, 34);         // #222222 card / row surface
        constexpr COLORREF CardHover = RGB(46, 46, 46);    // #2E2E2E hover
        constexpr COLORREF Border = RGB(58, 58, 58);       // #3A3A3A neutral grey border
        constexpr COLORREF Text = RGB(232, 232, 232);      // #E8E8E8 primary text
        constexpr COLORREF TextDim = RGB(154, 154, 154);   // #9A9A9A secondary text
        constexpr COLORREF Input = RGB(38, 38, 38);        // input field background
        constexpr COLORREF Accent = RGB(168, 126, 90);     // #A87E5A muted bronze

        // Workspace accent colors offered by the picker. Softer, less saturated presets.
        inline constexpr const wchar_t *kAccentPresets[] = {
            L"#A87E5A", L"#8A9A7E", L"#C9A25E", L"#9A7AA0", L"#B87A7A", L"#D16B5A",
            L"#D49A6E", L"#7E9FA0", L"#8FA8C4", L"#A09C7E", L"#C48FA0", L"#8A8A8A",
        };

        // Parse "#RRGGBB" / "#AARRGGBB" to COLORREF; returns Accent on failure.
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
