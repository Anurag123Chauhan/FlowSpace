#pragma once

#include "core/Models.h"
#include <Windows.h>
#include <string>
#include <vector>

namespace flowspace::ui
{
    /// <summary>
    /// The visual workspace switcher: a topmost popup listing all workspaces with numbers,
    /// icons and accent colors. Click or Enter switches the chosen workspace on the monitor
    /// the popup was opened for. Digits 1-9 select directly; Escape closes. The popup is
    /// excluded from workspace management (owned by the FlowSpace process).
    /// </summary>
    class SwitcherPopup
    {
    public:
        struct Item
        {
            core::Workspace workspace;
            int number;      // 1-based number, 0 when not addressable
            bool active;     // active on the target monitor
        };

        // Shows the popup; returns after the user selects (id 1..N) or cancels (0).
        // static so no persistent object is needed.
        static int Show(HINSTANCE instance, const core::MonitorInfo &monitor, std::vector<Item> items);
    };

    // Registers the popup window class once (called from the app entry point).
    void RegisterSwitcherClass(HINSTANCE instance);
}
