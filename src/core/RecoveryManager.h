#pragma once

#include "core/Models.h"
#include <Windows.h>
#include <optional>
#include <string>
#include <vector>

namespace flowspace::core
{
    /// <summary>
    /// One entry in the recovery journal: a window hidden by FlowSpace. Holds enough validated
    /// identity information (process id, executable path, process creation time) to restore the
    /// window safely after an abnormal shutdown — the HWND alone is never trusted.
    /// </summary>
    struct RecoveryEntry
    {
        HWND hwnd{nullptr};
        DWORD processId{0};
        std::wstring executablePath;
        std::wstring executableName;
        uint64_t processCreationTime{0};
        std::wstring title;
        std::wstring className;
        std::wstring monitorStableId;
        std::string workspaceId;
        bool wasMinimized{false};
        bool wasMaximized{false};
        RECT restoreRect{};
        long long hiddenAtUnixMs{0}; // epoch milliseconds, diagnostics only
    };

    /// <summary>
    /// File-backed recovery journal. Every window hidden by FlowSpace is journaled before being
    /// hidden and removed when shown again. On startup after a crash the journal is replayed:
    /// entries are validated against the live window and process identity before restoration.
    /// </summary>
    class RecoveryManager
    {
    public:
        explicit RecoveryManager(std::wstring journalPath);

        // Loads journal.json. A corrupt or missing file yields an empty journal (fail-safe:
        // no restoration is attempted, which risks nothing).
        void Load();

        // Atomically persists the current journal.
        bool Save();

        const std::vector<RecoveryEntry> &Entries() const { return entries_; }

        // Adds (or refreshes) an entry for a window about to be hidden.
        void RecordHide(const core::WindowInfo &window, const std::string &workspaceId,
                        bool wasMinimized, bool wasMaximized, const RECT &restoreRect);

        // Removes the entry for a restored window. Returns true when an entry existed.
        bool RecordShow(HWND hwnd);

        // Removes an entry for a destroyed window (nothing to restore).
        bool Forget(HWND hwnd);

        // Number of entries currently pending restoration.
        size_t Count() const { return entries_.size(); }

    private:
        std::wstring journalPath_;
        std::vector<RecoveryEntry> entries_;
    };
}
