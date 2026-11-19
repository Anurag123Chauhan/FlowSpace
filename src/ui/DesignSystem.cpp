#include "ui/DesignSystem.h"
#include "core/StringUtils.h"

#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace flowspace::ui
{

    // ===========================================================================
    // Theme
    // ===========================================================================

    namespace
    {
        // GDI substitutes a missing family silently, so verify the face exists
        // before using it and fall back to the Windows 10 / Windows 11 variants.
        bool FaceAvailable(const wchar_t *face)
        {
            HDC dc = GetDC(nullptr);
            if (!dc)
            {
                return false;
            }
            LOGFONTW lf{};
            lf.lfCharSet = DEFAULT_CHARSET;
            wcscpy_s(lf.lfFaceName, face);

            bool found = false;
            EnumFontFamiliesExW(dc, &lf,
                                [](const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM param) -> int
                                {
                                    *reinterpret_cast<bool *>(param) = true;
                                    return 0;
                                },
                                reinterpret_cast<LPARAM>(&found), 0);
            ReleaseDC(nullptr, dc);
            return found;
        }

        const wchar_t *UiFace()
        {
            static const wchar_t *face = FaceAvailable(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
            return face;
        }

        const wchar_t *IconFace()
        {
            static const wchar_t *face = FaceAvailable(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
            return face;
        }
    }

    AppTheme::AppTheme()
    {
        RebuildFonts();
    }

    AppTheme::~AppTheme()
    {
        auto del = [](HFONT &f)
        {
            if (f)
            {
                DeleteObject(f);
                f = nullptr;
            }
        };
        del(fontBrand_);
        del(fontTitle_);
        del(fontHeading_);
        del(fontBody_);
        del(fontSmall_);
        del(fontIcon_);
    }

    void AppTheme::SetMode(ThemeMode mode)
    {
        mode_ = mode;
        ResolveSystemMode();
        fontsDirty_ = true;
    }

    void AppTheme::SetDpiScale(double scale)
    {
        if (scale <= 0.0)
        {
            scale = 1.0;
        }
        if (std::abs(scale - dpiScale_) < 0.001)
        {
            return;
        }
        dpiScale_ = scale;
        fontsDirty_ = true;
    }

    void AppTheme::ResolveSystemMode()
    {
        if (mode_ != ThemeMode::System)
        {
            effectiveMode_ = mode_;
        }
        else
        {
            // Query the Windows apps light/dark preference.
            BOOL dark = FALSE;
            HKEY key = nullptr;
            LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key);
            if (rc == ERROR_SUCCESS)
            {
                DWORD value = 0;
                DWORD size = sizeof(value);
                if (RegGetValueW(key, nullptr, L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
                {
                    dark = (value == 0);
                }
                RegCloseKey(key);
            }
            effectiveMode_ = dark ? ThemeMode::Dark : ThemeMode::Light;
        }

        if (effectiveMode_ == ThemeMode::Light)
        {
            const LightThemeTokens lt{};
            tokens_.bg = lt.bg;
            tokens_.sidebar = lt.sidebar;
            tokens_.surface = lt.surface;
            tokens_.elevated = lt.elevated;
            tokens_.border = lt.border;
            tokens_.text = lt.text;
            tokens_.textDim = lt.textDim;
            tokens_.textOnAccent = lt.textOnAccent;
            tokens_.accent = lt.accent;
            tokens_.accentHover = lt.accentHover;
            tokens_.accentActive = lt.accentActive;
            tokens_.inputBg = lt.inputBg;
            tokens_.inputBorder = lt.inputBorder;
            tokens_.hover = lt.hover;
            tokens_.selected = lt.selected;
            tokens_.disabled = lt.disabled;
            tokens_.disabledText = lt.disabledText;
            tokens_.success = lt.success;
            tokens_.warning = lt.warning;
            tokens_.error = lt.error;
        }
        else
        {
            // Dark defaults are already the ThemeTokens initializers.
            ThemeTokens dt{}; // resets to dark
            tokens_ = dt;
        }
    }

    int AppTheme::Scale(int px96) const
    {
        return static_cast<int>(px96 * dpiScale_ + 0.5);
    }

    void AppTheme::RebuildFonts()
    {
        auto del = [](HFONT &f)
        {
            if (f)
            {
                DeleteObject(f);
                f = nullptr;
            }
        };
        del(fontBrand_);
        del(fontTitle_);
        del(fontHeading_);
        del(fontBody_);
        del(fontSmall_);
        del(fontIcon_);

        auto dpi = [this]()
        {
            return static_cast<unsigned>(96.0 * dpiScale_ + 0.5);
        };

        auto makeFont = [&](int pointSize, int weight, const wchar_t *face) -> HFONT
        {
            LOGFONTW lf{};
            lf.lfHeight = -MulDiv(pointSize, static_cast<int>(dpi()), 72);
            lf.lfWeight = weight;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfQuality = CLEARTYPE_QUALITY;
            lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
            lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
            wcscpy_s(lf.lfFaceName, face);
            return CreateFontIndirectW(&lf);
        };

        const wchar_t *ui = UiFace();
        fontBrand_ = makeFont(14, FW_SEMIBOLD, ui);  // ~19px product name
        fontTitle_ = makeFont(18, FW_SEMIBOLD, ui);  // ~24px page title
        fontHeading_ = makeFont(12, FW_SEMIBOLD, ui); // ~16px section heading
        fontBody_ = makeFont(10, FW_NORMAL, ui);      // ~13px body
        fontSmall_ = makeFont(9, FW_NORMAL, ui);      // ~12px secondary
        fontIcon_ = makeFont(13, FW_NORMAL, IconFace()); // ~17px icon glyphs

        fontsDirty_ = false;
    }

    HFONT AppTheme::BrandFont() const
    {
        if (fontsDirty_)
        {
            const_cast<AppTheme *>(this)->RebuildFonts();
        }
        return fontBrand_;
    }

    HFONT AppTheme::TitleFont() const
    {
        if (fontsDirty_)
        {
            const_cast<AppTheme *>(this)->RebuildFonts();
        }
        return fontTitle_;
    }

    HFONT AppTheme::HeadingFont() const
    {
        if (fontsDirty_)
        {
            const_cast<AppTheme *>(this)->RebuildFonts();
        }
        return fontHeading_;
    }

    HFONT AppTheme::BodyFont() const
    {
        if (fontsDirty_)
        {
            const_cast<AppTheme *>(this)->RebuildFonts();
        }
        return fontBody_;
    }

    HFONT AppTheme::SmallFont() const
    {
        if (fontsDirty_)
        {
            const_cast<AppTheme *>(this)->RebuildFonts();
        }
        return fontSmall_;
    }

    HFONT AppTheme::IconFont() const
    {
        if (fontsDirty_)
        {
            const_cast<AppTheme *>(this)->RebuildFonts();
        }
        return fontIcon_;
    }

    COLORREF AppTheme::FromHex(const std::wstring &hex)
    {
        uint32_t rgb = 0;
        if (core::TryParseHexColor(hex, rgb))
        {
            return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        }
        return RGB(87, 155, 255);
    }

    // ===========================================================================
    // Drawing primitives
    // ===========================================================================

    namespace Draw
    {
        namespace
        {
            Gdiplus::Color GdiColor(COLORREF color)
            {
                return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
            }

            // The UI is single-threaded; start GDI+ once for the process lifetime.
            void EnsureGdiPlus()
            {
                static ULONG_PTR token = 0;
                if (token == 0)
                {
                    Gdiplus::GdiplusStartupInput input;
                    Gdiplus::GdiplusStartup(&token, &input, nullptr);
                }
            }
        }

        void Fill(HDC dc, const RECT &rect, COLORREF color)
        {
            HBRUSH brush = CreateSolidBrush(color);
            HGDIOBJ old = SelectObject(dc, brush);
            FillRect(dc, &rect, brush);
            SelectObject(dc, old);
            DeleteObject(brush);
        }

        void Rect(HDC dc, const RECT &rect, COLORREF color, int width)
        {
            HPEN pen = CreatePen(PS_SOLID, width, color);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }

        void RoundRect(HDC dc, const RECT &rect, int radius, COLORREF fillColor, COLORREF borderColor, int borderWidth)
        {
            const int width = rect.right - rect.left;
            const int height = rect.bottom - rect.top;
            if (width <= 0 || height <= 0)
            {
                return;
            }

            // GDI's RoundRect has hard, aliased corners. Draw the shape through
            // GDI+ so card, button and toggle edges stay smooth at every DPI.
            EnsureGdiPlus();

            Gdiplus::Graphics graphics(dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

            const float x = static_cast<float>(rect.left);
            const float y = static_cast<float>(rect.top);
            const float w = static_cast<float>(width);
            const float h = static_cast<float>(height);
            const float maxRadius = std::min(w, h) / 2.0f;
            const float r = std::min(std::max(0, radius), static_cast<int>(maxRadius));

            Gdiplus::GraphicsPath path;
            if (r <= 0.5f)
            {
                path.AddRectangle(Gdiplus::RectF(x, y, w, h));
            }
            else
            {
                const float d = r * 2.0f;
                path.AddArc(x, y, d, d, 180.0f, 90.0f);
                path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
                path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
                path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
                path.CloseFigure();
            }

            Gdiplus::SolidBrush brush(GdiColor(fillColor));
            graphics.FillPath(&brush, &path);

            if (borderWidth > 0 && borderColor != fillColor)
            {
                Gdiplus::Pen pen(GdiColor(borderColor), static_cast<Gdiplus::REAL>(borderWidth));
                pen.SetAlignment(Gdiplus::PenAlignmentInset);
                graphics.DrawPath(&pen, &path);
            }
        }

        void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width)
        {
            HPEN pen = CreatePen(PS_SOLID, width, color);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            MoveToEx(dc, x1, y1, nullptr);
            LineTo(dc, x2, y2);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }

        void Text(HDC dc, const std::wstring &text, const RECT &rect, HFONT font, COLORREF color, UINT format)
        {
            if (text.empty())
            {
                return;
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, color);
            HGDIOBJ old = SelectObject(dc, font);
            DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), const_cast<RECT *>(&rect), format | DT_END_ELLIPSIS);
            SelectObject(dc, old);
        }

        void TextWrapped(HDC dc, const std::wstring &text, const RECT &rect, HFONT font, COLORREF color)
        {
            if (text.empty())
            {
                return;
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, color);
            HGDIOBJ old = SelectObject(dc, font);
            DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), const_cast<RECT *>(&rect), DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
            SelectObject(dc, old);
        }

        void Icon(HDC dc, wchar_t glyph, const RECT &rect, HFONT font, COLORREF color)
        {
            if (glyph == 0)
            {
                return;
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, color);
            HGDIOBJ old = SelectObject(dc, font);
            wchar_t buf[2] = {glyph, 0};
            DrawTextW(dc, buf, 1, const_cast<RECT *>(&rect), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
            SelectObject(dc, old);
        }

        SIZE MeasureText(HDC dc, const std::wstring &text, HFONT font)
        {
            SIZE size{};
            if (text.empty())
            {
                return size;
            }
            HGDIOBJ old = SelectObject(dc, font);
            GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
            SelectObject(dc, old);
            return size;
        }
    } // namespace Draw

    // ===========================================================================
    // Components
    // ===========================================================================

    namespace Components
    {
        namespace
        {
            COLORREF SurfaceOf(const ThemeTokens &t) { return t.surface; }

            HFONT BodyFontOf(const AppTheme &theme) { return theme.BodyFont(); }
        }

        void Card(HDC dc, const RECT &rect, const AppTheme &theme, bool elevated, int radius)
        {
            const COLORREF surface = elevated ? theme.Tokens().elevated : theme.Tokens().surface;
            Draw::RoundRect(dc, rect, radius, surface, theme.Tokens().border, 1);
        }

        void BrandMark(HDC dc, const RECT &rect, const AppTheme &theme)
        {
            const int w = rect.right - rect.left;
            const int h = rect.bottom - rect.top;
            const int s = std::min(w, h);
            if (s <= 0)
            {
                return;
            }
            const int ox = rect.left + (w - s) / 2;
            const int oy = rect.top + (h - s) / 2;

            const COLORREF blue = RGB(67, 137, 216);
            const COLORREF front = RGB(250, 251, 253);

            // Fractions match the 256 px source artwork in scripts/make-icon.ps1.
            auto square = [&](double fx1, double fy1, double fx2, double fy2, double fr,
                              COLORREF fill, COLORREF border)
            {
                RECT r{ox + static_cast<int>(fx1 * s), oy + static_cast<int>(fy1 * s),
                       ox + static_cast<int>(fx2 * s), oy + static_cast<int>(fy2 * s)};
                const int radius = std::max(2, static_cast<int>(fr * s));
                Draw::RoundRect(dc, r, radius, fill, border, 1);
            };
            square(0.133, 0.211, 0.867, 0.867, 0.086, blue, blue);  // back square
            square(0.250, 0.133, 0.984, 0.766, 0.086, front, theme.Tokens().border); // front square
            square(0.641, 0.641, 0.758, 0.758, 0.031, blue, blue);  // accent dot
        }

        void Divider(HDC dc, const RECT &rect, const AppTheme &theme)
        {
            Draw::Fill(dc, rect, theme.Tokens().border);
        }

        RECT Button(HDC dc, const RECT &rect, const AppTheme &theme, const std::wstring &label, HFONT font, bool hovered, bool selected, bool accent)
        {
            const auto &t = theme.Tokens();
            COLORREF fill;
            COLORREF textColor;
            COLORREF border = t.border;
            if (accent)
            {
                fill = hovered ? t.accentHover : (selected ? t.accentActive : t.accent);
                textColor = t.textOnAccent;
                border = fill;
            }
            else
            {
                fill = hovered ? t.hover : (selected ? t.selected : SurfaceOf(t));
                textColor = t.text;
            }
            Draw::RoundRect(dc, rect, theme.Px(6), fill, border, 1);
            Draw::Text(dc, label, rect, font ? font : BodyFontOf(theme), textColor,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            return rect;
        }

        RECT IconButton(HDC dc, const RECT &rect, const AppTheme &theme, wchar_t glyph, HFONT iconFont, bool hovered, bool selected, bool accent)
        {
            const auto &t = theme.Tokens();
            COLORREF fill = hovered ? t.hover : (selected ? t.selected : t.surface);
            COLORREF iconColor = hovered ? t.text : t.textDim;
            if (accent)
            {
                fill = hovered ? t.accentHover : (selected ? t.accentActive : t.accent);
                iconColor = t.textOnAccent;
            }
            Draw::RoundRect(dc, rect, theme.Px(5), fill, hovered || accent ? fill : t.border, 1);
            Draw::Icon(dc, glyph, rect, iconFont ? iconFont : theme.IconFont(), iconColor);
            return rect;
        }

        RECT Toggle(HDC dc, const RECT &rect, const AppTheme &theme, bool on, bool hovered)
        {
            const auto &t = theme.Tokens();
            const int h = rect.bottom - rect.top;
            const int r = h / 2;
            const COLORREF track = on ? (hovered ? t.accentHover : t.accent) : (hovered ? t.hover : t.border);
            Draw::RoundRect(dc, rect, r, track, track, 1);

            // Knob: a compact circle inset from the track so the track stays readable.
            const int inset = theme.Px(3);
            const int knobD = h - inset * 2;
            const int knobX = on ? rect.right - inset - knobD : rect.left + inset;
            const RECT knob{knobX, rect.top + inset, knobX + knobD, rect.bottom - inset};
            Draw::RoundRect(dc, knob, knobD / 2, RGB(255, 255, 255), RGB(255, 255, 255), 1);
            return rect;
        }

        RECT Badge(HDC dc, const RECT &rect, const AppTheme &theme, const std::wstring &label, HFONT font, COLORREF fill, COLORREF text)
        {
            Draw::RoundRect(dc, rect, 5, fill, fill, 1);
            Draw::Text(dc, label, rect, font, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return rect;
        }

        void SectionHeader(HDC dc, const RECT &rect, const AppTheme &theme, const std::wstring &heading, const std::wstring &description)
        {
            const auto &t = theme.Tokens();
            Draw::Text(dc, heading, {rect.left, rect.top, rect.right, rect.top + 20}, theme.HeadingFont(), t.text, DT_LEFT | DT_TOP | DT_SINGLELINE);
            if (!description.empty())
            {
                Draw::Text(dc, description, {rect.left, rect.top + 20, rect.right, rect.top + 36}, theme.SmallFont(), t.textDim, DT_LEFT | DT_TOP | DT_SINGLELINE);
            }
        }

        void EmptyState(HDC dc, const RECT &rect, const AppTheme &theme, wchar_t glyph, const std::wstring &heading, const std::wstring &description)
        {
            const auto &t = theme.Tokens();
            int cx = rect.left + (rect.right - rect.left) / 2;
            int iconSize = theme.Space32();
            RECT iconRect{cx - iconSize / 2, rect.top, cx + iconSize / 2, rect.top + iconSize};
            Draw::Icon(dc, glyph, iconRect, theme.IconFont(), t.textDim);

            RECT headingRect{rect.left, rect.top + iconSize + theme.Space12(), rect.right, rect.top + iconSize + theme.Space12() + 22};
            Draw::Text(dc, heading, headingRect, theme.HeadingFont(), t.text, DT_CENTER | DT_TOP | DT_SINGLELINE);

            RECT descRect{rect.left + theme.Space24(), rect.top + iconSize + theme.Space12() + 26, rect.right - theme.Space24(), rect.bottom};
            Draw::TextWrapped(dc, description, descRect, theme.SmallFont(), t.textDim);
        }
    } // namespace Components

} // namespace flowspace::ui
