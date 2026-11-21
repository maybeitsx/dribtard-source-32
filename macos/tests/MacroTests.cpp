#include "Keys.hpp"
#include "Macro/BladeBallMacro.hpp"
#include "Macro/MacroEngine.hpp"
#include "Macro/MoonwalkMacro.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    void Check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    enum class Kind
    {
        Key,
        Mouse
    };

    struct Event
    {
        Kind kind;
        int a;   // key code, or dx
        int b;   // dy
        bool keyUp;
        std::chrono::steady_clock::time_point at;
    };

    struct Recorder
    {
        std::mutex mutex;
        std::vector<Event> events;

        MacroInputBackend Backend()
        {
            return MacroInputBackend{
                [this](int key, bool up)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    events.push_back({ Kind::Key, key, 0, up, std::chrono::steady_clock::now() });
                    return true;
                },
                [this](int dx, int dy)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    events.push_back({ Kind::Mouse, dx, dy, false, std::chrono::steady_clock::now() });
                    return true;
                },
                nullptr,
                nullptr
            };
        }

        std::vector<Event> Snapshot()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return events;
        }

        std::size_t Count()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return events.size();
        }
    };

    void WaitForEvents(Recorder& recorder, std::size_t wanted,
        std::chrono::milliseconds timeout = 3000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (recorder.Count() < wanted && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
    }

    template <typename Engine>
    void WaitUntilIdle(Engine& engine, std::chrono::milliseconds timeout = 3000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (engine.IsRunning() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
    }

    int SumMouse(const std::vector<Event>& events, std::size_t from, std::size_t to)
    {
        int total = 0;
        for (std::size_t i = from; i < to && i < events.size(); ++i)
        {
            if (events[i].kind == Kind::Mouse)
                total += events[i].a;
        }
        return total;
    }

    bool AnyKeyDown(const std::vector<Event>& events)
    {
        int open = 0;
        for (const Event& event : events)
        {
            if (event.kind != Kind::Key)
                continue;
            open += event.keyUp ? -1 : 1;
            Check(open == 0 || open == 1, "Key events out of order");
        }
        return open != 0;
    }

    double MsBetween(
        std::chrono::steady_clock::time_point from,
        std::chrono::steady_clock::time_point to)
    {
        return std::chrono::duration<double, std::milli>(to - from).count();
    }

    int FirstIndex(const std::vector<Event>& events, Kind kind, int value, bool keyUp, int from = 0)
    {
        for (std::size_t i = static_cast<std::size_t>(from); i < events.size(); ++i)
        {
            if (events[i].kind == kind && events[i].a == value && events[i].keyUp == keyUp)
                return static_cast<int>(i);
        }
        return -1;
    }
}

int main()
{
    try
    {
        // Defaults and preset lookup.
        {
            Check(GetCurrentMultiplier(0.625) == 1.6, "1 / sensitivity must scale the flick");
            Check(GetCurrentMultiplier(0.0) == 100.0, "Sensitivity must clamp to 0.01");

            CurveType curve = CurveType::Upwards;
            Check(CurveTypeForHotkey(Keys::Two, curve) && curve == CurveType::Backwards,
                "Hotkey 2 must select Backwards");
            Check(CurveTypeForHotkey(Keys::Keypad3, curve) && curve == CurveType::Left,
                "Keypad 3 must select Left");
            Check(!CurveTypeForHotkey(Keys::Nine, curve),
                "Only 1-4 may switch the curve");

            const CurvePreset& fallback = CurvePresetFor(static_cast<CurveType>(99));
            Check(fallback.horizontalDegrees == 180.0,
                "Out-of-range curve must fall back to Backwards");

            const BladeBallSettings bladeBall{};
            Check(bladeBall.triggerKey == Keys::V && bladeBall.parryKey == Keys::F,
                "Blade Ball defaults must be V and F");
            Check(bladeBall.abilityKey == Keys::Q, "Blade Ball ability must default to Q");
            Check(kParryHoldSeconds == 0.020, "Parry hold must be 20 ms");
            Check(!bladeBall.armed, "Blade Ball must start disarmed");

            const MacroSettings macro{};
            Check(macro.triggerKey == Keys::C && macro.secondaryKey == Keys::F,
                "Structure defaults must be C and F");
            Check(macro.curveType == CurveType::Backwards, "Default curve must be Backwards");
        }

        // Structure sequence: curve, Q tap, camera return, then F twice.
        {
            Recorder recorder;
            MacroEngine engine(recorder.Backend());

            MacroSettings settings;
            settings.customCurveAngle = true;
            settings.horizontalDegrees = 30.0;
            settings.verticalDegrees = 0.0;
            settings.robloxSensitivity = 0.625;
            settings.mouseCountsPerDegree = 5.0;
            settings.instantMode = true;
            settings.initialDelaySeconds = 0.0;
            settings.secondaryDelaySeconds = 0.0;
            settings.activationHoldSeconds = 0.0;
            settings.secondaryHoldSeconds = 0.0;
           settings.restoreCamera = true;
            engine.SetSettings(settings);

            Check(engine.Start(), "Structure macro must start");
            WaitUntilIdle(engine);
            engine.Stop();

            const std::vector<Event> events = recorder.Snapshot();
            Check(!events.empty(), "Structure macro sent nothing");
            Check(events.front().kind == Kind::Mouse && events.front().a == 240,
                "First input must be the 240-count curve");

            const int qDown = FirstIndex(events, Kind::Key, settings.activationKey, false);
            const int firstReturn = FirstIndex(events, Kind::Mouse, -240, false);
            const int firstBlock = FirstIndex(events, Kind::Key, settings.secondaryKey, false);
            const int secondBlock = FirstIndex(events, Kind::Key, settings.secondaryKey, false,
                firstBlock + 1);

            Check(qDown >= 0, "Ability key was never pressed");
            Check(firstReturn > qDown, "Camera return must follow the ability tap");
            Check(firstBlock > firstReturn, "Block taps must follow the camera return");
            Check(secondBlock > firstBlock, "Block key must be tapped twice");
            Check(SumMouse(events, 0, events.size()) == 0,
                "Camera return must undo the curve exactly");
            Check(!AnyKeyDown(events), "Structure macro left a key held");
        }

        // Moonwalk loop taps A and D without ever holding both.
        {
            Recorder recorder;
            MoonwalkEngine engine(recorder.Backend());

            Check(engine.Start(), "Moonwalk must start");
            WaitForEvents(recorder, 6);
            engine.Stop();

            const std::vector<Event> events = recorder.Snapshot();
            Check(events.size() >= 6, "Moonwalk did not repeat");
            Check(!AnyKeyDown(events), "Moonwalk left a key held");

            const int aTaps = FirstIndex(events, Kind::Key, Keys::A, false) >= 0;
            const int dTaps = FirstIndex(events, Kind::Key, Keys::D, false) >= 0;
            Check(aTaps && dTaps, "Moonwalk must tap both directions");
        }

        // Blade Ball: sweep first, parry at the end, hold 20 ms, return.
        {
            Recorder recorder;
            BladeBallEngine engine(recorder.Backend());

            BladeBallSettings settings;
            settings.horizontalDegrees = 30.0;
            settings.verticalDegrees = 0.0;
            settings.robloxSensitivity = 0.625;
            settings.mouseCountsPerDegree = 5.0;
            settings.flickDurationSeconds = 0.08;
            settings.jitterMs = 0;
            settings.abilityEnabled = false;
            engine.SetSettings(settings);

            Check(engine.StartParry(), "Curve parry must start");
            WaitUntilIdle(engine);
            engine.Stop();

            const std::vector<Event> events = recorder.Snapshot();
            const int parryDown = FirstIndex(events, Kind::Key, settings.parryKey, false);
            const int parryUp = FirstIndex(events, Kind::Key, settings.parryKey, true);
            const int firstReturn = FirstIndex(events, Kind::Mouse, -240, false);

            Check(parryDown >= 0 && parryUp > parryDown, "Parry must be pressed and released");
            Check(SumMouse(events, 0, static_cast<std::size_t>(parryDown)) == 240,
                "The parry must be pressed after the full sweep");
            Check(firstReturn > parryDown, "Camera return must follow the parry");
            Check(SumMouse(events, 0, events.size()) == 0,
                "Blade Ball must undo the flick exactly");
            Check(MsBetween(events[parryDown].at, events[parryUp].at) >= 15.0,
                "Parry hold must last about 20 ms");
            Check(engine.ParryCount() == 1, "Exactly one parry must be counted");
            Check(!AnyKeyDown(events), "Curve parry left a key held");
        }

        // Blade Ball ability follow-up lands after the camera return.
        {
            Recorder recorder;
            BladeBallEngine engine(recorder.Backend());

            BladeBallSettings settings;
            settings.flickDurationSeconds = 0.02;
            settings.jitterMs = 0;
            settings.abilityEnabled = true;
            settings.abilityDelaySeconds = 0.01;
            engine.SetSettings(settings);

            Check(engine.StartParry(), "Ability parry must start");
            WaitUntilIdle(engine);
            engine.Stop();

            const std::vector<Event> events = recorder.Snapshot();
            const int abilityDown = FirstIndex(events, Kind::Key, settings.abilityKey, false);
            const int lastReturn = FirstIndex(events, Kind::Mouse, -240, false);
            Check(abilityDown > lastReturn && lastReturn >= 0,
                "Ability must be tapped after the camera return");
        }

        // Licence gate and concurrent starts.
        {
            Recorder recorder;
            BladeBallEngine engine(recorder.Backend());
            engine.SetLicenseValidator([]() { return false; });

            Check(!engine.StartParry(), "Curve parry must refuse an invalid license");
            Check(recorder.Count() == 0, "A refused start must not send input");

            Recorder second;
            BladeBallEngine running(second.Backend());
            BladeBallSettings slow;
            slow.flickDurationSeconds = 0.4;
            slow.jitterMs = 0;
            running.SetSettings(slow);

            Check(running.StartParry(), "Slow parry must start");
            Check(!running.StartParry(), "A second parry must be refused while one runs");
            running.Stop();
            Check(!running.IsRunning(), "Stop must end the parry");
        }

        std::cout << "macOS engine tests passed (curve, moonwalk, blade ball, gates)\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
