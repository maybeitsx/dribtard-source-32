#include "Macro/MacroEngine.hpp"

#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
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

    struct Event
    {
        bool mouse;
        int x;
        int y;
        bool keyUp;
    };

    struct FakeInput
    {
        std::atomic<int> mouseEvents{ 0 };
        std::atomic<int> keyDownEvents{ 0 };
        int failOnMouse = 0;
        int failOnKeyDown = 0;
        std::mutex mutex;
        std::vector<Event> events;
        std::vector<std::pair<int, int>> cursorSets;

        MacroInputBackend Backend()
        {
            return {
                [this](int key, bool up)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    events.push_back({ false, key, 0, up });
                    if (!up && ++keyDownEvents == failOnKeyDown)
                        return false;
                    return true;
                },
                [this](int dx, int dy)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    events.push_back({ true, dx, dy, false });
                    if (++mouseEvents == failOnMouse)
                        return false;
                    return true;
                },
                [](int& x, int& y)
                {
                    // Desktop cursor starts at a fixed point (not at a screen edge).
                    x = 1600;
                    y = 900;
                    return true;
                },
                [this](int x, int y)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    cursorSets.push_back({ x, y });
                    return true;
                }
            };
        }
    };

    MacroSettings FastSettings()
    {
        MacroSettings settings;
        settings.customCurveAngle = true;
        settings.horizontalDegrees = 30;
        settings.verticalDegrees = 30;
        settings.initialDelaySeconds = 0;
        settings.activationHoldSeconds = 0;
        settings.secondaryDelaySeconds = 0;
        settings.secondaryHoldSeconds = 0;
        return settings;
    }

    void WaitFinished(MacroEngine& engine)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (engine.IsRunning() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        Check(!engine.IsRunning(), "Macro did not finish/stop within two seconds");
        engine.Stop();
    }
}

int main()
{
    try
    {
        {
            // Roblox sensitivity 2.0 halves the emitted counts (1 / 2.0 = 0.5).
            FakeInput input;
            MacroEngine engine(input.Backend());
            MacroSettings settings = FastSettings();
            settings.robloxSensitivity = 2.0;
            engine.SetSettings(settings);
            Check(engine.Start(), "Sensitivity scaling start must succeed");
            WaitFinished(engine);
            Check(input.events.size() == 8 && input.events[0].mouse &&
                input.events[0].x == 75 && input.events[0].y == -100 &&
                input.events[3].mouse && input.events[3].x == -75 && input.events[3].y == 100,
                "Roblox sensitivity 2.0 must scale mouse counts by 0.5");
        }
        {
            // Backwards preset reset must undo the full emitted movement,
            // not truncate its vertical return to a fixed angle.
            FakeInput input;
            MacroEngine engine(input.Backend());
            engine.SetSettings(MacroSettings{});
            Check(engine.Start(), "Backwards preset start must succeed");
            WaitFinished(engine);
            Check(input.events.size() == 8 && input.events[0].mouse &&
                input.events[0].x == 1440 && input.events[0].y == -1067 &&
                input.events[3].mouse && input.events[3].x == -1440 && input.events[3].y == 853,
                "Backwards preset must send the full curve and return only the pitch reached (80 deg)");
        }
        {
            // 180 horizontal and 180 vertical at Roblox sensitivity 0.26: the full
            // curve is sent, and the reset undoes only the 80 deg pitch the camera reached.
            FakeInput input;
            MacroEngine engine(input.Backend());
            MacroSettings settings = FastSettings();
            settings.horizontalDegrees = 180;
            settings.verticalDegrees = 180;
            settings.robloxSensitivity = 0.26;
            engine.SetSettings(settings);
            Check(engine.Start(), "180/180 start must succeed");
            WaitFinished(engine);
            Check(input.events.size() == 8 && input.events[0].mouse &&
                input.events[0].x == 3462 && input.events[0].y == -4615 &&
                input.events[3].mouse && input.events[3].x == -3462 && input.events[3].y == 2051,
                "180/180 at 0.26 must send the full curve and return 80 deg of pitch");
        }
        {
            // The macro runs without any target window or game focus.
            FakeInput input;
            MacroEngine engine(input.Backend());
            engine.SetSettings(FastSettings());
            Check(engine.Start(), "Start must succeed without a target window");
            WaitFinished(engine);
            Check(input.events.size() == 8, "Macro sequence incomplete");
            Check(input.events[0].mouse && input.events[0].x == 240 && input.events[0].y == -320,
                "Initial curve input incorrect");
            Check(!input.events[1].mouse && input.events[1].x == 'Q' && !input.events[1].keyUp &&
                input.events[2].keyUp, "Ability tap incorrect");
            Check(input.events[3].mouse && input.events[3].x == -240 && input.events[3].y == 320,
                "Camera return incorrect");
            Check(input.events[4].x == 'F' && !input.events[4].keyUp && input.events[5].keyUp &&
                input.events[6].x == 'F' && !input.events[6].keyUp && input.events[7].keyUp,
                "Secondary taps incorrect");
        }
        {
            // Smooth movement still completes and restores the camera exactly.
            FakeInput input;
            auto settings = FastSettings();
            settings.instantMode = false;
            settings.rotationDurationSeconds = 0.025;
            MacroEngine engine(input.Backend());
            engine.SetSettings(settings);
            Check(engine.Start(), "Smooth start must succeed");
            WaitFinished(engine);
            int netX = 0;
            int netY = 0;
            for (const auto& event : input.events)
                if (event.mouse) { netX += event.x; netY += event.y; }
            Check(input.mouseEvents.load() >= 3 && netX == 0 && netY == 0 &&
                input.keyDownEvents.load() == 3, "Smooth macro did not complete and fully restore");
        }
        {
            // A failing key press aborts the run instead of continuing blindly.
            FakeInput input;
            input.failOnKeyDown = 2; // Q tap succeeds, the first F tap fails
            MacroEngine engine(input.Backend());
            engine.SetSettings(FastSettings());
            Check(engine.Start(), "Start must succeed");
            WaitFinished(engine);
            Check(input.events.size() == 5 && input.events[4].x == 'F' && !input.events[4].keyUp,
                "Failed key down must abort the run");
        }
        {
            // Emergency stop while the ability is held releases it, using the
            // settings snapshot captured at the start of the run.
            FakeInput input;
            auto settings = FastSettings();
            settings.qBeforeCamera = true;
            settings.activationLeadSeconds = 5;
            MacroEngine engine(input.Backend());
            engine.SetSettings(settings);
            Check(engine.Start(), "Start must succeed");
            const auto deadline = std::chrono::steady_clock::now() + 1s;
            while (input.keyDownEvents.load() == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(1ms);
            Check(input.keyDownEvents.load() == 1, "Worker did not hold the ability key");
            settings.activationKey = 'Z'; // live edits must not change the released key
            engine.SetSettings(settings);
            engine.EmergencyStop();
            WaitFinished(engine);
            Check(input.events.size() == 2 && input.events[0].x == 'Q' && !input.events[0].keyUp &&
                input.events[1].x == 'Q' && input.events[1].keyUp,
                "Stop must release the original held key, not the edited one");
        }
        {
            // A failed key send aborts the run without leaving a stuck key.
            FakeInput input;
            input.failOnKeyDown = 1;
            auto settings = FastSettings();
            settings.qBeforeCamera = true;
            settings.activationLeadSeconds = 5;
            MacroEngine engine(input.Backend());
            engine.SetSettings(settings);
            Check(engine.Start(), "Start must succeed");
            WaitFinished(engine);
            Check(input.events.size() == 1 && input.events[0].x == 'Q' && !input.events[0].keyUp,
                "Failed key send must abort the run");
        }
        {
            // The camera returns exactly once per run: one relative return
            // mid-sequence, and no desktop-cursor teleport at finish (the
            // game sees that teleport as a second camera input).
            FakeInput input;
            MacroEngine engine(input.Backend());
            engine.SetSettings(MacroSettings{});
            Check(engine.Start(), "Single-restore start must succeed");
            WaitFinished(engine);
            std::vector<Event> mouseMoves;
            int netX = 0;
            for (const auto& event : input.events)
                if (event.mouse) { mouseMoves.push_back(event); netX += event.x; }
            Check(mouseMoves.size() == 2,
                "Camera must move exactly twice (outward curve plus one return)");
            Check(netX == 0 && mouseMoves[0].x == -mouseMoves[1].x,
                "Horizontal return must undo the outward move exactly");
            Check(input.cursorSets.empty(),
                "Desktop cursor must not be teleported (that double-resets the camera)");
        }
        {
            // If no camera move was sent, the cursor is not touched.
            FakeInput input;
            input.failOnMouse = 1;
            MacroEngine engine(input.Backend());
            engine.SetSettings(FastSettings());
            Check(engine.Start(), "Cursor no-move start must succeed");
            WaitFinished(engine);
            Check(input.cursorSets.empty(), "Cursor must not be restored when no move was sent");
        }
        std::cout << "Macro sequence, smooth movement, stop cleanup and failure tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
