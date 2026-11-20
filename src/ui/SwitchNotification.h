#pragma once

#include "core/Models.h"
#include <Windows.h>
#include <string>

namespace flowspace::ui
{
    /// Briefly shows the newly selected workspace on the correct monitor.
    /// The notification does not take focus or receive input.
    class SwitchNotification
    {
      public:
        static void Show(HINSTANCE instance, const core::MonitorInfo &monitor, const core::Workspace &workspace,
                         int workspaceNumber, const std::string &animationStyle, int animationMs);
    };

    void RegisterSwitchNotificationClass(HINSTANCE instance);
}
