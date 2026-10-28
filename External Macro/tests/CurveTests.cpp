#include "Macro/MacroEngine.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        MacroSettings settings;
        Check(!settings.customCurveAngle && settings.curveType == CurveType::Backwards,
            "Default mode must use Backwards preset");
        Check(settings.rotationDurationSeconds == 0.10 && settings.mouseCountsPerDegree == 5.0 &&
            settings.secondaryHoldSeconds == 0.0, "Hidden defaults changed");
        Check(!settings.hotkeyMode, "Hotkey mode must be off by default");

        CurveType mapped = CurveType::Backwards;
        Check(CurveTypeForHotkey('1', mapped) && mapped == CurveType::Upwards,
            "Hotkey 1 must select Upwards");
        Check(CurveTypeForHotkey('2', mapped) && mapped == CurveType::Backwards,
            "Hotkey 2 must select Backwards");
        Check(CurveTypeForHotkey('3', mapped) && mapped == CurveType::Left,
            "Hotkey 3 must select Left");
        Check(CurveTypeForHotkey('4', mapped) && mapped == CurveType::Right,
            "Hotkey 4 must select Right");
        Check(CurveTypeForHotkey(VK_NUMPAD1, mapped) && mapped == CurveType::Upwards &&
            CurveTypeForHotkey(VK_NUMPAD2, mapped) && mapped == CurveType::Backwards &&
            CurveTypeForHotkey(VK_NUMPAD3, mapped) && mapped == CurveType::Left &&
            CurveTypeForHotkey(VK_NUMPAD4, mapped) && mapped == CurveType::Right,
            "Numpad 1-4 must select the same curves");
        Check(!CurveTypeForHotkey('5', mapped) && !CurveTypeForHotkey('C', mapped) &&
            !CurveTypeForHotkey(VK_F12, mapped),
            "Non-curve keys must not map to a curve");

        const double horizontal[] = { 30.0, 180.0, 80.0, -80.0 };
        const double vertical[] = { 170.0, 100.0, 30.0, 30.0 };
        settings.horizontalDegrees = 42.25;
        settings.verticalDegrees = -21.75;
        settings.secondaryDelaySeconds = 0.617;
        settings.rotationDurationSeconds = 0.234;
        settings.secondaryHoldSeconds = 0.043;
        for (int i = 0; i < 4; ++i)
        {
            settings.curveType = static_cast<CurveType>(i);
            const auto effective = GetEffectiveCurveSettings(settings);
            Check(effective.horizontalDegrees == horizontal[i] && effective.verticalDegrees == vertical[i] &&
                effective.secondaryDelaySeconds == 0.350, "Preset values incorrect");
            Check(effective.rotationDurationSeconds == 0.234 &&
                effective.secondaryHoldSeconds == 0.043 && effective.initialDelaySeconds == 0.10 &&
                effective.robloxSensitivity == 0.625, "Preset altered unrelated settings");
        }
        Check(settings.horizontalDegrees == 42.25 && settings.verticalDegrees == -21.75 &&
            settings.secondaryDelaySeconds == 0.617, "Presets destroyed stored custom values");
        settings.customCurveAngle = true;
        const auto custom = GetEffectiveCurveSettings(settings);
        Check(custom.horizontalDegrees == 42.25 && custom.verticalDegrees == -21.75 &&
            custom.secondaryDelaySeconds == 0.617, "Custom mode ignored custom values");

        settings = MacroSettings{};
        Check(settings.curveType == CurveType::Backwards, "Return tests assume the Backwards default");
        // Backwards must reverse all emitted counts, including the full 100-degree preset.
        Check(GetVerticalReturnCounts(settings, -1360) == 1360, "Backwards return must fully reverse emitted counts");
        Check(GetVerticalReturnCounts(settings, -800) == 800, "Backwards 100V must return to its starting counts");
        Check(GetVerticalReturnCounts(settings, -400) == 400, "Partial input must fully reverse");
        Check(GetVerticalReturnCounts(settings, 1360) == -1360, "Positive vertical input must reverse direction");
        Check(GetVerticalReturnCounts(settings, -137) == 137, "Partial input must not over-restore");
        Check(GetVerticalReturnCounts(settings, 0) == 0, "Zero input must not move camera");

        // No curve type should silently discard some of the inverse movement.
        settings = MacroSettings{};
        for (CurveType type : { CurveType::Backwards, CurveType::Upwards, CurveType::Right, CurveType::Left })
        {
            settings.curveType = type;
            for (int sentY : { -100000, -800, -1, 0, 1, 800, 100000 })
                Check(GetVerticalReturnCounts(settings, sentY) == -sentY,
                    "Camera return must exactly reverse emitted vertical counts");
        }

        // Every curve returns its full movement.
        for (CurveType type : { CurveType::Upwards, CurveType::Right, CurveType::Left })
        {
            settings = MacroSettings{};
            settings.curveType = type;
            Check(GetVerticalReturnCounts(settings, -1360) == 1360 &&
                GetVerticalReturnCounts(settings, 1360) == -1360,
                "Curves must fully reverse emitted movement");
        }
        // CurrentMultiplier = 1 / Roblox sensitivity, with the input clamped to 0.01.
        Check(std::fabs(GetCurrentMultiplier(0.625) - 1.6) < 1e-9, "0.625 must give the old 1.6 multiplier");
        Check(std::fabs(GetCurrentMultiplier(2.0) - 0.5) < 1e-9, "Sensitivity 2.0 must halve the multiplier");
        Check(std::fabs(GetCurrentMultiplier(0.0) - 100.0) < 1e-9, "Zero sensitivity must clamp to 0.01");
        Check(std::fabs(GetCurrentMultiplier(-3.0) - 100.0) < 1e-9, "Negative sensitivity must clamp to 0.01");
        Check(std::fabs(GetCurrentMultiplier(std::nan("")) - 100.0) < 1e-9, "NaN sensitivity must clamp to 0.01");
        Check(std::isfinite(GetCurrentMultiplier(0.0)), "Zero sensitivity must not produce infinity");
        std::cout << "Curve settings and calibrated camera return tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
