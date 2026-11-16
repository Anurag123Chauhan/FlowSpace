#pragma once

#include <Windows.h>
#include <string>

namespace flowspace::platform
{
    /// <summary>
    /// Per-user "start with Windows" integration via the HKCU Run key. No administrator
    /// rights required: the value lives in the logged-in user's hive.
    /// </summary>
    class StartupManager
    {
    public:
        explicit StartupManager(std::wstring valueName = L"FlowSpace") : valueName_(std::move(valueName)) {}

        // Reads the current registration. Returns false when not registered.
        bool IsRegistered() const;
        std::wstring GetCommand() const;

        // Registers (or updates) the Run entry with the current executable path, quoted.
        bool Enable();
        bool Disable();

    private:
        std::wstring valueName_;
        static constexpr const wchar_t *kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    };
}
