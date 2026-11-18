#pragma once

#include "core/Models.h"
#include <Windows.h>
#include <string>
#include <vector>

namespace flowspace::ui
{
    /// <summary>
    /// Manual workspace selector: a compact, translucent Windows 11-style panel centered on
    /// the monitor it was opened for. Each row shows a monitor icon, the workspace name and
    /// its number; the active row is accented. Click/Enter selects, digits 1-9 select directly,
    /// Escape or clicking outside dismisses. Owned by the FlowSpace process, so it is excluded
    /// from workspace management.
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
