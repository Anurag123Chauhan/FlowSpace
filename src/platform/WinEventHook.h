#pragma once

#include <Windows.h>
#include <functional>

namespace flowspace::platform
{
    /// <summary>
    /// Installs WinEvent hooks (out-of-context is NOT used: the hooks are installed on the
    /// main thread, so callbacks arrive through the message loop and are serialized with all
    /// other engine work by construction). Events tracked:
    ///   EVENT_OBJECT_CREATE / SHOW / HIDE / DESTROY, EVENT_SYSTEM_MOVESIZEEND,
    ///   EVENT_SYSTEM_FOREGROUND.
    /// </summary>
    class WinEventHook
    {
    public:
        // Callback signature: (event, hwnd, idObject, idChild)
        using Handler = std::function<void(DWORD, HWND, LONG, LONG)>;

        // Installs hooks; returns false when none could be installed. idEventThread must be
        // the thread that owns the message loop (0 = current thread).
        bool Install(DWORD idEventThread, Handler handler);
        void Uninstall();

    private:
        static void CALLBACK StaticCallback(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                            LONG idObject, LONG idChild,
                                            DWORD idEventThread, DWORD dwmsEventTime);

        HWINEVENTHOOK hCreate_ = nullptr;
        HWINEVENTHOOK hShow_ = nullptr;
        HWINEVENTHOOK hHide_ = nullptr;
        HWINEVENTHOOK hDestroy_ = nullptr;
        HWINEVENTHOOK hMoveSizeEnd_ = nullptr;
        HWINEVENTHOOK hForeground_ = nullptr;
        Handler handler_;
    };
}
