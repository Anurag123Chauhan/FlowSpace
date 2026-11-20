#include "platform/WinEventHook.h"

#include <spdlog/spdlog.h>
#include <objbase.h>

namespace flowspace::platform
{
    namespace
    {
        WinEventHook *g_instance = nullptr; // hooks run on the main thread only

        // Only OBJID_WINDOW events for top-level windows are interesting; window-object
        // filters keep the callback volume manageable.
        bool IsRelevantEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild)
        {
            if (idObject != OBJID_WINDOW || idChild != 0)
            {
                return false;
            }
            switch (event)
            {
                case EVENT_OBJECT_CREATE:
                case EVENT_OBJECT_SHOW:
                case EVENT_OBJECT_HIDE:
                case EVENT_OBJECT_DESTROY:
                case EVENT_SYSTEM_MOVESIZEEND:
                case EVENT_SYSTEM_FOREGROUND:
                    return hwnd != nullptr;
                default:
                    return false;
            }
        }
    }

    void CALLBACK WinEventHook::StaticCallback(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                               DWORD, DWORD)
    {
        if (g_instance && g_instance->handler_ && IsRelevantEvent(event, hwnd, idObject, idChild))
        {
            g_instance->handler_(event, hwnd, idObject, idChild);
        }
    }

    bool WinEventHook::Install(DWORD idEventThread, Handler handler)
    {
        handler_ = std::move(handler);
        g_instance = this;

        const DWORD flags = WINEVENT_OUTOFCONTEXT; // callbacks delivered via the message loop
        hCreate_ = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr, &StaticCallback, 0, idEventThread,
                                   flags);
        hShow_ =
            SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, &StaticCallback, 0, idEventThread, flags);
        hHide_ =
            SetWinEventHook(EVENT_OBJECT_HIDE, EVENT_OBJECT_HIDE, nullptr, &StaticCallback, 0, idEventThread, flags);
        hDestroy_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, nullptr, &StaticCallback, 0,
                                    idEventThread, flags);
        hMoveSizeEnd_ = SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, nullptr, &StaticCallback, 0,
                                        idEventThread, flags);
        hForeground_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &StaticCallback, 0,
                                       idEventThread, flags);

        const bool any = hCreate_ || hShow_ || hHide_ || hDestroy_ || hMoveSizeEnd_ || hForeground_;
        if (!any)
        {
            spdlog::error("SetWinEventHook failed for all events (GetLastError={})", GetLastError());
        }
        else
        {
            spdlog::info("WinEvent hooks installed (create={} show={} hide={} destroy={} moveend={} fg={})",
                         hCreate_ != nullptr, hShow_ != nullptr, hHide_ != nullptr, hDestroy_ != nullptr,
                         hMoveSizeEnd_ != nullptr, hForeground_ != nullptr);
        }
        return any;
    }

    void WinEventHook::Uninstall()
    {
        auto un = [](HWINEVENTHOOK &h)
        {
            if (h)
            {
                UnhookWinEvent(h);
                h = nullptr;
            }
        };
        un(hCreate_);
        un(hShow_);
        un(hHide_);
        un(hDestroy_);
        un(hMoveSizeEnd_);
        un(hForeground_);
        g_instance = nullptr;
    }
}
