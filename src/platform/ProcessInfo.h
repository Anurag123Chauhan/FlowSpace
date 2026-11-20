#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

namespace flowspace::platform
{
    // Extracted from WindowManager so the recovery journal can validate process identity
    // independently of window discovery.
    namespace ProcessInfo
    {
        // Opens the process with PROCESS_QUERY_LIMITED_INFORMATION and queries the executable
        // full path, file name, and creation time (FILETIME 100ns ticks). Returns false when the
        // process cannot be opened or queried (e.g. it has exited).
        bool QueryDetails(DWORD pid, std::wstring &outFullPath, std::wstring &outExeName, uint64_t &outCreationTime);

        // Convenience wrapper used by the recovery journal: returns true when the process
        // identified by (pid, exePath, creationTime) still exists and matches.
        bool IdentityMatches(DWORD pid, const std::wstring &exePath, uint64_t creationTime);
    }
}
