#include "ui/TrayIcon.h"
#include "ui/Theme.h"

#include <spdlog/spdlog.h>
#include <memory>

namespace flowspace::ui
{
    namespace
    {
        HICON CreateTrayIcon(HDC screenDc)
        {
            // Draws a simple layered-square mark (two rounded rects, accent + white).
            const int size = GetSystemMetrics(SM_CXSMICON);
            auto *bits = static_cast<uint32_t *>(nullptr);
            BITMAPV5HEADER bi{};
            bi.bV5Size = sizeof(bi);
            bi.bV5Width = size;
            bi.bV5Height = -size;
            bi.bV5Planes = 1;
            bi.bV5BitCount = 32;
            bi.bV5Compression = BI_BITFIELDS;
            bi.bV5RedMask = 0x00FF0000;
            bi.bV5GreenMask = 0x0000FF00;
            bi.bV5BlueMask = 0x000000FF;
            bi.bV5AlphaMask = 0xFF000000;

            void *data = nullptr;
            HBITMAP bmp =
                CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS, &data, nullptr, 0);
            if (!bmp || !data)
            {
                return LoadIcon(nullptr, IDI_APPLICATION);
            }
            bits = static_cast<uint32_t *>(data);

            auto put = [&](int x, int y, uint32_t color)
            {
                bits[y * size + x] = color;
            };

            const uint32_t accent = 0xFFA87E5A;
            const uint32_t white = 0xFFFFFFFF;
            const int inset = size / 8;
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    if (x >= inset && x < size - inset && y >= inset && y < size - inset)
                    {
                        put(x, y, accent);
                    }
                    const int f = inset + size / 6;
                    if (x >= f && x < size - inset && y >= 0 + f - inset && y < size - inset - size / 6)
                    {
                        put(x, y, white);
                    }
                }
            }

            ICONINFO info{};
            info.fIcon = TRUE;
            info.hbmColor = bmp;
            info.hbmMask = CreateBitmap(size, size, 1, 1, nullptr);
            HICON icon = CreateIconIndirect(&info);
            DeleteObject(info.hbmMask);
            DeleteObject(bmp);
            return icon ? icon : LoadIcon(nullptr, IDI_APPLICATION);
        }
    }

    void TrayIcon::Initialize(HWND messageWindow, UINT callbackMessage, UINT menuIdBase)
    {
        window_ = messageWindow;
        callbackMessage_ = callbackMessage;
        menuIdBase_ = menuIdBase;
    }

    bool TrayIcon::Update(bool paused)
    {
        if (!window_)
        {
            return false;
        }
        paused_ = paused;

        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = 1;
        data.uFlags = NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = callbackMessage_;
        wcscpy_s(data.szTip, paused ? L"FlowSpace (paused)" : L"FlowSpace");
        data.dwState = 0;
        data.dwStateMask = 0;

        HDC screenDc = GetDC(nullptr);
        HICON icon = CreateTrayIcon(screenDc);
        ReleaseDC(nullptr, screenDc);
        data.hIcon = icon;
        data.uFlags |= NIF_ICON;

        BOOL ok;
        if (added_)
        {
            ok = Shell_NotifyIconW(NIM_MODIFY, &data);
        }
        else
        {
            ok = Shell_NotifyIconW(NIM_ADD, &data);
            if (ok)
            {
                added_ = true;
            }
        }
        if (icon)
        {
            DestroyIcon(icon);
        }
        return ok != FALSE;
    }

    void TrayIcon::Shutdown()
    {
        if (added_)
        {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = window_;
            data.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &data);
            added_ = false;
        }
    }

    int TrayIcon::HandleCallbackMessage(LPARAM lParam)
    {
        if (!window_)
        {
            return 0;
        }
        const WORD event = LOWORD(lParam);

        // Right-click (legacy shell behavior) or the modern context menu event.
        if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT)
        {
            HMENU menu = CreatePopupMenu();
            if (!menu)
            {
                return 0;
            }

            AppendMenuW(menu, MF_STRING, menuIdBase_ + 0, L"Open Settings");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            if (workspaceMenuBuilder_)
            {
                workspaceMenuBuilder_(menu);
            }
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, menuIdBase_ + 1, L"Reveal All Windows");
            AppendMenuW(menu, MF_STRING | (paused_ ? MF_CHECKED : 0), menuIdBase_ + 2, L"Pause Workspace Management");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, menuIdBase_ + 3, L"Exit FlowSpace");

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(window_); // required for correct menu dismissal
            const int cmd =
                TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, window_, nullptr);
            DestroyMenu(menu);
            return cmd;
        }

        if (event == WM_LBUTTONUP)
        {
            return -1; // plain left click: open settings
        }
        return 0;
    }
}
