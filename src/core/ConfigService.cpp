#include "core/ConfigService.h"
#include "core/StringUtils.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace flowspace::core
{
    // nlohmann serializes custom types through unqualified to_json/from_json calls resolved by
    // ADL, so these must live in flowspace::core itself — an anonymous namespace would hide
    // them from ADL.
    void to_json(nlohmann::json &j, const HotkeyBinding &h);
    void from_json(const nlohmann::json &j, HotkeyBinding &h);
    void to_json(nlohmann::json &j, const AppRule &r);
    void from_json(const nlohmann::json &j, AppRule &r);
    void to_json(nlohmann::json &j, const Workspace &ws);
    void from_json(const nlohmann::json &j, Workspace &ws);

    // Helpers below live directly in flowspace::core (not an anonymous namespace): nlohmann
    // resolves to_json/from_json for custom types via ADL, which only searches the associated
    // namespace flowspace::core. They are not declared in the header, so they remain internal
    // to this translation unit for all practical purposes.
    std::string W(const std::wstring &v) { return WideToUtf8(v); }
    std::wstring S(const std::string &v) { return Utf8ToWide(v); }

    void to_json(json &j, const HotkeyBinding &h)
    {
        j = json{{"action", static_cast<int>(h.action)}, {"enabled", h.enabled}, {"modifiers", W(h.modifiers)}, {"key", W(h.key)}};
    }

    void from_json(const json &j, HotkeyBinding &h)
    {
        if (auto it = j.find("action"); it != j.end() && it->is_number_integer())
        {
            h.action = static_cast<HotkeyAction>(it->get<int>());
        }
        h.enabled = j.value("enabled", true);
        h.modifiers = S(j.value("modifiers", std::string("Ctrl+Alt")));
        h.key = S(j.value("key", std::string("Left")));
    }

    void to_json(json &j, const AppRule &r)
    {
        j = json{{"id", r.id}, {"enabled", r.enabled}, {"exeName", W(r.exeName)}, {"exePath", W(r.exePath)}, {"titleContains", W(r.titleContains)}, {"targetWorkspaceId", r.targetWorkspaceId}, {"monitorPreference", static_cast<int>(r.monitorPreference)}, {"monitorStableId", W(r.monitorStableId)}, {"launchCommand", W(r.launchCommand)}};
    }

    void from_json(const json &j, AppRule &r)
    {
        r.id = j.value("id", std::string{});
        r.enabled = j.value("enabled", true);
        r.exeName = S(j.value("exeName", std::string{}));
        r.exePath = S(j.value("exePath", std::string{}));
        r.titleContains = S(j.value("titleContains", std::string{}));
        r.targetWorkspaceId = j.value("targetWorkspaceId", std::string{});
        r.monitorPreference = static_cast<MonitorPreference>(j.value("monitorPreference", 0));
        r.monitorStableId = S(j.value("monitorStableId", std::string{}));
        r.launchCommand = S(j.value("launchCommand", std::string{}));
    }

    void to_json(json &j, const Workspace &ws)
    {
        j = json{{"id", ws.id}, {"name", ws.name}, {"icon", ws.icon}, {"accentColor", ws.accentColor}, {"orderIndex", ws.orderIndex}, {"monitor", W(ws.monitorStableId)}, {"overlayBackgroundColor", W(ws.overlayBackgroundColor)}, {"overlayTextColor", W(ws.overlayTextColor)}, {"overlayPattern", W(ws.overlayPattern)}, {"overlayPatternOpacity", ws.overlayPatternOpacity}, {"overlayAnimation", ws.overlayAnimation}, {"overlayAnimationMs", ws.overlayAnimationMs}};
    }

    void from_json(const json &j, Workspace &ws)
    {
        ws.id = j.value("id", std::string{});
        ws.name = j.value("name", std::string{});
        ws.icon = j.value("icon", std::string("grid"));
        ws.accentColor = j.value("accentColor", std::string("#A87E5A"));
        ws.orderIndex = j.value("orderIndex", 0);
        if (auto it = j.find("monitor"); it != j.end() && it->is_string())
        {
            ws.monitorStableId = S(it->get<std::string>());
        }
        else if (auto legacy = j.find("monitorStableId"); legacy != j.end() && legacy->is_string())
        {
            ws.monitorStableId = S(legacy->get<std::string>());
        }
        ws.overlayBackgroundColor = S(j.value("overlayBackgroundColor", std::string{}));
        ws.overlayTextColor = S(j.value("overlayTextColor", std::string("#FFFFFFFF")));
        ws.overlayPattern = S(j.value("overlayPattern", std::string("none")));
        ws.overlayPatternOpacity = j.value("overlayPatternOpacity", 0.10);
        ws.overlayAnimation = j.value("overlayAnimation", std::string("slide"));
        ws.overlayAnimationMs = j.value("overlayAnimationMs", 180);
    }

    json Serialize(const Config &c)
    {
        json settings = json{
            {"hideMode", static_cast<int>(c.settings.hideMode)},
            {"startWithWindows", c.settings.startWithWindows},
            {"switchByMouseMonitor", c.settings.switchByMouseMonitor},
            {"paused", c.settings.paused},
            {"groupWindowsByApplication", c.settings.groupWindowsByApplication},
            {"showOverlay", c.settings.showOverlay},
            {"animation", json{{"enabled", c.settings.showOverlay},
                               {"style", c.settings.overlayAnimation},
                               {"durationMs", c.settings.overlayAnimationMs}}},
        };

        json monitorState = json::object();
        for (const auto &[monId, wsId] : c.monitorState)
        {
            monitorState[W(monId)] = wsId;
        }

        return json{
            {"schemaVersion", c.schemaVersion},
            {"settings", settings},
            {"workspaces", c.workspaces},
            {"appRules", c.appRules},
            {"hotkeys", c.hotkeys},
            {"monitorState", monitorState},
        };
    }

    bool TryParse(const json &j, Config &out)
    {
        if (!j.is_object())
        {
            return false;
        }
        Config c;
        c.schemaVersion = j.value("schemaVersion", 0); // missing field = legacy v0

        if (auto it = j.find("settings"); it != j.end() && it->is_object())
        {
            const auto &s = *it;
            c.settings.hideMode = static_cast<HideMode>(s.value("hideMode", 0));
            c.settings.startWithWindows = s.value("startWithWindows", false);
            c.settings.switchByMouseMonitor = s.value("switchByMouseMonitor", true);
            c.settings.paused = s.value("paused", false);
            c.settings.groupWindowsByApplication = s.value("groupWindowsByApplication", true);
            c.settings.showOverlay = s.value("showOverlay", true);
            if (auto a = s.find("animation"); a != s.end() && a->is_object())
            {
                c.settings.showOverlay = a->value("enabled", c.settings.showOverlay);
                c.settings.overlayAnimation = a->value("style", std::string("slide"));
                c.settings.overlayAnimationMs = a->value("durationMs", 180);
            }
        }

        if (auto it = j.find("workspaces"); it != j.end() && it->is_array())
        {
            c.workspaces = it->get<std::vector<Workspace>>();
        }
        if (auto it = j.find("appRules"); it != j.end() && it->is_array())
        {
            c.appRules = it->get<std::vector<AppRule>>();
        }
        if (auto it = j.find("hotkeys"); it != j.end() && it->is_array())
        {
            c.hotkeys = it->get<std::vector<HotkeyBinding>>();
        }
        if (auto it = j.find("monitorState"); it != j.end() && it->is_object())
        {
            for (auto mit = it->begin(); mit != it->end(); ++mit)
            {
                if (mit.value().is_string())
                {
                    c.monitorState[S(mit.key())] = mit.value().get<std::string>();
                }
            }
        }

        out = std::move(c);
        return true;
    }

    void ConfigService::ApplyDefaultWorkspaces(Config &config)
    {
        Workspace ws;
        ws.id = "ws-1";
        ws.name = "Default WS";
        ws.icon = "grid";
        ws.accentColor = "#A87E5A";
        ws.orderIndex = 0;
        config.workspaces = {std::move(ws)};
    }

    void ConfigService::ApplyDefaultHotkeys(Config &config)
    {
        config.hotkeys = {
            HotkeyBinding{HotkeyAction::PreviousWorkspace, true, L"Ctrl+Alt", L"Left"},
            HotkeyBinding{HotkeyAction::NextWorkspace, true, L"Ctrl+Alt", L"Right"},
            HotkeyBinding{HotkeyAction::SwitchToWorkspaceByNumber, true, L"Ctrl+Alt", L"D1"},
            HotkeyBinding{HotkeyAction::MoveActiveWindowToPreviousWorkspace, true, L"Ctrl+Alt+Shift", L"Left"},
            HotkeyBinding{HotkeyAction::MoveActiveWindowToNextWorkspace, true, L"Ctrl+Alt+Shift", L"Right"},
            HotkeyBinding{HotkeyAction::MoveActiveWindowToWorkspaceByNumber, true, L"Ctrl+Alt+Shift", L"D1"},
            HotkeyBinding{HotkeyAction::OpenWorkspaceSwitcher, true, L"Ctrl+Alt", L"Space"},
            HotkeyBinding{HotkeyAction::RevealAllWindows, true, L"Ctrl+Alt", L"F12"},
            HotkeyBinding{HotkeyAction::OpenSettings, true, L"Ctrl+Alt", L"O"},
        };
    }

    namespace
    {
        // Generates an id that does not collide with the workspaces already in the config.
        std::string UniqueWorkspaceId(const Config &config)
        {
            static std::atomic<uint64_t> counter{1000};
            for (;;)
            {
                std::string id = "ws-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
                const bool used = std::any_of(config.workspaces.begin(), config.workspaces.end(),
                                              [&](const Workspace &ws)
                                              { return ws.id == id; });
                if (!used)
                {
                    return id;
                }
            }
        }

        std::vector<Workspace> WorkspacesForMonitorSorted(const Config &config, const std::wstring &monitorId)
        {
            std::vector<Workspace> result;
            for (const auto &ws : config.workspaces)
            {
                if (ws.monitorStableId == monitorId)
                {
                    result.push_back(ws);
                }
            }
            std::stable_sort(result.begin(), result.end(), [](const Workspace &a, const Workspace &b)
                             { return a.orderIndex < b.orderIndex; });
            return result;
        }

        // When an active-workspace reference points at a workspace owned by another monitor,
        // remap it to the same position in the target monitor's own list.
        void RemapMonitorStateByIndex(Config &config, const std::wstring &monitorId)
        {
            auto stateIt = config.monitorState.find(monitorId);
            if (stateIt == config.monitorState.end())
            {
                return;
            }
            const Workspace *referenced = nullptr;
            for (const auto &ws : config.workspaces)
            {
                if (ws.id == stateIt->second)
                {
                    referenced = &ws;
                    break;
                }
            }
            if (referenced == nullptr || referenced->monitorStableId == monitorId)
            {
                return;
            }

            const auto sourceList = WorkspacesForMonitorSorted(config, referenced->monitorStableId);
            size_t index = 0;
            bool found = false;
            for (; index < sourceList.size(); ++index)
            {
                if (sourceList[index].id == stateIt->second)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return;
            }

            const auto targetList = WorkspacesForMonitorSorted(config, monitorId);
            if (index < targetList.size())
            {
                stateIt->second = targetList[index].id;
            }
        }

        // v1 -> v2: assign unowned workspaces to a "legacy owner" monitor, clone the set for
        // every other monitor already known from the persisted state, and remap each monitor's
        // active workspace reference onto its own clone.
        void MigrateWorkspacesToPerMonitor(Config &config)
        {
            std::vector<std::wstring> knownMonitors;
            auto remember = [&](const std::wstring &id)
            {
                if (!id.empty() &&
                    std::find(knownMonitors.begin(), knownMonitors.end(), id) == knownMonitors.end())
                {
                    knownMonitors.push_back(id);
                }
            };
            for (const auto &[monitorId, _] : config.monitorState)
            {
                remember(monitorId);
            }

            if (knownMonitors.empty())
            {
                // No monitor hint at all: leave the list unowned. WorkspaceManager::ReconcileMonitors
                // adopts it for the first display detected at runtime.
                return;
            }

            const std::wstring owner = knownMonitors.front();
            for (auto &ws : config.workspaces)
            {
                if (ws.monitorStableId.empty())
                {
                    ws.monitorStableId = owner;
                }
            }

            const auto ownerList = WorkspacesForMonitorSorted(config, owner);
            if (ownerList.empty())
            {
                return;
            }

            for (const auto &monitorId : knownMonitors)
            {
                if (monitorId == owner)
                {
                    continue;
                }

                const bool alreadyHasWorkspaces =
                    std::any_of(config.workspaces.begin(), config.workspaces.end(),
                                [&](const Workspace &ws)
                                { return ws.monitorStableId == monitorId; });
                if (alreadyHasWorkspaces)
                {
                    RemapMonitorStateByIndex(config, monitorId);
                    continue;
                }

                std::vector<std::pair<std::string, std::string>> idMap;
                for (const auto &source : ownerList)
                {
                    Workspace copy = source;
                    copy.id = UniqueWorkspaceId(config);
                    copy.monitorStableId = monitorId;
                    idMap.emplace_back(source.id, copy.id);
                    config.workspaces.push_back(std::move(copy));
                }

                auto stateIt = config.monitorState.find(monitorId);
                if (stateIt != config.monitorState.end())
                {
                    for (const auto &[oldId, newId] : idMap)
                    {
                        if (stateIt->second == oldId)
                        {
                            stateIt->second = newId;
                            break;
                        }
                    }
                }
            }

            // Any remaining cross-monitor reference is remapped by list position.
            for (const auto &monitorId : knownMonitors)
            {
                RemapMonitorStateByIndex(config, monitorId);
            }
        }
    } // namespace

    void ConfigService::Migrate(Config &config)
    {
        // v0 (missing schemaVersion): the diagnostic-era config carried only a workspaceCount
        // hint and no per-workspace wallpaper/appearance state. Regenerate default names if the
        // list is empty, attach default hotkeys, and stamp the new schema version.
        if (config.schemaVersion < 1)
        {
            spdlog::info("Migrating configuration schema v{} -> v1", config.schemaVersion);
            config.schemaVersion = 1;
            if (config.workspaces.empty())
            {
                ApplyDefaultWorkspaces(config);
            }
            if (config.hotkeys.empty())
            {
                ApplyDefaultHotkeys(config);
            }
        }

        // v2: workspaces become per-monitor. Legacy configs (schemaVersion 1) had one global
        // list shared by every display; assign the originals to one monitor and clone the set
        // for every other monitor already known from monitorState/wallpaper captures.
        if (config.schemaVersion < 2)
        {
            spdlog::info("Migrating configuration schema v{} -> v2 (per-monitor workspaces)", config.schemaVersion);
            MigrateWorkspacesToPerMonitor(config);
            if (config.workspaces.empty())
            {
                ApplyDefaultWorkspaces(config);
            }
            config.schemaVersion = 2;
        }

        // Future migrations append here as `if (config.schemaVersion < N) {...}`.
    }

    void ConfigService::Normalize(Config &config)
    {
        // Ensure every workspace has an id; drop exact-duplicate ids; re-number order indexes.
        std::vector<std::string> seen;
        int nextId = 100;
        std::vector<Workspace> valid;
        for (auto &ws : config.workspaces)
        {
            if (ws.id.empty())
            {
                ws.id = "ws-" + std::to_string(nextId++);
            }
            if (std::find(seen.begin(), seen.end(), ws.id) != seen.end())
            {
                spdlog::warn("Dropping duplicate workspace id '{}'", ws.id);
                continue;
            }
            if (ws.name.empty())
            {
                ws.name = "Workspace";
            }
            uint32_t rgb = 0;
            if (!TryParseHexColor(Utf8ToWide(ws.accentColor), rgb))
            {
                ws.accentColor = "#A87E5A";
            }
            if (ws.orderIndex < 0)
            {
                ws.orderIndex = 0;
            }
            seen.push_back(ws.id);
            valid.push_back(ws);
        }
        config.workspaces = std::move(valid);
        // Sort by owning monitor, then by the monitor-local order index; renumber each
        // monitor's list contiguously from 0 so numbering/hotkeys stay predictable.
        std::stable_sort(config.workspaces.begin(), config.workspaces.end(),
                         [](const Workspace &a, const Workspace &b)
                         {
                             if (a.monitorStableId != b.monitorStableId)
                             {
                                 return a.monitorStableId < b.monitorStableId;
                             }
                             return a.orderIndex < b.orderIndex;
                         });
        std::wstring currentMonitor;
        int32_t nextOrder = 0;
        bool first = true;
        for (auto &ws : config.workspaces)
        {
            if (first || ws.monitorStableId != currentMonitor)
            {
                currentMonitor = ws.monitorStableId;
                nextOrder = 0;
                first = false;
            }
            ws.orderIndex = nextOrder++;
        }

        // Validate monitorState workspace references: the workspace must exist and, when it
        // already has an owner, must belong to that monitor.
        for (auto it = config.monitorState.begin(); it != config.monitorState.end();)
        {
            auto wsIt = std::find_if(config.workspaces.begin(), config.workspaces.end(),
                                     [&](const Workspace &ws)
                                     { return ws.id == it->second; });
            const bool ok = wsIt != config.workspaces.end() && (wsIt->monitorStableId.empty() || wsIt->monitorStableId == it->first);
            if (!ok)
            {
                it = config.monitorState.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Drop orphaned unassigned workspaces when other workspaces already have owners.
        // These are leftovers from a v1->v2 migration that already cloned the set to every
        // monitor; keeping them would inflate counts (e.g. Overview showing 8 instead of 4).
        const bool hasAssigned = std::any_of(config.workspaces.begin(), config.workspaces.end(),
                                             [](const Workspace &ws)
                                             { return !ws.monitorStableId.empty(); });
        if (hasAssigned)
        {
            auto it = std::remove_if(config.workspaces.begin(), config.workspaces.end(),
                                     [](const Workspace &ws)
                                     { return ws.monitorStableId.empty(); });
            if (it != config.workspaces.end())
            {
                spdlog::warn("Dropping {} orphaned unassigned workspace(s)", std::distance(it, config.workspaces.end()));
                config.workspaces.erase(it, config.workspaces.end());
            }
        }

        // Validate hotkeys: drop entries without a key.
        config.hotkeys.erase(
            std::remove_if(config.hotkeys.begin(), config.hotkeys.end(),
                           [](const HotkeyBinding &h)
                           { return h.key.empty(); }),
            config.hotkeys.end());

        if (config.settings.overlayAnimationMs < 0)
        {
            config.settings.overlayAnimationMs = 180;
        }
    }

    ConfigService::ConfigService(std::wstring configPath, std::wstring backupPath)
        : configPath_(std::move(configPath)), backupPath_(std::move(backupPath))
    {
        ApplyDefaultWorkspaces(config_);
        ApplyDefaultHotkeys(config_);
    }

    ConfigLoadStatus ConfigService::Load()
    {
        auto readAll = [](const std::wstring &path) -> std::optional<std::string>
        {
            std::ifstream file(fs::path(path), std::ios::binary);
            if (!file)
            {
                return std::nullopt;
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            return ss.str();
        };

        auto parse = [&](const std::string &text, Config &out) -> bool
        {
            try
            {
                json j = json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/true,
                                     /*ignore_comments=*/false);
                return TryParse(j, out);
            }
            catch (const json::exception &e)
            {
                spdlog::warn("Config parse error: {}", e.what());
                return false;
            }
        };

        if (auto text = readAll(configPath_))
        {
            Config parsed;
            if (parse(*text, parsed))
            {
                Migrate(parsed);
                Normalize(parsed);
                config_ = std::move(parsed);
                return ConfigLoadStatus::Loaded;
            }
            spdlog::error("config.json is corrupt; attempting backup recovery");
        }

        if (auto text = readAll(backupPath_))
        {
            Config parsed;
            if (parse(*text, parsed))
            {
                Migrate(parsed);
                Normalize(parsed);
                config_ = std::move(parsed);
                return ConfigLoadStatus::RecoveredFromBackup;
            }
        }

        // No readable file: keep defaults (already initialized in the constructor).
        Migrate(config_);
        Normalize(config_);
        return ConfigLoadStatus::FreshDefaults;
    }

    bool ConfigService::Save()
    {
        namespace fs = std::filesystem;
        try
        {
            fs::path main(configPath_);
            fs::path backup(backupPath_);
            fs::create_directories(main.parent_path());
            if (fs::exists(main))
            {
                fs::copy_file(main, backup, fs::copy_options::overwrite_existing);
            }

            // Atomic write: temp file in the same directory, flushed, then renamed.
            const std::string text = Serialize(config_).dump(2);
            const fs::path tmp = main.native() + L".tmp";
            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    spdlog::error("Cannot open config for writing: {}", std::string(main.u8string().begin(), main.u8string().end()));
                    return false;
                }
                out << text;
                out.flush();
            }
            std::error_code ec;
            fs::rename(tmp, main, ec);
            if (ec)
            {
                // Windows rename-to-existing fails; remove then rename.
                fs::remove(main, ec);
                fs::rename(tmp, main, ec);
            }
            if (ec)
            {
                spdlog::error("Config save rename failed: {}", ec.message());
                return false;
            }
            return true;
        }
        catch (const std::exception &e)
        {
            spdlog::error("Config save failed: {}", e.what());
            return false;
        }
    }
}
