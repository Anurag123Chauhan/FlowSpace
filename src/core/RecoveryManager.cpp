#include "core/RecoveryManager.h"
#include "core/StringUtils.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace flowspace::core
{
    namespace
    {
        std::string W(const std::wstring &v)
        {
            return WideToUtf8(v);
        }
        std::wstring S(const std::string &v)
        {
            return Utf8ToWide(v);
        }

        json EntryToJson(const RecoveryEntry &e)
        {
            return json{
                {"hwnd", reinterpret_cast<uint64_t>(e.hwnd)},
                {"processId", e.processId},
                {"executablePath", W(e.executablePath)},
                {"executableName", W(e.executableName)},
                {"processCreationTime", e.processCreationTime},
                {"title", W(e.title)},
                {"className", W(e.className)},
                {"monitorStableId", W(e.monitorStableId)},
                {"workspaceId", e.workspaceId},
                {"wasMinimized", e.wasMinimized},
                {"wasMaximized", e.wasMaximized},
                {"restoreRect", {e.restoreRect.left, e.restoreRect.top, e.restoreRect.right, e.restoreRect.bottom}},
                {"hiddenAtUnixMs", e.hiddenAtUnixMs},
            };
        }

        bool EntryFromJson(const json &j, RecoveryEntry &e)
        {
            if (!j.is_object())
            {
                return false;
            }
            e.hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(j.value("hwnd", 0ull)));
            e.processId = j.value("processId", 0u);
            e.executablePath = S(j.value("executablePath", std::string{}));
            e.executableName = S(j.value("executableName", std::string{}));
            e.processCreationTime = j.value("processCreationTime", 0ull);
            e.title = S(j.value("title", std::string{}));
            e.className = S(j.value("className", std::string{}));
            e.monitorStableId = S(j.value("monitorStableId", std::string{}));
            e.workspaceId = j.value("workspaceId", std::string{});
            e.wasMinimized = j.value("wasMinimized", false);
            e.wasMaximized = j.value("wasMaximized", false);
            if (j.contains("restoreRect") && j["restoreRect"].is_array() && j["restoreRect"].size() == 4)
            {
                e.restoreRect.left = j["restoreRect"][0].get<LONG>();
                e.restoreRect.top = j["restoreRect"][1].get<LONG>();
                e.restoreRect.right = j["restoreRect"][2].get<LONG>();
                e.restoreRect.bottom = j["restoreRect"][3].get<LONG>();
            }
            e.hiddenAtUnixMs = j.value("hiddenAtUnixMs", 0ll);
            return e.hwnd != nullptr;
        }
    }

    RecoveryManager::RecoveryManager(std::wstring journalPath) : journalPath_(std::move(journalPath))
    {
    }

    void RecoveryManager::Load()
    {
        entries_.clear();
        fs::path path(journalPath_);
        if (!fs::exists(path))
        {
            return;
        }

        try
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                return;
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            json j = json::parse(ss.str(), nullptr, /*allow_exceptions=*/true);
            if (!j.is_array())
            {
                return;
            }
            for (const auto &item : j)
            {
                RecoveryEntry e;
                if (EntryFromJson(item, e))
                {
                    entries_.push_back(e);
                }
            }
            spdlog::info("Loaded recovery journal with {} pending window(s)", entries_.size());
        }
        catch (const json::exception &e)
        {
            // Corrupt journal: the safest behavior is to attempt nothing rather than guessing.
            spdlog::error("Recovery journal corrupt ({}); ignoring it", e.what());
            entries_.clear();
        }
    }

    bool RecoveryManager::Save()
    {
        try
        {
            json arr = json::array();
            for (const auto &e : entries_)
            {
                arr.push_back(EntryToJson(e));
            }
            const std::string text = arr.dump(2);

            fs::path path(journalPath_);
            fs::create_directories(path.parent_path());
            const fs::path tmp = path.native() + L".tmp";
            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    return false;
                }
                out << text;
                out.flush();
            }
            std::error_code ec;
            fs::rename(tmp, path, ec);
            if (ec)
            {
                fs::remove(path, ec);
                fs::rename(tmp, path, ec);
            }
            return !ec;
        }
        catch (const std::exception &e)
        {
            spdlog::error("Recovery journal save failed: {}", e.what());
            return false;
        }
    }

    void RecoveryManager::RecordHide(const core::WindowInfo &window, const std::string &workspaceId, bool wasMinimized,
                                     bool wasMaximized, const RECT &restoreRect)
    {
        // Refresh or replace any existing entry for this hwnd (e.g. re-hidden after a peek).
        RecordShow(window.hwnd);

        RecoveryEntry e;
        e.hwnd = window.hwnd;
        e.processId = window.processId;
        e.executablePath = window.executablePath;
        e.executableName = window.executableName;
        e.processCreationTime = window.processCreationTime;
        e.title = window.title;
        e.className = window.className;
        e.monitorStableId = window.monitorStableId;
        e.workspaceId = workspaceId;
        e.wasMinimized = wasMinimized;
        e.wasMaximized = wasMaximized;
        e.restoreRect = restoreRect;
        e.hiddenAtUnixMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        entries_.push_back(std::move(e));
        Save();
    }

    bool RecoveryManager::RecordShow(HWND hwnd)
    {
        const size_t before = entries_.size();
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(), [hwnd](const RecoveryEntry &e) { return e.hwnd == hwnd; }),
            entries_.end());
        if (entries_.size() != before)
        {
            Save();
            return true;
        }
        return false;
    }

    bool RecoveryManager::Forget(HWND hwnd)
    {
        return RecordShow(hwnd); // same operation: remove by hwnd
    }
}
