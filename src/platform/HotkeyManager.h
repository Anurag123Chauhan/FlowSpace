#pragma once

#include "core/ConfigService.h"
#include <Windows.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace flowspace::platform
{
    /// Registers global shortcuts and reports conflicts to the settings UI.
    /// Number-based actions register one shortcut for each number key.
    class HotkeyManager
    {
      public:
        // idBase..idBase+N-1 are reserved for hotkeys; ids are unique per message window.
        explicit HotkeyManager(int idBase = 1) : idBase_(idBase) {}

        void UnregisterAll();

        /// Registers enabled shortcuts and returns any conflicts.
        /// Conflicting shortcuts are left unregistered.
        std::vector<std::wstring> RegisterAll(HWND messageWindow, const std::vector<core::HotkeyBinding> &bindings);

        // Resolves a WM_HOTKEY id back to (action, numericIndex 1..9 or 0 for non-numeric).
        std::optional<std::pair<core::HotkeyAction, int>> Resolve(int hotkeyId) const;

        static std::wstring Describe(const core::HotkeyBinding &binding);

        // Modifier parsing shared with the shortcut capture UI. Returns MOD_* mask;
        // sets outUnsupported when the text contains unparseable parts.
        static UINT ParseModifiers(const std::wstring &modifiers);

        static UINT ParseKey(const std::wstring &keyName);
        static std::wstring KeyName(UINT vk);

      private:
        struct Registration
        {
            core::HotkeyAction action;
            int numericIndex; // 1..9 for numeric actions, 0 otherwise
        };

        int idBase_;
        HWND window_ = nullptr;
        int nextId_ = 0;
        std::unordered_map<int, Registration> byId_;
        std::vector<int> registeredIds_;
    };
}
