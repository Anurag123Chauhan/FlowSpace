#pragma once

#include "core/Models.h"
#include <Windows.h>
#include <string>
#include <vector>

namespace flowspace::ui
{
    /// Shows a workspace picker on the target monitor. Users can navigate with the keyboard
    /// or mouse; the window belongs to FlowSpace and is not managed as a workspace window.
    class SwitcherPopup
    {
      public:
        struct Item
        {
            core::Workspace workspace;
            int number;  // 1-based number, 0 when not addressable
            bool active; // active on the target monitor
        };

        // Returns the selected workspace number, or 0 when the popup is dismissed.
        static int Show(HINSTANCE instance, const core::MonitorInfo &monitor, std::vector<Item> items);
    };

    // Register the popup window class before creating the picker.
    void RegisterSwitcherClass(HINSTANCE instance);
}
