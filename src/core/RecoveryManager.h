#pragma once

#include "core/Models.h"
#include <Windows.h>
#include <optional>
#include <string>
#include <vector>

namespace flowspace::core
{
    /// Describes a window FlowSpace hid and may need to restore after a crash.
    /// Process details help ensure the saved window handle still belongs to that window.
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

    /// Saves hidden-window details to disk before hiding each window.
    /// On startup, it checks those details before restoring windows.
    class RecoveryManager
    {
      public:
        explicit RecoveryManager(std::wstring journalPath);

        // Loads the journal. A missing or corrupt file produces an empty list.
        void Load();

        bool Save();

        const std::vector<RecoveryEntry> &Entries() const { return entries_; }

        // Adds (or refreshes) an entry for a window about to be hidden.
        void RecordHide(const core::WindowInfo &window, const std::string &workspaceId, bool wasMinimized,
                        bool wasMaximized, const RECT &restoreRect);

        // Removes the entry for a restored window. Returns true when an entry existed.
        bool RecordShow(HWND hwnd);

        // Removes an entry for a destroyed window (nothing to restore).
        bool Forget(HWND hwnd);

        size_t Count() const { return entries_.size(); }

      private:
        std::wstring journalPath_;
        std::vector<RecoveryEntry> entries_;
    };
}
