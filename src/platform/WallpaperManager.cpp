#include "platform/WallpaperManager.h"

#include <spdlog/spdlog.h>
#include <objbase.h>
#include <string>

namespace flowspace::platform
{
    namespace
    {
        // Verified against ShObjIdl.idl (Windows 10 SDK, um include directory).
        // {B92B56A9-8B55-4E14-9A89-0199BBB6F93B}
        const GUID kIID_IDesktopWallpaper = {
            0xB92B56A9, 0x8B55, 0x4E14, {0x9A, 0x89, 0x01, 0x99, 0xBB, 0xB6, 0xF9, 0x3B}};
        // {C2CF3110-460E-4fc1-B9D0-8A1C0C9CC4BD}
        const GUID kCLSID_DesktopWallpaper = {
            0xC2CF3110, 0x460E, 0x4fc1, {0xB9, 0xD0, 0x8A, 0x1C, 0x0C, 0x9C, 0xC4, 0xBD}};

        // Reads a CoTaskMem-allocated LPWSTR out-param and frees it.
        std::wstring ReadCoString(LPWSTR owned)
        {
            std::wstring result;
            if (owned)
            {
                result = owned;
                CoTaskMemFree(owned);
            }
            return result;
        }
    }

    WallpaperManager::WallpaperManager()
    {
        // The caller is expected to have initialized COM (STA on the UI thread).
        HRESULT hr = CoCreateInstance(kCLSID_DesktopWallpaper, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                      kIID_IDesktopWallpaper, reinterpret_cast<void **>(&wallpaper_));
        if (FAILED(hr) || !wallpaper_)
        {
            // Some builds register the coclass only under the WOW64 hive; retry without
            // restricting activation context.
            hr = CoCreateInstance(kCLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, kIID_IDesktopWallpaper,
                                  reinterpret_cast<void **>(&wallpaper_));
        }
        if (FAILED(hr) || !wallpaper_)
        {
            spdlog::error("IDesktopWallpaper COM object unavailable (hr=0x{:08X}). Wallpaper features disabled.",
                          static_cast<uint32_t>(hr));
            wallpaper_ = nullptr;
        }
        else
        {
            spdlog::info("IDesktopWallpaper COM object created. Wallpaper management available.");
        }
    }

    WallpaperManager::~WallpaperManager()
    {
        if (wallpaper_)
        {
            wallpaper_->Release();
            wallpaper_ = nullptr;
        }
    }

    UINT WallpaperManager::GetMonitorDevicePathCount()
    {
        if (!wallpaper_)
        {
            return 0;
        }
        UINT count = 0;
        const HRESULT hr = wallpaper_->GetMonitorDevicePathCount(&count);
        if (FAILED(hr))
        {
            spdlog::warn("GetMonitorDevicePathCount failed (hr=0x{:08X})", static_cast<uint32_t>(hr));
            return 0;
        }
        return count;
    }

    std::wstring WallpaperManager::GetMonitorDevicePathAt(UINT index)
    {
        if (!wallpaper_)
        {
            return {};
        }
        LPWSTR path = nullptr;
        const HRESULT hr = wallpaper_->GetMonitorDevicePathAt(index, &path);
        if (FAILED(hr))
        {
            spdlog::debug("GetMonitorDevicePathAt({}) failed (hr=0x{:08X})", index, static_cast<uint32_t>(hr));
            return {};
        }
        return ReadCoString(path);
    }

    std::vector<std::wstring> WallpaperManager::GetMonitorDevicePaths()
    {
        std::vector<std::wstring> result;
        const UINT count = GetMonitorDevicePathCount();
        result.reserve(count);
        for (UINT i = 0; i < count; ++i)
        {
            auto path = GetMonitorDevicePathAt(i);
            if (!path.empty())
            {
                result.push_back(std::move(path));
            }
        }
        return result;
    }

    bool WallpaperManager::GetWallpaper(const std::wstring &monitorDevicePath, std::wstring &outPath, HRESULT *hrOut)
    {
        outPath.clear();
        if (!wallpaper_)
        {
            return false;
        }
        LPWSTR path = nullptr;
        const HRESULT hr = wallpaper_->GetWallpaper(monitorDevicePath.c_str(), &path);
        if (hrOut)
        {
            *hrOut = hr;
        }
        if (FAILED(hr))
        {
            return false;
        }
        outPath = ReadCoString(path);
        return true;
    }

    bool WallpaperManager::SetWallpaper(const std::wstring &monitorDevicePath, const std::wstring &imagePath)
    {
        if (!wallpaper_)
        {
            return false;
        }
        // Intentionally never passes nullptr for monitorID: a null id would apply the image to
        // all monitors, defeating per-monitor independence.
        if (monitorDevicePath.empty())
        {
            spdlog::warn("SetWallpaper called with an empty monitor id; ignoring to protect other monitors.");
            return false;
        }
        const HRESULT hr = wallpaper_->SetWallpaper(monitorDevicePath.c_str(), imagePath.c_str());
        if (FAILED(hr))
        {
            spdlog::error("SetWallpaper('{}','{}') failed (hr=0x{:08X})",
                          std::string(monitorDevicePath.begin(), monitorDevicePath.end()),
                          std::string(imagePath.begin(), imagePath.end()), static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    }

    bool WallpaperManager::SetPosition(WallpaperPosition position)
    {
        if (!wallpaper_)
        {
            return false;
        }
        const HRESULT hr = wallpaper_->SetPosition(static_cast<int>(position));
        return SUCCEEDED(hr);
    }

    WallpaperPosition WallpaperManager::GetPosition()
    {
        if (!wallpaper_)
        {
            return WallpaperPosition::Fill;
        }
        int position = static_cast<int>(WallpaperPosition::Fill);
        if (SUCCEEDED(wallpaper_->GetPosition(&position)))
        {
            return static_cast<WallpaperPosition>(position);
        }
        return WallpaperPosition::Fill;
    }

    bool WallpaperManager::SetBackgroundColor(COLORREF color)
    {
        if (!wallpaper_)
        {
            return false;
        }
        return SUCCEEDED(wallpaper_->SetBackgroundColor(color));
    }

    bool WallpaperManager::GetBackgroundColor(COLORREF &out)
    {
        if (!wallpaper_)
        {
            return false;
        }
        return SUCCEEDED(wallpaper_->GetBackgroundColor(&out));
    }

    bool WallpaperManager::IsMonitorAttached(const std::wstring &monitorDevicePath)
    {
        if (!wallpaper_ || monitorDevicePath.empty())
        {
            return false;
        }
        RECT rect{};
        const HRESULT hr = wallpaper_->GetMonitorRECT(monitorDevicePath.c_str(), &rect);
        // S_FALSE with an empty RECT means the monitor is detached.
        return hr == S_OK;
    }

    std::vector<CapturedWallpaper> WallpaperManager::CaptureAll()
    {
        std::vector<CapturedWallpaper> captured;
        if (!wallpaper_)
        {
            return captured;
        }

        COLORREF background = RGB(0, 0, 0);
        (void)GetBackgroundColor(background);
        const WallpaperPosition position = GetPosition();

        for (const auto &path : GetMonitorDevicePaths())
        {
            CapturedWallpaper entry;
            entry.monitorDevicePath = path;
            entry.backgroundColor = background;
            entry.position = position;
            std::wstring image;
            if (GetWallpaper(path, image))
            {
                entry.imagePath = image;
            }
            captured.push_back(std::move(entry));
        }
        return captured;
    }

    void WallpaperManager::RestoreAll(const std::vector<CapturedWallpaper> &captured)
    {
        for (const auto &entry : captured)
        {
            if (entry.monitorDevicePath.empty())
            {
                continue;
            }
            // Skip monitors that are no longer attached: restoring them is meaningless and
            // SetWallpaper would silently record them for a future reattach.
            if (!IsMonitorAttached(entry.monitorDevicePath))
            {
                spdlog::info("RestoreAll: skipping detached monitor '{}'",
                             std::string(entry.monitorDevicePath.begin(), entry.monitorDevicePath.end()));
                continue;
            }

            HRESULT hr = wallpaper_->SetWallpaper(entry.monitorDevicePath.c_str(), entry.imagePath.c_str());
            if (FAILED(hr))
            {
                spdlog::warn("RestoreAll: SetWallpaper failed for '{}' (hr=0x{:08X})",
                             std::string(entry.monitorDevicePath.begin(), entry.monitorDevicePath.end()),
                             static_cast<uint32_t>(hr));
                continue;
            }
            (void)SetBackgroundColor(entry.backgroundColor);
            (void)SetPosition(entry.position);
        }
    }
}
