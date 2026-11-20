#pragma once

#include <cstdint>
#include <string>

namespace flowspace::core
{
    // Shared UTF-8 <-> UTF-16 conversion helpers (was previously duplicated in main.cpp).
    std::string WideToUtf8(const std::wstring &wide);
    std::wstring Utf8ToWide(const std::string &utf8);

    // Simple hex color helpers. Colors are stored as strings like "#4389D8" or "#FF4389D8".
    // Parsing failures return false and leave outValue untouched.
    bool TryParseHexColor(const std::wstring &text, uint32_t &outValue);  // 0x00RRGGBB
    bool TryParseHexColor8(const std::wstring &text, uint32_t &outValue); // 0xAARRGGBB
    std::wstring FormatHexColor(uint32_t rgb);                            // "#RRGGBB"
    std::wstring FormatHexColor8(uint32_t argb);                          // "#AARRGGBB"
}
