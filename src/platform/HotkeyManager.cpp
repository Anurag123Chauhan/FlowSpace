#include "platform/HotkeyManager.h"
#include "core/StringUtils.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <map>

namespace flowspace::platform
{
    namespace
    {
        struct ModifierPart
        {
            const wchar_t *name;
            UINT mod;
        };
        constexpr ModifierPart kModifiers[] = {
            {L"Ctrl", MOD_CONTROL},
            {L"Control", MOD_CONTROL},
            {L"Alt", MOD_ALT},
            {L"Shift", MOD_SHIFT},
            {L"Win", MOD_WIN},
            {L"Windows", MOD_WIN},
        };

        struct KeyPart
        {
            const wchar_t *name;
            UINT vk;
        };
        // Only keys that make sense as global hotkeys are nameable.
        constexpr KeyPart kKeys[] = {
            {L"Left", VK_LEFT}, {L"Right", VK_RIGHT}, {L"Up", VK_UP}, {L"Down", VK_DOWN},
            {L"Space", VK_SPACE}, {L"Tab", VK_TAB}, {L"Back", VK_BACK},
            {L"D0", '0'}, {L"D1", '1'}, {L"D2", '2'}, {L"D3", '3'}, {L"D4", '4'},
            {L"D5", '5'}, {L"D6", '6'}, {L"D7", '7'}, {L"D8", '8'}, {L"D9", '9'},
            {L"F1", VK_F1}, {L"F2", VK_F2}, {L"F3", VK_F3}, {L"F4", VK_F4},
            {L"F5", VK_F5}, {L"F6", VK_F6}, {L"F7", VK_F7}, {L"F8", VK_F8},
            {L"F9", VK_F9}, {L"F10", VK_F10}, {L"F11", VK_F11}, {L"F12", VK_F12},
            {L"Home", VK_HOME}, {L"End", VK_END}, {L"Insert", VK_INSERT},
            {L"PageUp", VK_PRIOR}, {L"PageDown", VK_NEXT},
            {L"OemComma", VK_OEM_COMMA}, {L"OemPeriod", VK_OEM_PERIOD},
            {L"Minus", VK_OEM_MINUS}, {L"Plus", VK_OEM_PLUS},
            // Letters and common punctuation used by defaults like Ctrl+Alt+O (OpenSettings).
            {L"A", 'A'}, {L"B", 'B'}, {L"C", 'C'}, {L"D", 'D'}, {L"E", 'E'},
            {L"F", 'F'}, {L"G", 'G'}, {L"H", 'H'}, {L"I", 'I'}, {L"J", 'J'},
            {L"K", 'K'}, {L"L", 'L'}, {L"M", 'M'}, {L"N", 'N'}, {L"O", 'O'},
            {L"P", 'P'}, {L"Q", 'Q'}, {L"R", 'R'}, {L"S", 'S'}, {L"T", 'T'},
            {L"U", 'U'}, {L"V", 'V'}, {L"W", 'W'}, {L"X", 'X'}, {L"Y", 'Y'},
            {L"Z", 'Z'},
        };
    }

    void HotkeyManager::UnregisterAll()
    {
        // UnregisterHotKey must run on the thread that registered the hotkeys and reference
        // the same message window, which is captured during RegisterAll.
        if (window_ == nullptr)
        {
            return;
        }
        for (int id : registeredIds_)
        {
            UnregisterHotKey(window_, id);
        }
        registeredIds_.clear();
        byId_.clear();
        nextId_ = 0;
    }

    UINT HotkeyManager::ParseModifiers(const std::wstring &modifiers)
    {
        UINT mask = 0;
        std::wstring text = modifiers;
        std::transform(text.begin(), text.end(), text.begin(), ::towlower);
        size_t start = 0;
        while (start < text.size())
        {
            const size_t plus = text.find(L'+', start);
            const std::wstring part = text.substr(start, (plus == std::wstring::npos ? text.size() : plus) - start);
            start = (plus == std::wstring::npos) ? text.size() : plus + 1;
            for (const auto &m : kModifiers)
            {
                const std::wstring lowerName = [n = m.name]
                {
                    std::wstring s(n);
                    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
                    return s;
                }();
                if (part == lowerName)
                {
                    mask |= m.mod;
                    break;
                }
            }
        }
        return mask;
    }

    UINT HotkeyManager::ParseKey(const std::wstring &keyName)
    {
        for (const auto &k : kKeys)
        {
            if (_wcsicmp(k.name, keyName.c_str()) == 0)
            {
                return k.vk;
            }
        }
        return 0;
    }

    std::wstring HotkeyManager::KeyName(UINT vk)
    {
        for (const auto &k : kKeys)
        {
            if (k.vk == vk)
            {
                return k.name;
            }
        }
        return {};
    }

    std::wstring HotkeyManager::Describe(const core::HotkeyBinding &binding)
    {
        std::wstring result;
        if (!binding.modifiers.empty())
        {
            result = binding.modifiers + L"+";
        }
        // Numeric bindings act as "1".."9".
        std::wstring key = binding.key;
        if (key.size() == 2 && (key[0] == L'D' || key[0] == L'd') && key[1] >= L'1' && key[1] <= L'9')
        {
            key = std::wstring(1, key[1]);
        }
        result += key;
        return result;
    }

    std::vector<std::wstring> HotkeyManager::RegisterAll(HWND messageWindow, const std::vector<core::HotkeyBinding> &bindings)
    {
        UnregisterAll();
        window_ = messageWindow;

        struct Source
        {
            core::HotkeyAction action;
            int numericIndex;
            std::wstring describe;
            bool operator==(const Source &other) const
            {
                return action == other.action && numericIndex == other.numericIndex;
            }
        };

        struct Candidate
        {
            UINT mod;
            UINT vk;
            core::HotkeyAction action;
            int numericIndex;
            std::vector<Source> sources; // logical bindings that want this combo
        };

        std::vector<Candidate> candidates;
        auto findOrAdd = [&](UINT mod, UINT vk) -> Candidate &
        {
            for (auto &c : candidates)
            {
                if (c.mod == mod && c.vk == vk)
                {
                    return c;
                }
            }
            candidates.push_back(Candidate{mod, vk, core::HotkeyAction::PreviousWorkspace, 0, {}});
            return candidates.back();
        };

        // Expand bindings into concrete combinations.
        for (const auto &binding : bindings)
        {
            if (!binding.enabled)
            {
                continue;
            }
            const UINT mod = ParseModifiers(binding.modifiers);
            const bool numeric = binding.action == core::HotkeyAction::SwitchToWorkspaceByNumber
                              || binding.action == core::HotkeyAction::MoveActiveWindowToWorkspaceByNumber;

            if (numeric)
            {
                // Support at least nine directly addressable workspaces through number keys.
                for (int n = 1; n <= 9; ++n)
                {
                    std::wstring keyName = L"D" + std::to_wstring(n);
                    const UINT vk = ParseKey(keyName);
                    if (vk == 0)
                    {
                        continue;
                    }
                    auto &c = findOrAdd(mod, vk);
                    if (c.sources.empty())
                    {
                        c.action = binding.action;
                        c.numericIndex = n;
                    }
                    c.sources.push_back(Source{binding.action, n, Describe(binding) + L"[" + std::to_wstring(n) + L"]"});
                }
            }
            else
            {
                const UINT vk = ParseKey(binding.key);
                if (vk == 0)
                {
                    spdlog::warn("Hotkey with unknown key name '{}' skipped", std::string(binding.key.begin(), binding.key.end()));
                    continue;
                }
                auto &c = findOrAdd(mod, vk);
                if (c.sources.empty())
                {
                    c.action = binding.action;
                    c.numericIndex = 0;
                }
                c.sources.push_back(Source{binding.action, 0, Describe(binding)});
            }
        }

        // Detect conflicts: two different logical bindings want the same combination.
        // Numeric bindings expand to distinct indexes, so identical (action,index) pairs
        // are the same logical binding (not a conflict).
        std::vector<std::wstring> conflicts;
        for (auto &c : candidates)
        {
            bool distinct = false;
            for (size_t i = 1; i < c.sources.size(); ++i)
            {
                if (!(c.sources[i] == c.sources[0]))
                {
                    distinct = true;
                    break;
                }
            }
            if (distinct)
            {
                std::wstring text = L"Conflicting shortcut: " + std::to_wstring(c.sources.size())
                                  + L" bindings want the same key combination. None were registered.";
                conflicts.push_back(text);
                continue;
            }
            const int id = idBase_ + nextId_++;
            if (RegisterHotKey(messageWindow, id, c.mod, c.vk))
            {
                registeredIds_.push_back(id);
                byId_[id] = Registration{c.action, c.numericIndex};
            }
            else
            {
                conflicts.push_back(L"Shortcut registration failed (already used by another program): "
                                    + Describe(core::HotkeyBinding{c.action, true, L"", KeyName(c.vk)}));
            }
        }

        if (!conflicts.empty())
        {
            for (const auto &c : conflicts)
            {
                spdlog::warn("{}", std::string(c.begin(), c.end()));
            }
        }
        return conflicts;
    }

    std::optional<std::pair<core::HotkeyAction, int>> HotkeyManager::Resolve(int hotkeyId) const
    {
        auto it = byId_.find(hotkeyId);
        if (it == byId_.end())
        {
            return std::nullopt;
        }
        return std::make_pair(it->second.action, it->second.numericIndex);
    }
}
