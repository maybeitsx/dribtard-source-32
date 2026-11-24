#include "Configuration/Settings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    constexpr int kConfigVersion = 1;
    constexpr std::size_t kMaxNameLength = 48;

    void AppendInt(std::string& out, const char* key, int value, bool comma = true)
    {
        out += "  \"";
        out += key;
        out += "\": ";
        out += std::to_string(value);
        out += comma ? ",\n" : "\n";
    }

    void AppendBool(std::string& out, const char* key, bool value, bool comma = true)
    {
        out += "  \"";
        out += key;
        out += "\": ";
        out += value ? "true" : "false";
        out += comma ? ",\n" : "\n";
    }

    void AppendDouble(std::string& out, const char* key, double value, bool comma = true)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.4f", value);
        out += "  \"";
        out += key;
        out += "\": ";
        out += buffer;
        out += comma ? ",\n" : "\n";
    }

    bool FindValue(const std::string& json, const std::string& key, std::string& value)
    {
        const std::string needle = "\"" + key + "\"";
        std::size_t pos = 0;

        while ((pos = json.find(needle, pos)) != std::string::npos)
        {
            std::size_t p = pos + needle.size();
            while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])))
                ++p;

            if (p >= json.size() || json[p] != ':')
            {
                pos += needle.size();
                continue;
            }

            ++p;
            while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])))
                ++p;

            if (p >= json.size())
                return false;

            if (json[p] == '"')
            {
                const std::size_t start = ++p;
                while (p < json.size() && json[p] != '"')
                    ++p;
                value = json.substr(start, p - start);
                return true;
            }

            const std::size_t start = p;
            while (p < json.size() && json[p] != ',' && json[p] != '}' &&
                json[p] != '\n' && json[p] != '\r')
            {
                ++p;
            }

            value = json.substr(start, p - start);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.pop_back();
            return true;
        }

        return false;
    }

    bool ReadInt(const std::string& json, const std::string& key, int& out)
    {
        std::string raw;
        if (!FindValue(json, key, raw) || raw.empty())
            return false;

        char* end = nullptr;
        const long parsed = std::strtol(raw.c_str(), &end, 10);
        if (end == raw.c_str())
            return false;

        out = static_cast<int>(parsed);
        return true;
    }

    bool ReadDouble(const std::string& json, const std::string& key, double& out)
    {
        std::string raw;
        if (!FindValue(json, key, raw) || raw.empty())
            return false;

        char* end = nullptr;
        const double parsed = std::strtod(raw.c_str(), &end);
        if (end == raw.c_str())
            return false;

        out = parsed;
        return true;
    }

    bool ReadBool(const std::string& json, const std::string& key, bool& out)
    {
        std::string raw;
        if (!FindValue(json, key, raw))
            return false;

        if (raw == "true" || raw == "1")
        {
            out = true;
            return true;
        }
        if (raw == "false" || raw == "0")
        {
            out = false;
            return true;
        }
        return false;
    }

    int ClampInt(int value, int low, int high)
    {
        return value < low ? low : (value > high ? high : value);
    }

    double ClampDouble(double value, double low, double high)
    {
        return value < low ? low : (value > high ? high : value);
    }

    std::string Trim(const std::string& text)
    {
        std::size_t begin = 0;
        while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
            ++begin;

        std::size_t end = text.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
            --end;

        return text.substr(begin, end - begin);
    }

    std::string Serialize(const AppConfig& config)
    {
        const MacroSettings& m = config.macro;
        const BladeBallSettings& bb = config.bladeBall;
        const UiSettings& ui = config.ui;

        std::string out = "{\n";
        AppendInt(out, "version", kConfigVersion);
        AppendInt(out, "triggerKey", m.triggerKey);
        AppendInt(out, "activationKey", m.activationKey);
        AppendInt(out, "secondaryKey", m.secondaryKey);
        AppendInt(out, "curveType", static_cast<int>(m.curveType));
        AppendBool(out, "customCurveAngle", m.customCurveAngle);
        AppendBool(out, "hotkeyMode", m.hotkeyMode);
        AppendDouble(out, "horizontalDegrees", m.horizontalDegrees);
        AppendDouble(out, "verticalDegrees", m.verticalDegrees);
        AppendDouble(out, "rotationDurationSeconds", m.rotationDurationSeconds);
        AppendDouble(out, "robloxSensitivity", m.robloxSensitivity);
        AppendDouble(out, "mouseCountsPerDegree", m.mouseCountsPerDegree);
        AppendBool(out, "qBeforeCamera", m.qBeforeCamera);
        AppendDouble(out, "activationHoldSeconds", m.activationHoldSeconds);
        AppendDouble(out, "activationLeadSeconds", m.activationLeadSeconds);
        AppendDouble(out, "initialDelaySeconds", m.initialDelaySeconds);
        AppendDouble(out, "secondaryDelaySeconds", m.secondaryDelaySeconds);
        AppendDouble(out, "secondaryHoldSeconds", m.secondaryHoldSeconds);
        AppendBool(out, "instantMode", m.instantMode);
        AppendBool(out, "restoreCamera", m.restoreCamera);
        AppendInt(out, "clickerKey", config.clicker.key);
        AppendInt(out, "clickerMode", config.clicker.mode == ClickerMode::Toggle ? 1 : 0);
        AppendInt(out, "clickerOutputKey", config.clicker.outputKey);
        // Kept for older builds that only knew left and right.
        AppendInt(out, "clickerButton", ClickerOutputIsRight(config.clicker.outputKey) ? 1 : 0);
        AppendInt(out, "clickerCps", ClampClickerCps(config.clicker.cps));
        AppendBool(out, "bbArmed", bb.armed);
        AppendBool(out, "bbHotkeyMode", bb.hotkeyMode);
        AppendInt(out, "bbCurveType", static_cast<int>(bb.curveType));
        AppendBool(out, "bbCustomCurve", bb.customCurveAngle);
        AppendInt(out, "bbTriggerKey", bb.triggerKey);
        AppendInt(out, "bbParryKey", bb.parryKey);
        AppendInt(out, "bbAbilityKey", bb.abilityKey);
        AppendBool(out, "bbAbilityEnabled", bb.abilityEnabled);
        AppendDouble(out, "bbAbilityDelaySeconds", bb.abilityDelaySeconds);
        AppendDouble(out, "bbHorizontal", bb.horizontalDegrees);
        AppendDouble(out, "bbVertical", bb.verticalDegrees);
        AppendDouble(out, "bbDuration", bb.flickDurationSeconds);
        AppendDouble(out, "bbSensitivity", bb.robloxSensitivity);
        AppendDouble(out, "bbCountsPerDegree", bb.mouseCountsPerDegree);
        AppendBool(out, "bbRestoreCamera", bb.restoreCamera);
        AppendInt(out, "menuKey", ui.menuKey);
        AppendDouble(out, "accentR", static_cast<double>(ui.accentR));
        AppendDouble(out, "accentG", static_cast<double>(ui.accentG));
        AppendDouble(out, "accentB", static_cast<double>(ui.accentB));
        AppendDouble(out, "cardR", static_cast<double>(ui.cardR));
        AppendDouble(out, "cardG", static_cast<double>(ui.cardG));
        AppendDouble(out, "cardB", static_cast<double>(ui.cardB));
        AppendDouble(out, "panelR", static_cast<double>(ui.panelR));
        AppendDouble(out, "panelG", static_cast<double>(ui.panelG));
        AppendDouble(out, "panelB", static_cast<double>(ui.panelB));
        AppendDouble(out, "glowIntensity", static_cast<double>(ui.glowIntensity));
        AppendDouble(out, "windowGlowIntensity", static_cast<double>(ui.windowGlowIntensity));
        AppendDouble(out, "panelOpacity", static_cast<double>(ui.panelOpacity));
        AppendBool(out, "darkenBackground", ui.darkenBackground);
        AppendDouble(out, "dimOpacity", static_cast<double>(ui.dimOpacity));
        AppendBool(out, "showNotifications", ui.showNotifications);
        AppendBool(out, "chatDetection", ui.chatDetection);
        AppendBool(out, "portableMode", ui.portableMode);
        AppendBool(out, "tabFade", ui.tabFade, false);
        out += "}\n";
        return out;
    }

    bool Parse(const std::string& json, AppConfig& config)
    {
        if (json.empty())
            return false;

        int version = 0;
        if (!ReadInt(json, "version", version) || version != kConfigVersion)
            return false;

        AppConfig parsed;
        int v = 0;
        double d = 0.0;
        bool b = false;

        MacroSettings& m = parsed.macro;
        if (ReadInt(json, "triggerKey", v)) m.triggerKey = ClampInt(v, 0, 255);
        if (ReadInt(json, "activationKey", v)) m.activationKey = ClampInt(v, 0, 255);
        if (ReadInt(json, "secondaryKey", v)) m.secondaryKey = ClampInt(v, 0, 255);
        if (ReadInt(json, "curveType", v) && v >= 0 && v <= 3)
            m.curveType = static_cast<CurveType>(v);
        if (ReadBool(json, "customCurveAngle", b)) m.customCurveAngle = b;
        if (ReadBool(json, "hotkeyMode", b)) m.hotkeyMode = b;
        if (ReadDouble(json, "horizontalDegrees", d)) m.horizontalDegrees = ClampDouble(d, -180.0, 180.0);
        if (ReadDouble(json, "verticalDegrees", d)) m.verticalDegrees = ClampDouble(d, -180.0, 180.0);
        if (ReadDouble(json, "rotationDurationSeconds", d)) m.rotationDurationSeconds = ClampDouble(d, 0.0, 10.0);
        if (ReadDouble(json, "robloxSensitivity", d)) m.robloxSensitivity = ClampDouble(d, 0.001, 100.0);
        if (ReadDouble(json, "mouseCountsPerDegree", d)) m.mouseCountsPerDegree = ClampDouble(d, 0.1, 100.0);
        if (ReadBool(json, "qBeforeCamera", b)) m.qBeforeCamera = b;
        if (ReadDouble(json, "activationHoldSeconds", d)) m.activationHoldSeconds = ClampDouble(d, 0.0, 10.0);
        if (ReadDouble(json, "activationLeadSeconds", d)) m.activationLeadSeconds = ClampDouble(d, 0.0, 10.0);
        if (ReadDouble(json, "initialDelaySeconds", d)) m.initialDelaySeconds = ClampDouble(d, 0.0, 30.0);
        if (ReadDouble(json, "secondaryDelaySeconds", d)) m.secondaryDelaySeconds = ClampDouble(d, 0.0, 30.0);
        if (ReadDouble(json, "secondaryHoldSeconds", d)) m.secondaryHoldSeconds = ClampDouble(d, 0.0, 10.0);
        if (ReadBool(json, "instantMode", b)) m.instantMode = b;
        if (ReadBool(json, "restoreCamera", b)) m.restoreCamera = b;

        if (ReadInt(json, "clickerKey", v))
            parsed.clicker.key = ClampInt(v, 0, 255);
        else if (ReadInt(json, "moonwalkHoldKey", v))
            parsed.clicker.key = ClampInt(v, 0, 255);
        if (ReadInt(json, "clickerMode", v))
            parsed.clicker.mode = v == 1 ? ClickerMode::Toggle : ClickerMode::Hold;
        if (ReadInt(json, "clickerOutputKey", v))
            parsed.clicker.outputKey = v;
        else if (ReadInt(json, "clickerButton", v))
            parsed.clicker.outputKey = v == 1 ? ClickerMouseRight : ClickerMouseLeft;
        if (ReadInt(json, "clickerCps", v))
            parsed.clicker.cps = ClampClickerCps(v);

        BladeBallSettings& bb = parsed.bladeBall;
        if (ReadBool(json, "bbArmed", b)) bb.armed = b;
        if (ReadBool(json, "bbHotkeyMode", b)) bb.hotkeyMode = b;
        if (ReadInt(json, "bbCurveType", v) && v >= 0 && v <= 3)
            bb.curveType = static_cast<CurveType>(v);
        if (ReadBool(json, "bbCustomCurve", b)) bb.customCurveAngle = b;
        if (ReadInt(json, "bbTriggerKey", v)) bb.triggerKey = ClampInt(v, 0, 255);
        if (ReadInt(json, "bbParryKey", v)) bb.parryKey = ClampInt(v, 0, 255);
        if (ReadInt(json, "bbAbilityKey", v)) bb.abilityKey = ClampInt(v, 0, 255);
        if (ReadBool(json, "bbAbilityEnabled", b)) bb.abilityEnabled = b;
        if (ReadDouble(json, "bbAbilityDelaySeconds", d)) bb.abilityDelaySeconds = ClampDouble(d, 0.0, 30.0);
        if (ReadDouble(json, "bbHorizontal", d)) bb.horizontalDegrees = ClampDouble(d, -180.0, 180.0);
        if (ReadDouble(json, "bbVertical", d)) bb.verticalDegrees = ClampDouble(d, -180.0, 180.0);
        if (ReadDouble(json, "bbDuration", d)) bb.flickDurationSeconds = ClampDouble(d, 0.0, 10.0);
        if (ReadDouble(json, "bbSensitivity", d)) bb.robloxSensitivity = ClampDouble(d, 0.001, 100.0);
        if (ReadDouble(json, "bbCountsPerDegree", d)) bb.mouseCountsPerDegree = ClampDouble(d, 0.1, 100.0);
        if (ReadBool(json, "bbRestoreCamera", b)) bb.restoreCamera = b;

        // An old or hand-edited file can bind the trigger to the key the macro
        // presses. The game would then get two F presses for one parry, which
        // looks exactly like a double click.
        if (bb.triggerKey == bb.parryKey)
            bb.triggerKey = (bb.parryKey == Keys::V) ? Keys::C : Keys::V;

        UiSettings& ui = parsed.ui;
        if (ReadInt(json, "menuKey", v)) ui.menuKey = ClampInt(v, 0, 255);
        if (ReadDouble(json, "accentR", d)) ui.accentR = static_cast<float>(ClampDouble(d, 0.0, 1.0));
        if (ReadDouble(json, "accentG", d)) ui.accentG = static_cast<float>(ClampDouble(d, 0.0, 1.0));
        if (ReadDouble(json, "accentB", d)) ui.accentB = static_cast<float>(ClampDouble(d, 0.0, 1.0));
        if (ReadDouble(json, "cardR", d)) ui.cardR = static_cast<float>(ClampDouble(d, 0.0, 1.0));
        if (ReadDouble(json, "cardG", d)) ui.cardG = static_cast<float>(ClampDouble(d, 0.0, 1.0));
        if (ReadDouble(json, "cardB", d)) ui.cardB = static_cast<float>(ClampDouble(d, 0.0, 1.0));
        if (ReadDouble(json, "panelR", d)) ui.panelR = static_cast<float>(ClampDouble(d, 0.0, 1.0));
        if (ReadDouble(json, "panelG", d)) ui.panelG = static_cast<float>(ClampDouble(d, 0.0, 1.0));
        if (ReadDouble(json, "panelB", d)) ui.panelB = static_cast<float>(ClampDouble(d, 0.0, 1.0));
        if (ReadDouble(json, "glowIntensity", d)) ui.glowIntensity = static_cast<float>(ClampDouble(d, 0.0, 1.5));
        if (ReadDouble(json, "windowGlowIntensity", d)) ui.windowGlowIntensity = static_cast<float>(ClampDouble(d, 0.0, 1.5));
        if (ReadDouble(json, "panelOpacity", d)) ui.panelOpacity = static_cast<float>(ClampDouble(d, 0.30, 1.0));
        if (ReadBool(json, "darkenBackground", b)) ui.darkenBackground = b;
        if (ReadDouble(json, "dimOpacity", d)) ui.dimOpacity = static_cast<float>(ClampDouble(d, 0.05, 0.85));
        if (ReadBool(json, "showNotifications", b)) ui.showNotifications = b;
        if (ReadBool(json, "chatDetection", b)) ui.chatDetection = b;
        if (ReadBool(json, "portableMode", b)) ui.portableMode = b;
        if (ReadBool(json, "tabFade", b)) ui.tabFade = b;

        config = parsed;
        return true;
    }

    bool WriteFile(const std::string& path, const std::string& content)
    {
        const std::string temp = path + ".tmp";
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            if (!stream)
                return false;
            stream << content;
            if (!stream.good())
                return false;
        }

        std::error_code ec;
        std::filesystem::rename(temp, path, ec);
        if (ec)
        {
            // Rename over an existing file can fail on some filesystems; fall
            // back to a direct write rather than losing the settings.
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream)
                return false;
            stream << content;
            return stream.good();
        }

        return true;
    }

    bool ReadFile(const std::string& path, std::string& content)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return false;

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        content = buffer.str();
        return !content.empty();
    }
}

std::string Settings::Directory()
{
    const char* home = std::getenv("HOME");
    const std::string base = (home != nullptr && *home != '\0') ? home : ".";
    return base + "/Library/Application Support/dribtard";
}

std::string Settings::Path()
{
    return Directory() + "/last.json";
}

std::string Settings::ConfigsDirectory()
{
    return Directory() + "/configs";
}

std::string Settings::SanitizeName(const std::string& name)
{
    const std::string trimmed = Trim(name);

    if (trimmed.empty() || trimmed.size() > kMaxNameLength)
        return "";
    if (trimmed == "." || trimmed == "..")
        return "";

    for (const char c : trimmed)
    {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
        if (!ok)
            return "";
    }

    return trimmed;
}

std::string Settings::ConfigFile(const std::string& name)
{
    const std::string clean = SanitizeName(name);
    if (clean.empty())
        return "";
    return ConfigsDirectory() + "/" + clean + ".json";
}

bool Settings::Save(const AppConfig& config)
{
    std::error_code ec;
    std::filesystem::create_directories(Directory(), ec);
    return WriteFile(Path(), Serialize(config));
}

bool Settings::Load(AppConfig& config)
{
    std::string content;
    if (!ReadFile(Path(), content))
        return false;
    return Parse(content, config);
}

bool Settings::SaveNamed(const std::string& name, const AppConfig& config)
{
    const std::string path = ConfigFile(name);
    if (path.empty())
        return false;

    std::error_code ec;
    std::filesystem::create_directories(ConfigsDirectory(), ec);
    return WriteFile(path, Serialize(config));
}

bool Settings::LoadNamed(const std::string& name, AppConfig& config)
{
    const std::string path = ConfigFile(name);
    if (path.empty())
        return false;

    std::string content;
    if (!ReadFile(path, content))
        return false;
    return Parse(content, config);
}

bool Settings::DeleteNamed(const std::string& name)
{
    const std::string path = ConfigFile(name);
    if (path.empty())
        return false;

    std::error_code ec;
    return std::filesystem::remove(path, ec) && !ec;
}

std::vector<std::string> Settings::ListNamed()
{
    std::vector<std::string> names;
    std::error_code ec;

    const std::filesystem::path directory(ConfigsDirectory());
    if (!std::filesystem::exists(directory, ec))
        return names;

    std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator it(directory, ec);
        !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        if (it->path().extension().string() != ".json")
            continue;

        const std::string stem = it->path().stem().string();
        if (!SanitizeName(stem).empty())
            names.push_back(stem);
    }

    std::sort(names.begin(), names.end());
    return names;
}
