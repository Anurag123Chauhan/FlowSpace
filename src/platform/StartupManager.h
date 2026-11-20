#pragma once

#include <Windows.h>
#include <string>

namespace flowspace::platform
{
    /// Adds or removes FlowSpace from the current user's startup apps.
    /// This changes the user's registry settings and does not require admin rights.
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
