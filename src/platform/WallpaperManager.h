#pragma once

#include <Windows.h>
#include <string>
#include <vector>

namespace flowspace::platform
{
    // Values mirror DESKTOP_WALLPAPER_POSITION from ShObjIdl.idl (verified against the
    // Windows 10 SDK). Kept locally so the core does not depend on shell IDL headers.
    enum class WallpaperPosition : int
    {
        Center = 0,
        Tile = 1,
        Stretch = 2,
        Fit = 3,
        Fill = 4,
        Span = 5,
    };

    // Captured wallpaper state for one monitor, used to restore the user's original setup.
    struct CapturedWallpaper
    {
        std::wstring monitorDevicePath;
        std::wstring imagePath;      // empty = solid background color only
        COLORREF backgroundColor = RGB(0, 0, 0);
        WallpaperPosition position = WallpaperPosition::Fill;
    };

    /// <summary>
    /// Per-monitor wallpaper management through the documented IDesktopWallpaper COM interface
    /// (Windows 8+). Never uses undocumented virtual-desktop interfaces or registry hacks.
    ///
    /// Interop note (verified against ShObjIdl.idl from the Windows 10 SDK, include/um):
    ///   IID_IDesktopWallpaper  {B92B56A9-8B55-4E14-9A89-0199BBB6F93B}
    ///   CLSID_DesktopWallpaper {C2CF3110-460E-4fc1-B9D0-8A1C0C9CC4BD}
    /// </summary>
    class WallpaperManager
    {
    public:
        WallpaperManager();
        ~WallpaperManager();

        WallpaperManager(const WallpaperManager &) = delete;
        WallpaperManager &operator=(const WallpaperManager &) = delete;

        // Returns false when the COM object could not be created (wallpaper management disabled).
        bool IsAvailable() const { return wallpaper_ != nullptr; }

        // Number of monitors known to the wallpaper service. Includes monitors that are
        // currently detached but still have an image assigned.
        UINT GetMonitorDevicePathCount();

        // Stable device path for monitor index i as used by the wallpaper service, e.g.
        // "\\?\DISPLAY#DEL4060#5&10a2bc4d&0&UID4353#{e6f07b5f-...}". Empty on failure.
        std::wstring GetMonitorDevicePathAt(UINT index);

        // Lists all monitor device paths currently known to the wallpaper service.
        std::vector<std::wstring> GetMonitorDevicePaths();

        // Current wallpaper image path for a monitor. Empty = none / solid color.
        // Returns false on COM failure; on missing/invalid monitor the HRESULT is exposed.
        bool GetWallpaper(const std::wstring &monitorDevicePath, std::wstring &outPath, HRESULT *hr = nullptr);

        // Sets the wallpaper image for exactly one monitor. Never passes a null monitor
        // identifier, so other monitors are untouched. Passing an empty path clears the image
        // (the background color remains). Returns false and logs on COM failure.
        bool SetWallpaper(const std::wstring &monitorDevicePath, const std::wstring &imagePath);

        // Position (fit mode) is global for the wallpaper API; applied when setting images.
        bool SetPosition(WallpaperPosition position);
        WallpaperPosition GetPosition();

        // Background color visible when no image is displayed or the image does not fill.
        bool SetBackgroundColor(COLORREF color);
        bool GetBackgroundColor(COLORREF &out);

        // Captures the current wallpaper state of all monitors for later restoration.
        std::vector<CapturedWallpaper> CaptureAll();

        // Restores previously captured state. Monitors that no longer exist are skipped;
        // COM failures are logged but do not abort restoration of the remaining monitors.
        void RestoreAll(const std::vector<CapturedWallpaper> &captured);

        // True when the monitor is attached according to GetMonitorRECT (S_FALSE = detached).
        bool IsMonitorAttached(const std::wstring &monitorDevicePath);

    private:
        // Raw COM interface, declared in vtable order exactly as in ShObjIdl.idl.
        struct IDesktopWallpaperImpl : public IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE SetWallpaper(LPCWSTR monitorID, LPCWSTR wallpaper) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetWallpaper(LPCWSTR monitorID, LPWSTR *wallpaper) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetMonitorDevicePathAt(UINT monitorIndex, LPWSTR *monitorID) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetMonitorDevicePathCount(UINT *count) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetMonitorRECT(LPCWSTR monitorID, RECT *displayRect) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetBackgroundColor(COLORREF color) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetBackgroundColor(COLORREF *color) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetPosition(int position) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetPosition(int *position) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetSlideshow(IUnknown *items) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetSlideshow(IUnknown **items) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetSlideshowOptions(UINT options, UINT slideshowTick) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetSlideshowOptions(UINT *options, UINT *slideshowTick) = 0;
            virtual HRESULT STDMETHODCALLTYPE AdvanceSlideshow(LPCWSTR monitorID, UINT direction) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetStatus(UINT *state) = 0;
            virtual HRESULT STDMETHODCALLTYPE Enable(BOOL enable) = 0;
        };

        IDesktopWallpaperImpl *wallpaper_ = nullptr;
    };
}
