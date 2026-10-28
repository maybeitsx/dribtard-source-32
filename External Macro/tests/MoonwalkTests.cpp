#include "Macro/MoonwalkMacro.hpp"

#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
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
        int key;
        bool keyUp;
        std::chrono::steady_clock::time_point at;
    };

    struct FakeKeys
    {
        std::mutex mutex;
        std::vector<Event> events;

        MacroInputBackend Backend()
        {
            return {
                [this](int key, bool up)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    events.push_back({ key, up, std::chrono::steady_clock::now() });
                    return true;
                },
                nullptr,
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

    void WaitForEvents(FakeKeys& keys, std::size_t wanted)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (keys.Count() < wanted && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
    }

    double MsBetween(
        std::chrono::steady_clock::time_point from,
        std::chrono::steady_clock::time_point to)
    {
        return std::chrono::duration<double, std::milli>(to - from).count();
    }
}

int main()
{
    try
    {
        // Defaults match the requested tech: hold X and the loop taps A/D.
        {
            MoonwalkSettings settings;
            Check(settings.holdKey == 'X', "Default moonwalk hold key must be X");
            Check(kMoonwalkTapHoldMs == 55, "Tap hold must be 55 ms");
            Check(kMoonwalkGapMs == 5, "A/D gap must be 5 ms");
        }

        // The loop repeats A down, A up, D down, D up while held, and the
        // durations match the requested 55 ms taps with a 5 ms gap.
        {
            FakeKeys keys;
            MoonwalkEngine engine(keys.Backend());
            Check(engine.Start(), "Moonwalk start must succeed");

            WaitForEvents(keys, 12);
            engine.Stop();
            Check(!engine.IsRunning(), "Moonwalk must stop when asked");

            const std::vector<Event> events = keys.Snapshot();
            Check(events.size() >= 8, "Moonwalk did not repeat its sequence");

            for (std::size_t i = 0; i < events.size(); ++i)
            {
                const std::size_t step = i % 4;
                if (step == 0)
                    Check(events[i].key == 'A' && !events[i].keyUp, "Expected A down");
                else if (step == 1)
                    Check(events[i].key == 'A' && events[i].keyUp, "Expected A up");
                else if (step == 2)
                    Check(events[i].key == 'D' && !events[i].keyUp, "Expected D down");
                else
                    Check(events[i].key == 'D' && events[i].keyUp, "Expected D up");
            }

            // Whole A/D pairs are always complete, and no key is left down.
            for (int key : { 'A', 'D' })
            {
                int open = 0;
                for (const Event& event : events)
                {
                    if (event.key != key)
                        continue;
                    open += event.keyUp ? -1 : 1;
                    Check(open == 0 || open == 1, "Key events out of order");
                }
                Check(open == 0, "Moonwalk left a key held after stopping");
            }

            // Stop can interrupt the final hold, so skip the last cycle when
            // checking hold durations; the earlier cycles are uninterrupted.
            for (std::size_t i = 0; i + 3 < events.size(); ++i)
            {
                if (i % 4 == 0)
                {
                    // A is held for about 55 ms.
                    const double held = MsBetween(events[i].at, events[i + 1].at);
                    Check(held >= 40.0 && held <= 400.0,
                        "A tap hold must be about 55 ms");
                }
                else if (i % 4 == 1)
                {
                    // A is released before D goes down, roughly 5 ms apart.
                    const double gap = MsBetween(events[i].at, events[i + 1].at);
                    Check(gap >= 1.0 && gap <= 100.0,
                        "Gap between A and D must be about 5 ms");
                }
                else if (i % 4 == 2)
                {
                    const double held = MsBetween(events[i].at, events[i + 1].at);
                    Check(held >= 40.0 && held <= 400.0,
                        "D tap hold must be about 55 ms");
                }
            }
        }

        // Stopping in the middle of a hold releases the held direction.
        {
            FakeKeys keys;
            MoonwalkEngine engine(keys.Backend());
            Check(engine.Start(), "Moonwalk start must succeed");

            WaitForEvents(keys, 1);
            const std::vector<Event> started = keys.Snapshot();
            Check(started.size() == 1 && started[0].key == 'A' && !started[0].keyUp,
                "Loop must begin by holding A");

            engine.EmergencyStop();
            engine.Stop();

            const std::vector<Event> events = keys.Snapshot();
            Check(events.size() == 2 && events[1].key == 'A' && events[1].keyUp,
                "Emergency stop must release the held key");
        }

        // The license gate blocks the loop exactly like the main macro.
        {
            FakeKeys keys;
            MoonwalkEngine engine(keys.Backend());
            engine.SetLicenseValidator([]() { return false; });
            Check(!engine.Start(), "Moonwalk must refuse to start without a license");
            Check(!engine.IsRunning(), "Blocked moonwalk must not run");
            Check(keys.Count() == 0, "Blocked moonwalk must not send input");
        }

        // A single Start per hold: a second Start while running is refused.
        {
            FakeKeys keys;
            MoonwalkEngine engine(keys.Backend());
            Check(engine.Start(), "Moonwalk start must succeed");
            Check(!engine.Start(), "Moonwalk must not start twice");
            engine.Stop();
        }

        std::cout << "Moonwalk hold sequence, timing, stop cleanup and license tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
