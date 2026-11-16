#pragma once

#include "core/Models.h"
#include <Windows.h>
#include <string>

namespace flowspace::ui
{
    /// <summary>
    /// The workspace switch overlay: a frameless, topmost, non-activating layered window shown
    /// briefly on the monitor being switched. Uses UpdateLayeredWindow with per-pixel alpha so
    /// the rounded card floats over the desktop without a window frame.
    ///
    /// The overlay is owned by the FlowSpace process, which the engine's manageability filter
    /// excludes — overlays never become managed application windows.
    /// Animations: slide (horizontal entrance), fade (opacity ramp), scale (size ramp), none.
    /// </summary>
    class OverlayWindow
    {
    public:
        static void RegisterOverlayClass(HINSTANCE instance);

        // Shows the overlay on the given monitor and returns immediately; the window closes
        // itself via a timer.
        static void Show(HINSTANCE instance, const core::MonitorInfo &monitor, const core::Workspace &workspace,
                         int workspaceNumber, const std::string &animationStyle, int animationMs);
    };
}
