#pragma once

#include "core/Models.h"
#include <Windows.h>
#include <string>

namespace flowspace::ui
{
    /// <summary>
    /// Temporary, non-activating notification shown after a workspace switch. Displays only
    /// the newly selected workspace, anchored bottom-center on the correct monitor, and
    /// dismisses itself after ~800ms.
    /// Uses a layered cached DIB for the translucent rounded card and its fade/scale animation.
    /// It never receives keyboard or mouse input.
    /// </summary>
    class SwitchNotification
    {
    public:
        static void Show(HINSTANCE instance, const core::MonitorInfo &monitor,
                         const core::Workspace &workspace, int workspaceNumber,
                         const std::string &animationStyle, int animationMs);
    };

    // Registers the notification window class once at startup.
    void RegisterSwitchNotificationClass(HINSTANCE instance);
}
