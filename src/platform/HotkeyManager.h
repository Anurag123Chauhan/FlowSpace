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
    /// <summary>
    /// Registers configurable global hotkeys on a hidden message window. Each logical action
    /// expands into one or more Win32 hotkey registrations (numeric actions expand to nine).
    /// Registration failures and conflicts between configured bindings are reported so the UI
    /// can explain them; a conflicting binding never silently overrides another.
    /// </summary>
    class HotkeyManager
    {
    public:
        // idBase..idBase+N-1 are reserved for hotkeys; ids are unique per message window.
        explicit HotkeyManager(int idBase = 1) : idBase_(idBase) {}

        // Unregisters everything currently registered.
        void UnregisterAll();

        /// <summary>
        /// Registers all enabled bindings. Returns the list of conflicts detected
        /// (two bindings expanding to the same modifier+key combination). A conflict is
        /// registered for neither binding.
        /// </summary>
        std::vector<std::wstring> RegisterAll(HWND messageWindow, const std::vector<core::HotkeyBinding> &bindings);

        // Resolves a WM_HOTKEY id back to (action, numericIndex 1..9 or 0 for non-numeric).
        std::optional<std::pair<core::HotkeyAction, int>> Resolve(int hotkeyId) const;

        // Human-readable text for a binding, e.g. "Ctrl+Alt+Left".
        static std::wstring Describe(const core::HotkeyBinding &binding);

        // Modifier parsing shared with the shortcut capture UI. Returns MOD_* mask;
        // sets outUnsupported when the text contains unparseable parts.
        static UINT ParseModifiers(const std::wstring &modifiers);

        // Virtual key name mapping shared with the shortcut capture UI.
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
