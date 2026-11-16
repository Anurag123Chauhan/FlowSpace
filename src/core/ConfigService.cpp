#include "core/ConfigService.h"
#include "core/StringUtils.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
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
    void to_json(nlohmann::json &j, const OriginalWallpaper &w);
    void from_json(const nlohmann::json &j, OriginalWallpaper &w);
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
            j = json{{"action", static_cast<int>(h.action)}, {"enabled", h.enabled},
                     {"modifiers", W(h.modifiers)}, {"key", W(h.key)}};
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
            j = json{{"id", r.id}, {"enabled", r.enabled},
                     {"exeName", W(r.exeName)}, {"exePath", W(r.exePath)},
                     {"titleContains", W(r.titleContains)},
                     {"targetWorkspaceId", r.targetWorkspaceId},
                     {"monitorPreference", static_cast<int>(r.monitorPreference)},
                     {"monitorStableId", W(r.monitorStableId)},
                     {"launchCommand", W(r.launchCommand)}};
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

        void to_json(json &j, const OriginalWallpaper &w)
        {
            j = json{{"monitorDevicePath", W(w.monitorDevicePath)},
                     {"imagePath", W(w.imagePath)},
                     {"backgroundColor", w.backgroundColor},
                     {"fit", w.fit}};
        }

        void from_json(const json &j, OriginalWallpaper &w)
        {
            w.monitorDevicePath = S(j.value("monitorDevicePath", std::string{}));
            w.imagePath = S(j.value("imagePath", std::string{}));
            w.backgroundColor = j.value("backgroundColor", 0u);
            w.fit = j.value("fit", 4);
        }

        void to_json(json &j, const Workspace &ws)
        {
            json perMon = json::object();
            for (const auto &[monId, path] : ws.perMonitorWallpaper)
            {
                perMon[W(monId)] = W(path);
            }
            json perMonFit = json::object();
            for (const auto &[monId, fit] : ws.perMonitorWallpaperFit)
            {
                perMonFit[W(monId)] = fit;
            }
            j = json{{"id", ws.id}, {"name", ws.name}, {"icon", ws.icon},
                     {"accentColor", ws.accentColor}, {"orderIndex", ws.orderIndex},
                     {"wallpaper", W(ws.defaultWallpaperPath)},
                     {"wallpaperFit", ws.wallpaperFit},
                     {"wallpaperBackgroundColor", W(ws.wallpaperBackgroundColor)},
                     {"perMonitorWallpaper", perMon},
                     {"perMonitorWallpaperFit", perMonFit},
                     {"overlayBackgroundColor", W(ws.overlayBackgroundColor)},
                     {"overlayTextColor", W(ws.overlayTextColor)},
                     {"overlayPattern", W(ws.overlayPattern)},
                     {"overlayPatternOpacity", ws.overlayPatternOpacity},
                     {"overlayAnimation", ws.overlayAnimation},
                     {"overlayAnimationMs", ws.overlayAnimationMs}};
        }

        void from_json(const json &j, Workspace &ws)
        {
            ws.id = j.value("id", std::string{});
            ws.name = j.value("name", std::string{});
            ws.icon = j.value("icon", std::string("grid"));
            ws.accentColor = j.value("accentColor", std::string("#4389D8"));
            ws.orderIndex = j.value("orderIndex", 0);
            ws.defaultWallpaperPath = S(j.value("wallpaper", std::string{}));
            ws.wallpaperFit = j.value("wallpaperFit", 4);
            ws.wallpaperBackgroundColor = S(j.value("wallpaperBackgroundColor", std::string("#000000")));
            if (auto it = j.find("perMonitorWallpaper"); it != j.end() && it->is_object())
            {
                for (auto mit = it->begin(); mit != it->end(); ++mit)
                {
                    if (mit.value().is_string())
                    {
                        ws.perMonitorWallpaper[S(mit.key())] = S(mit.value().get<std::string>());
                    }
                }
            }
            if (auto it = j.find("perMonitorWallpaperFit"); it != j.end() && it->is_object())
            {
                for (auto mit = it->begin(); mit != it->end(); ++mit)
                {
                    if (mit.value().is_number_integer())
                    {
                        ws.perMonitorWallpaperFit[S(mit.key())] = mit.value().get<int32_t>();
                    }
                }
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
                {"wallpaperManagementEnabled", c.settings.wallpaperManagementEnabled},
                {"restoreWallpaperOnExit", c.settings.restoreWallpaperOnExit},
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
                {"originalWallpapers", c.originalWallpapers},
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
                c.settings.wallpaperManagementEnabled = s.value("wallpaperManagementEnabled", true);
                c.settings.restoreWallpaperOnExit = s.value("restoreWallpaperOnExit", true);
                c.settings.paused = s.value("paused", false);
                c.settings.groupWindowsByApplication = s.value("groupWindowsByApplication", true);
                c.settings.showOverlay = s.value("showOverlay", true);
                c.settings.wallpaperCaptureInitialized = s.value("wallpaperCaptureInitialized", false);
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
            if (auto it = j.find("originalWallpapers"); it != j.end() && it->is_array())
            {
                c.originalWallpapers = it->get<std::vector<OriginalWallpaper>>();
            }

            out = std::move(c);
            return true;
        }

    void ConfigService::ApplyDefaultWorkspaces(Config &config)
    {
        config.workspaces = {
            Workspace{.id = "ws-1", .name = "Development", .icon = "code", .accentColor = "#4389D8", .orderIndex = 0},
            Workspace{.id = "ws-2", .name = "Research", .icon = "book", .accentColor = "#2EA043", .orderIndex = 1},
            Workspace{.id = "ws-3", .name = "Personal", .icon = "home", .accentColor = "#D29922", .orderIndex = 2},
            Workspace{.id = "ws-4", .name = "Communication", .icon = "chat", .accentColor = "#A371F7", .orderIndex = 3},
        };
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
                ws.accentColor = "#4389D8";
            }
            if (ws.orderIndex < 0)
            {
                ws.orderIndex = 0;
            }
            seen.push_back(ws.id);
            valid.push_back(ws);
        }
        config.workspaces = std::move(valid);
        std::sort(config.workspaces.begin(), config.workspaces.end(),
                  [](const Workspace &a, const Workspace &b) { return a.orderIndex < b.orderIndex; });
        for (size_t i = 0; i < config.workspaces.size(); ++i)
        {
            config.workspaces[i].orderIndex = static_cast<int32_t>(i);
        }

        // Validate monitorState workspace references.
        for (auto it = config.monitorState.begin(); it != config.monitorState.end();)
        {
            const bool ok = std::any_of(config.workspaces.begin(), config.workspaces.end(),
                                        [&](const Workspace &ws) { return ws.id == it->second; });
            if (!ok)
            {
                it = config.monitorState.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Validate wallpaper image paths: non-existent files fall back to no image.
        for (auto &ws : config.workspaces)
        {
            if (!ws.defaultWallpaperPath.empty() && !fs::exists(fs::path(ws.defaultWallpaperPath)))
            {
                spdlog::warn("Workspace '{}': wallpaper '{}' not found; clearing",
                             ws.id, std::string(ws.defaultWallpaperPath.begin(), ws.defaultWallpaperPath.end()));
                ws.defaultWallpaperPath.clear();
            }
            for (auto it = ws.perMonitorWallpaper.begin(); it != ws.perMonitorWallpaper.end();)
            {
                if (!it->second.empty() && !fs::exists(fs::path(it->second)))
                {
                    spdlog::warn("Workspace '{}': per-monitor wallpaper '{}' not found; clearing", ws.id,
                                 std::string(it->second.begin(), it->second.end()));
                    it = ws.perMonitorWallpaper.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            ws.overlayAnimationMs = std::clamp(ws.overlayAnimationMs, 0, 2000);
        }

        // Validate hotkeys: drop entries without a key.
        config.hotkeys.erase(
            std::remove_if(config.hotkeys.begin(), config.hotkeys.end(),
                           [](const HotkeyBinding &h) { return h.key.empty(); }),
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
