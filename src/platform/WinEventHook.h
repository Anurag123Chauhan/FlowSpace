#pragma once

#include <Windows.h>
#include <functional>

namespace flowspace::platform
{
    /// Watches window creation, visibility, movement, and focus changes.
    /// Windows delivers callbacks through the app's message loop on its main thread.
    class WinEventHook
    {
      public:
        using Handler = std::function<void(DWORD, HWND, LONG, LONG)>;

        // Installs hooks; returns false when none could be installed. idEventThread must be
        // the thread that owns the message loop (0 = current thread).
        bool Install(DWORD idEventThread, Handler handler);
        void Uninstall();

      private:
        static void CALLBACK StaticCallback(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
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
