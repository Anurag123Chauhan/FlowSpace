#include "platform/StartupManager.h"

#include <spdlog/spdlog.h>
#include <vector>

namespace flowspace::platform
{
    bool StartupManager::IsRegistered() const
    {
        return !GetCommand().empty();
    }

    std::wstring StartupManager::GetCommand() const
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        {
            return {};
        }
        DWORD type = 0;
        DWORD size = 0;
        if (RegQueryValueExW(key, valueName_.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
            type != REG_SZ)
        {
            RegCloseKey(key);
            return {};
        }
        std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(key, valueName_.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer.data()),
                             &size) != ERROR_SUCCESS)
        {
            RegCloseKey(key);
            return {};
        }
        RegCloseKey(key);
        return std::wstring(buffer.data());
    }

    bool StartupManager::Enable()
    {
        wchar_t exePath[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
        {
            spdlog::error("Startup: GetModuleFileNameW failed ({})", GetLastError());
            return false;
        }
        std::wstring command = L"\"" + std::wstring(exePath) + L"\"";

        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        {
            spdlog::error("Startup: cannot open HKCU Run key ({})", GetLastError());
            return false;
        }
        const LRESULT result =
            RegSetValueExW(key, valueName_.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(command.c_str()),
                           static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        if (result != ERROR_SUCCESS)
        {
            spdlog::error("Startup: RegSetValueExW failed ({})", result);
            return false;
        }
        return true;
    }

    bool StartupManager::Disable()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        const LRESULT result = RegDeleteValueW(key, valueName_.c_str());
        RegCloseKey(key);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }
}
