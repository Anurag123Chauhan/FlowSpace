#include "core/StringUtils.h"

#include <windows.h>
#include <algorithm>
#include <cctype>

namespace flowspace::core
{
    std::string WideToUtf8(const std::wstring &wide)
    {
        if (wide.empty())
        {
            return {};
        }
        const int sizeNeeded = WideCharToMultiByte(
            CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (sizeNeeded <= 0)
        {
            return {};
        }
        std::string utf8(static_cast<size_t>(sizeNeeded), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), sizeNeeded, nullptr, nullptr);
        return utf8;
    }

    std::wstring Utf8ToWide(const std::string &utf8)
    {
        if (utf8.empty())
        {
            return {};
        }
        const int sizeNeeded = MultiByteToWideChar(
            CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (sizeNeeded <= 0)
        {
            return {};
        }
        std::wstring wide(static_cast<size_t>(sizeNeeded), L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), sizeNeeded);
        return wide;
    }

    namespace
    {
        int HexValue(wchar_t c)
        {
            if (c >= L'0' && c <= L'9')
            {
                return c - L'0';
            }
            if (c >= L'a' && c <= L'f')
            {
                return c - L'a' + 10;
            }
            if (c >= L'A' && c <= L'F')
            {
                return c - L'A' + 10;
            }
            return -1;
        }
    }

    bool TryParseHexColor(const std::wstring &text, uint32_t &outValue)
    {
        std::wstring t = text;
        if (!t.empty() && t.front() == L'#')
        {
            t.erase(t.begin());
        }
        if (t.size() == 8)
        {
            // Drop the alpha channel for RGB parsing.
            t = t.substr(2);
        }
        if (t.size() != 6)
        {
            return false;
        }
        uint32_t value = 0;
        for (wchar_t c : t)
        {
            const int v = HexValue(c);
            if (v < 0)
            {
                return false;
            }
            value = (value << 4) | static_cast<uint32_t>(v);
        }
        outValue = value;
        return true;
    }

    bool TryParseHexColor8(const std::wstring &text, uint32_t &outValue)
    {
        std::wstring t = text;
        if (!t.empty() && t.front() == L'#')
        {
            t.erase(t.begin());
        }
        if (t.size() == 6)
        {
            t = L"FF" + t; // Default to fully opaque.
        }
        if (t.size() != 8)
        {
            return false;
        }
        uint32_t value = 0;
        for (wchar_t c : t)
        {
            const int v = HexValue(c);
            if (v < 0)
            {
                return false;
            }
            value = (value << 4) | static_cast<uint32_t>(v);
        }
        outValue = value;
        return true;
    }

    std::wstring FormatHexColor(uint32_t rgb)
    {
        wchar_t buf[16];
        swprintf_s(buf, L"#%06X", rgb & 0xFFFFFFu);
        return buf;
    }

    std::wstring FormatHexColor8(uint32_t argb)
    {
        wchar_t buf[16];
        swprintf_s(buf, L"#%08X", argb & 0xFFFFFFFFu);
        return buf;
    }
}
