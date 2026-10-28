#include "SettingsManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

#include <filesystem>

namespace
{

constexpr int kConfigVersion = 1;
constexpr std::size_t kMaxNameLength = 48;

double ClampDouble(double v, double lo, double hi)
{
    if (!std::isfinite(v))
        return lo;
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

int ClampInt(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

std::string Trim(const std::string& s)
{
    std::size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])))
        ++begin;
    std::size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(begin, end - begin);
}

// --- Minimal JSON field lookup -------------------------------------------
// The writer controls the format, so the reader just searches for
// "key" : <value> anywhere in the document (nesting independent).

const char* SkipWs(const char* p, const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        ++p;
    return p;
}

bool FindValueBegin(const std::string& body, const std::string& key, const char*& out, const char*& end)
{
    const std::string quoted = "\"" + key + "\"";
    const char* begin = body.data();
    const char* docEnd = begin + body.size();
    const char* pos = std::search(begin, docEnd, quoted.data(), quoted.data() + quoted.size());
    if (pos == docEnd)
        return false;
    const char* p = SkipWs(pos + quoted.size(), docEnd);
    if (p == docEnd || *p != ':')
        return false;
    p = SkipWs(p + 1, docEnd);
    if (p == docEnd)
        return false;
    out = p;
    end = docEnd;
    return true;
}

bool ReadBool(const std::string& body, const std::string& key, bool& out)
{
    const char* p = nullptr;
    const char* end = nullptr;
    if (!FindValueBegin(body, key, p, end))
        return false;
    if (end - p >= 4 && std::string(p, 4) == "true")
    {
        out = true;
        return true;
    }
    if (end - p >= 5 && std::string(p, 5) == "false")
    {
        out = false;
        return true;
    }
    return false;
}

bool ReadInt(const std::string& body, const std::string& key, int& out)
{
    const char* p = nullptr;
    const char* end = nullptr;
    if (!FindValueBegin(body, key, p, end))
        return false;
    char* stop = nullptr;
    const long v = std::strtol(p, &stop, 10);
    if (stop == p)
        return false;
    out = static_cast<int>(v);
    return true;
}

bool ReadDouble(const std::string& body, const std::string& key, double& out)
{
    const char* p = nullptr;
    const char* end = nullptr;
    if (!FindValueBegin(body, key, p, end))
        return false;
    char* stop = nullptr;
    const double v = std::strtod(p, &stop);
    if (stop == p || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

bool ReadString(const std::string& body, const std::string& key, std::string& out)
{
    const char* p = nullptr;
    const char* end = nullptr;
    if (!FindValueBegin(body, key, p, end))
        return false;
    if (p == end || *p != '"')
        return false;
    ++p;
    std::string result;
    while (p < end)
    {
        if (*p == '\\' && p + 1 < end)
        {
            ++p;
            switch (*p)
            {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            default: result += *p; break;
            }
            ++p;
        }
        else if (*p == '"')
        {
            out = result;
            return true;
        }
        else
        {
            result += *p;
            ++p;
        }
    }
    return false;
}

bool WriteFile(const std::string& path, const std::string& content)
{
    std::error_code ec;
    const std::string parent = std::filesystem::path(path).parent_path().string();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
}

bool ReadFile(const std::string& path, std::string& content)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    content = ss.str();
    return true;
}

} // namespace

SettingsManager& SettingsManager::Get()
{
    static SettingsManager instance;

    return instance;
}

std::string SettingsManager::JsonEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 2);
    for (const char c : value)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string SettingsManager::ToJson(const AppConfig& config)
{
    const MacroSettings& m = config.macro;
    const UiSettings& u = config.ui;

    char buf[64];
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"version\": " << kConfigVersion << ",\n";
    ss << "  \"targetProcessName\": \"" << JsonEscape(config.targetProcessName) << "\",\n";
    ss << "  \"macro\": {\n";

    auto dbl = [&](const char* key, double v) {
        std::snprintf(buf, sizeof(buf), "%.6f", v);
        ss << "    \"" << key << "\": " << buf;
    };

    ss << "    \"triggerKey\": " << m.triggerKey << ",\n";
    ss << "    \"activationKey\": " << m.activationKey << ",\n";
    ss << "    \"secondaryKey\": " << m.secondaryKey << ",\n";
    ss << "    \"curveType\": " << static_cast<int>(m.curveType) << ",\n";
    ss << "    \"customCurveAngle\": " << (m.customCurveAngle ? "true" : "false") << ",\n";
    ss << "    \"hotkeyMode\": " << (m.hotkeyMode ? "true" : "false") << ",\n";
    dbl("horizontalDegrees", m.horizontalDegrees); ss << ",\n";
    dbl("verticalDegrees", m.verticalDegrees); ss << ",\n";
    dbl("rotationDurationSeconds", m.rotationDurationSeconds); ss << ",\n";
    dbl("robloxSensitivity", m.robloxSensitivity); ss << ",\n";
    dbl("mouseCountsPerDegree", m.mouseCountsPerDegree); ss << ",\n";
    ss << "    \"qBeforeCamera\": " << (m.qBeforeCamera ? "true" : "false") << ",\n";
    dbl("activationHoldSeconds", m.activationHoldSeconds); ss << ",\n";
    dbl("activationLeadSeconds", m.activationLeadSeconds); ss << ",\n";
    dbl("initialDelaySeconds", m.initialDelaySeconds); ss << ",\n";
    dbl("secondaryDelaySeconds", m.secondaryDelaySeconds); ss << ",\n";
    dbl("secondaryHoldSeconds", m.secondaryHoldSeconds); ss << ",\n";
    ss << "    \"instantMode\": " << (m.instantMode ? "true" : "false") << ",\n";
    ss << "    \"restoreCamera\": " << (m.restoreCamera ? "true" : "false") << "\n";
    ss << "  },\n";
    ss << "  \"moonwalk\": {\n";
    ss << "    \"holdKey\": " << config.moonwalk.holdKey << "\n";
    ss << "  },\n";
    ss << "  \"ui\": {\n";
    ss << "    \"toggleKey\": " << u.toggleKey << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.accentR));
    ss << "    \"accentR\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.accentG));
    ss << "    \"accentG\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.accentB));
    ss << "    \"accentB\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.accentA));
    ss << "    \"accentA\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.cardR));
    ss << "    \"cardR\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.cardG));
    ss << "    \"cardG\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.cardB));
    ss << "    \"cardB\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.glowIntensity));
    ss << "    \"glowIntensity\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.windowGlowIntensity));
    ss << "    \"windowGlowIntensity\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.panelOpacity));
    ss << "    \"panelOpacity\": " << buf << ",\n";
    ss << "    \"darkenBackground\": " << (u.darkenBackground ? "true" : "false") << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(u.dimOpacity));
    ss << "    \"dimOpacity\": " << buf << ",\n";
    ss << "    \"limitMenuFps\": " << (u.limitMenuFps ? "true" : "false") << ",\n";
    ss << "    \"menuFpsCap\": " << u.menuFpsCap << "\n";
    ss << "  }\n";
    ss << "}\n";
    return ss.str();
}

bool SettingsManager::FromJson(const std::string& json, AppConfig& out)
{
    int version = 0;
    if (!ReadInt(json, "version", version) || version != kConfigVersion)
        return false;

    AppConfig parsed;

    std::string target;
    if (ReadString(json, "targetProcessName", target) && !Trim(target).empty())
        parsed.targetProcessName = Trim(target).substr(0, 64);

    MacroSettings& m = parsed.macro;
    int v = 0;
    double d = 0.0;
    bool b = false;

    if (ReadInt(json, "triggerKey", v))
        m.triggerKey = ClampInt(v, 1, 255);
    if (ReadInt(json, "activationKey", v))
        m.activationKey = ClampInt(v, 1, 255);
    if (ReadInt(json, "secondaryKey", v))
        m.secondaryKey = ClampInt(v, 1, 255);
    if (ReadInt(json, "curveType", v) && v >= 0 && v <= 3)
        m.curveType = static_cast<CurveType>(v);
    if (ReadBool(json, "customCurveAngle", b))
        m.customCurveAngle = b;
    if (ReadBool(json, "hotkeyMode", b))
        m.hotkeyMode = b;
    if (ReadDouble(json, "horizontalDegrees", d))
        m.horizontalDegrees = ClampDouble(d, -180.0, 180.0);
    if (ReadDouble(json, "verticalDegrees", d))
        m.verticalDegrees = ClampDouble(d, -180.0, 180.0);
    if (ReadDouble(json, "rotationDurationSeconds", d))
        m.rotationDurationSeconds = ClampDouble(d, 0.0, 10.0);
    if (ReadDouble(json, "robloxSensitivity", d))
        m.robloxSensitivity = ClampDouble(d, 0.01, 10.0);
    if (ReadDouble(json, "mouseCountsPerDegree", d))
        m.mouseCountsPerDegree = ClampDouble(d, 0.1, 100.0);
    if (ReadBool(json, "qBeforeCamera", b))
        m.qBeforeCamera = b;
    if (ReadDouble(json, "activationHoldSeconds", d))
        m.activationHoldSeconds = ClampDouble(d, 0.0, 10.0);
    if (ReadDouble(json, "activationLeadSeconds", d))
        m.activationLeadSeconds = ClampDouble(d, 0.0, 10.0);
    if (ReadDouble(json, "initialDelaySeconds", d))
        m.initialDelaySeconds = ClampDouble(d, 0.0, 30.0);
    if (ReadDouble(json, "secondaryDelaySeconds", d))
        m.secondaryDelaySeconds = ClampDouble(d, 0.0, 30.0);
    if (ReadDouble(json, "secondaryHoldSeconds", d))
        m.secondaryHoldSeconds = ClampDouble(d, 0.0, 10.0);
    if (ReadBool(json, "instantMode", b))
        m.instantMode = b;
    if (ReadBool(json, "restoreCamera", b))
        m.restoreCamera = b;

    if (ReadInt(json, "holdKey", v))
        parsed.moonwalk.holdKey = ClampInt(v, 1, 255);

    UiSettings& u = parsed.ui;
    if (ReadInt(json, "toggleKey", v))
        u.toggleKey = ClampInt(v, 1, 255);
    if (ReadDouble(json, "accentR", d))
        u.accentR = static_cast<float>(ClampDouble(d, 0.0, 1.0));
    if (ReadDouble(json, "accentG", d))
        u.accentG = static_cast<float>(ClampDouble(d, 0.0, 1.0));
    if (ReadDouble(json, "accentB", d))
        u.accentB = static_cast<float>(ClampDouble(d, 0.0, 1.0));
    if (ReadDouble(json, "accentA", d))
        u.accentA = static_cast<float>(ClampDouble(d, 0.0, 1.0));
    if (ReadDouble(json, "cardR", d))
        u.cardR = static_cast<float>(ClampDouble(d, 0.0, 1.0));
    if (ReadDouble(json, "cardG", d))
        u.cardG = static_cast<float>(ClampDouble(d, 0.0, 1.0));
    if (ReadDouble(json, "cardB", d))
        u.cardB = static_cast<float>(ClampDouble(d, 0.0, 1.0));
    if (ReadDouble(json, "glowIntensity", d))
        u.glowIntensity = static_cast<float>(ClampDouble(d, 0.0, 1.5));
    if (ReadDouble(json, "windowGlowIntensity", d))
        u.windowGlowIntensity = static_cast<float>(ClampDouble(d, 0.0, 1.5));
    if (ReadDouble(json, "panelOpacity", d))
        u.panelOpacity = static_cast<float>(ClampDouble(d, 0.30, 1.0));
    if (ReadBool(json, "darkenBackground", b))
        u.darkenBackground = b;
    if (ReadDouble(json, "dimOpacity", d))
        u.dimOpacity = static_cast<float>(ClampDouble(d, 0.05, 0.85));
    if (ReadBool(json, "limitMenuFps", b))
        u.limitMenuFps = b;
    if (ReadInt(json, "menuFpsCap", v))
        u.menuFpsCap = ClampInt(v, 10, 480);

    out = parsed;
    return true;
}

std::string SettingsManager::ConfigDir()
{
    char overrideDir[1024]{};
#ifdef _WIN32
    if (GetEnvironmentVariableA("DRIBTARD_CONFIG_DIR", overrideDir,
        static_cast<DWORD>(sizeof(overrideDir))) > 0)
        return std::string(overrideDir);
#else
    if (const char* env = std::getenv("DRIBTARD_CONFIG_DIR"))
    {
        if (env[0] != '\0')
            return std::string(env);
    }
#endif
#ifdef _WIN32
    char appData[260]{};
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData)))
        return ".";
    return std::string(appData) + "\\dribtard";
#else
    if (const char* home = std::getenv("HOME"))
        return std::string(home) + "/.dribtard";
    return ".";
#endif
}

std::string SettingsManager::LastConfigPath()
{
#ifdef _WIN32
    return ConfigDir() + "\\last.json";
#else
    return ConfigDir() + "/last.json";
#endif
}

std::string SettingsManager::ConfigsDir()
{
#ifdef _WIN32
    return ConfigDir() + "\\configs";
#else
    return ConfigDir() + "/configs";
#endif
}

std::string SettingsManager::SanitizeName(const std::string& name)
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

std::string SettingsManager::ConfigFilePath(const std::string& name)
{
    const std::string clean = SanitizeName(name);
#ifdef _WIN32
    return ConfigsDir() + "\\" + clean + ".json";
#else
    return ConfigsDir() + "/" + clean + ".json";
#endif
}

bool SettingsManager::SaveLast(const AppConfig& config)
{
    return WriteFile(LastConfigPath(), ToJson(config));
}

bool SettingsManager::LoadLast(AppConfig& out)
{
    std::string content;
    if (!ReadFile(LastConfigPath(), content))
        return false;
    return FromJson(content, out);
}

bool SettingsManager::SaveNamed(const std::string& name, const AppConfig& config)
{
    if (SanitizeName(name).empty())
        return false;
    return WriteFile(ConfigFilePath(name), ToJson(config));
}

bool SettingsManager::LoadNamed(const std::string& name, AppConfig& out)
{
    if (SanitizeName(name).empty())
        return false;
    std::string content;
    if (!ReadFile(ConfigFilePath(name), content))
        return false;
    return FromJson(content, out);
}

bool SettingsManager::DeleteNamed(const std::string& name)
{
    if (SanitizeName(name).empty())
        return false;
    std::error_code ec;
    return std::filesystem::remove(ConfigFilePath(name), ec) && !ec;
}

std::vector<std::string> SettingsManager::ListConfigs()
{
    std::vector<std::string> names;
    std::error_code ec;
    const std::filesystem::path dir(ConfigsDir());
    if (!std::filesystem::exists(dir, ec))
        return names;
    std::filesystem::directory_iterator endIt;
    for (std::filesystem::directory_iterator it(dir, ec); !ec && it != endIt; it.increment(ec))
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
