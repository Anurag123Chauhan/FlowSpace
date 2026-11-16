#include "platform/ProcessInfo.h"

#include <array>
#include <filesystem>
#include <memory>
#include <type_traits>

namespace flowspace::platform
{
    namespace
    {
        struct HandleCloser
        {
            void operator()(HANDLE handle) const noexcept
            {
                if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle);
                }
            }
        };
        using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;
    }

    bool ProcessInfo::QueryDetails(
        DWORD pid,
        std::wstring &outFullPath,
        std::wstring &outExeName,
        uint64_t &outCreationTime)
    {
        outFullPath.clear();
        outExeName.clear();
        outCreationTime = 0;

        if (pid == 0)
        {
            return false;
        }

        UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (!process)
        {
            return false;
        }

        std::array<wchar_t, MAX_PATH * 2> pathBuffer{};
        DWORD size = static_cast<DWORD>(pathBuffer.size());
        if (QueryFullProcessImageNameW(process.get(), 0, pathBuffer.data(), &size))
        {
            outFullPath.assign(pathBuffer.data(), size);
            outExeName = std::filesystem::path(outFullPath).filename().wstring();
        }

        FILETIME createTime{}, exitTime{}, kernelTime{}, userTime{};
        if (GetProcessTimes(process.get(), &createTime, &exitTime, &kernelTime, &userTime))
        {
            ULARGE_INTEGER creationTime{};
            creationTime.LowPart = createTime.dwLowDateTime;
            creationTime.HighPart = createTime.dwHighDateTime;
            outCreationTime = creationTime.QuadPart;
        }

        return true;
    }

    bool ProcessInfo::IdentityMatches(DWORD pid, const std::wstring &exePath, uint64_t creationTime)
    {
        std::wstring path, name;
        uint64_t created = 0;
        if (!QueryDetails(pid, path, name, created))
        {
            return false;
        }

        if (!exePath.empty())
        {
            // Case-insensitive compare: Windows paths are case-insensitive.
            return _wcsicmp(path.c_str(), exePath.c_str()) == 0
                && (creationTime == 0 || created == 0 || creationTime == created);
        }
        return creationTime == 0 || created == 0 || creationTime == created;
    }
}
