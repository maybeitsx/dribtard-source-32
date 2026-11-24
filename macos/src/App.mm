#include "App.hpp"

#include "Configuration/Settings.hpp"
#include "Keys.hpp"
#include "Macro/BladeBallMacro.hpp"
#include "Macro/ClickerMacro.hpp"
#include "Macro/MacroEngine.hpp"
#include "Overlay/Overlay.hpp"
#include "Platform/Hotkeys.hpp"
#include "Platform/MacInput.hpp"
#include "UI/Menu.hpp"

#import <Cocoa/Cocoa.h>
#import <dispatch/dispatch.h>

#include <chrono>
#include <cstring>
#include <memory>

namespace
{
    /*
        One process-wide core. The AppKit delegate owns it for the lifetime of
        the run loop, and the overlay frame callback drives it.

        Licence gate: the Windows build validates a signed key against the
        licence server. This build ships ungated, so Licensed() is the single
        place to add that check later; every start path already asks it.
    */
    struct AppCore
    {
        AppConfig config{};
        Menu menu{};
        MacroEngine structure{};
        ClickerEngine clicker{};
        BladeBallEngine bladeBall{};
        HotkeyMonitor hotkeys{};
        Overlay overlay{};

        AppConfig lastSaved{};
        std::chrono::steady_clock::time_point lastSave{};
        std::chrono::steady_clock::time_point lastTapAttempt{};
        bool clickerBlockedUntilRelease = false;
        bool clickerLatched = false;
        bool clickerKeyWasDown = false;
        // Chat detection: true while the in-game chat is open ("/" until
        // Enter). Every macro keybind is suspended while it is set.
        bool chatActive = false;
        int clickerSampleClicks = 0;
        float clickerMeasuredCps = -1.0f;
        std::chrono::steady_clock::time_point clickerSampleAt{};
        std::chrono::steady_clock::time_point clickerUnfocusedSince{};
        std::chrono::steady_clock::time_point clickerKeyUpSince{};
        bool started = false;
        bool quit = false;
        bool m_Terminating = false;

        bool Licensed() const { return true; }

        bool Start()
        {
            // A missing or foreign file leaves the defaults in place.
            Settings::Load(config);
            lastSaved = config;

            if (!MacInput::IsTrusted())
                MacInput::RequestTrust();

            if (!overlay.Init([this]() { Frame(); }))
                return false;

            // Global hotkeys are optional: without Accessibility permission
            // the tap cannot be created, and the menu still works.
            hotkeys.Start();

            started = true;
            return true;
        }

        void Shutdown()
        {
            if (!started)
                return;

            started = false;

            structure.Stop();
            clicker.Stop();
            bladeBall.Stop();
            hotkeys.Stop();

            Settings::Save(config);

            overlay.Shutdown();
        }

        void HandleKey(int keyCode)
        {
            // Emergency stop works even while a key is being rebound.
            if (keyCode == Keys::F12)
            {
                structure.EmergencyStop();
                clicker.EmergencyStop();
                bladeBall.EmergencyStop();
                clickerBlockedUntilRelease = true;
                clickerLatched = false;
                return;
            }

            // While the menu is rebinding, the next key goes to the setting.
            if (menu.IsCapturingKey())
            {
                menu.CaptureKey(keyCode, config);
                return;
            }

            /*
                Chat detection. "/" opens the in-game chat, so every macro
                keybind is suspended until Enter sends the message or Escape
                closes it. Only the game window counts, so typing "/" or Enter
                in another app never pauses the macro.
            */
            if (config.ui.chatDetection && MacInput::GameFocused())
            {
                if (keyCode == Keys::Slash)
                {
                    chatActive = true;
                    return;
                }
                if (keyCode == Keys::Return)
                {
                    chatActive = false;
                    return;
                }
                /*
                    Escape closes the chat without sending, so it must hand
                    the keybinds back too. The check only runs while the chat
                    is open, so a macro key bound to Escape still works the
                    rest of the time.
                */
                if (chatActive && keyCode == Keys::Escape)
                {
                    chatActive = false;
                    return;
                }
            }

            // The menu key is checked before every other shortcut, so a key
            // bound to it always toggles the panel.
            if (keyCode == config.ui.menuKey)
            {
                menu.SetVisible(!menu.Visible());
                return;
            }

            // The in-game chat owns the keyboard until Enter or Escape: no
            // macro keybind may fire while the user is typing a message.
            if (chatActive)
                return;

            if (config.bladeBall.armed && keyCode == config.bladeBall.triggerKey)
            {
                TryStartBladeBall();
                return;
            }

            if (config.bladeBall.armed && config.bladeBall.hotkeyMode &&
                keyCode != config.bladeBall.triggerKey)
            {
                CurveType curve{};
                if (CurveTypeForHotkey(keyCode, curve))
                {
                    config.bladeBall.curveType = curve;
                    config.bladeBall.customCurveAngle = false;
                    menu.ShowNotice(std::string(ParryCurvePresetFor(curve).name) + " parry");
                    return;
                }
            }

            if (keyCode == config.macro.triggerKey)
            {
                // Armed means Blade Ball owns the curve; the Structure
                // trigger stays quiet instead of firing a second curve.
                if (!config.bladeBall.armed)
                    TryStartStructure();
                return;
            }

            if (config.macro.hotkeyMode)
            {
                CurveType curve{};
                if (CurveTypeForHotkey(keyCode, curve))
                {
                    config.macro.curveType = curve;
                    config.macro.customCurveAngle = false;
                    menu.ShowNotice(CurvePresetFor(curve).name);
                }
            }
        }

        void TryStartStructure()
        {
            if (config.bladeBall.armed || !Licensed())
                return;
            if (structure.IsRunning() || clicker.IsRunning() || bladeBall.IsRunning())
                return;
            if (!MacInput::GameRunning() || !MacInput::GameFocused())
                return;

            structure.SetSettings(config.macro);
            structure.Start();
        }

        void TryStartBladeBall()
        {
            if (!Licensed())
                return;
            if (structure.IsRunning() || bladeBall.IsRunning())
                return;
            if (!MacInput::GameRunning() || !MacInput::GameFocused())
                return;

            bladeBall.SetSettings(config.bladeBall);
            bladeBall.StartParry();
        }

        void Frame()
        {
            if (!started)
                return;

            /*
                The tap cannot be created before Accessibility permission is
                granted, and macOS normally grants it after the app has already
                launched. Retrying here is what makes the menu key (and every
                rebind) start working the moment the user allows it, instead of
                staying dead until the next launch.
            */
            if (!hotkeys.Running())
            {
                const auto attemptNow = std::chrono::steady_clock::now();

                if (lastTapAttempt.time_since_epoch().count() == 0 ||
                    attemptNow - lastTapAttempt > std::chrono::seconds(2))
                {
                    lastTapAttempt = attemptNow;
                    hotkeys.Start();
                }
            }

            for (const int key : hotkeys.PollPressed())
                HandleKey(key);

            const bool focused = MacInput::GameFocused();

            // The chat lives in the game window. Losing focus means it is gone,
            // so the suspension is cleared with it instead of leaving the
            // keybinds dead.
            if (!config.ui.chatDetection || !focused)
                chatActive = false;

            const bool structureRunning = structure.IsRunning();

            const bool capturing = menu.IsCapturingKey();
            const bool keyDown = hotkeys.Running()
                ? hotkeys.IsKeyDown(config.clicker.key)
                : MacInput::IsKeyDown(config.clicker.key);
            if (!keyDown)
                clickerBlockedUntilRelease = false;

            if (capturing)
            {
                clickerKeyWasDown = true;
            }
            else
            {
                const bool pressed = keyDown && !clickerKeyWasDown && !chatActive;
                if (pressed &&
                    !clickerBlockedUntilRelease &&
                    config.clicker.mode == ClickerMode::Toggle)
                {
                    clickerLatched = !clickerLatched;
                }
                clickerKeyWasDown = keyDown;
            }

            const bool heldNow = keyDown && !capturing && !clickerBlockedUntilRelease &&
                !chatActive;
            bool engaged = false;
            if (config.clicker.mode == ClickerMode::Toggle)
            {
                engaged = clickerLatched;
            }
            else
            {
                const auto keyNow = std::chrono::steady_clock::now();
                if (heldNow || !clicker.IsRunning())
                    clickerKeyUpSince = {};
                else if (clickerKeyUpSince.time_since_epoch().count() == 0)
                    clickerKeyUpSince = keyNow;

                const bool releaseGrace =
                    clicker.IsRunning() &&
                    clickerKeyUpSince.time_since_epoch().count() != 0 &&
                    keyNow - clickerKeyUpSince < std::chrono::milliseconds(150);
                engaged = heldNow || releaseGrace;
            }

            const auto focusNow = std::chrono::steady_clock::now();
            if (focused)
                clickerUnfocusedSince = {};
            else if (clickerUnfocusedSince.time_since_epoch().count() == 0)
                clickerUnfocusedSince = focusNow;
            const bool gameFocused = focused ||
                (clicker.IsRunning() &&
                    clickerUnfocusedSince.time_since_epoch().count() != 0 &&
                    focusNow - clickerUnfocusedSince < std::chrono::milliseconds(500));

            const bool clickerAllowed =
                Licensed() &&
                engaged &&
                !chatActive &&
                gameFocused &&
                !structureRunning;

            if (clickerAllowed && !clicker.IsRunning())
            {
                clicker.SetSettings(config.clicker);
                clicker.Start();
            }
            else if (!clickerAllowed && clicker.IsRunning())
            {
                clicker.Stop();
            }
            else if (clicker.IsRunning())
            {
                clicker.SetSettings(config.clicker);
            }

            const int clicks = clicker.ClickCount();
            const auto clickerNow = std::chrono::steady_clock::now();
            if (clicks < clickerSampleClicks || !clicker.IsRunning())
            {
                clickerSampleClicks = clicks;
                clickerSampleAt = clickerNow;
                if (!clicker.IsRunning())
                    clickerMeasuredCps = -1.0f;
            }
            else
            {
                const double sampleSeconds = std::chrono::duration<double>(
                    clickerNow - clickerSampleAt).count();
                if (sampleSeconds >= 0.25)
                {
                    clickerMeasuredCps = static_cast<float>(
                        (clicks - clickerSampleClicks) / sampleSeconds);
                    clickerSampleClicks = clicks;
                    clickerSampleAt = clickerNow;
                }
            }

            const int clickerPause =
                (engaged && !clicker.IsRunning() && !gameFocused) ? 1 :
                (engaged && !clicker.IsRunning() && structureRunning) ? 2 :
                0;

            overlay.SetPassMouseThrough(clicker.IsRunning());

            AppStatus status;
            status.gameRunning = MacInput::GameRunning();
            status.gameFocused = focused;
            status.structureRunning = structureRunning;
            status.clickerRunning = clicker.IsRunning();
            status.clickerEngaged = engaged;
            status.clickerClicks = clicks;
            status.clickerMeasuredCps = clickerMeasuredCps;
            status.clickerPause = clickerPause;
            status.bladeBallRunning = bladeBall.IsRunning();
            status.parries = bladeBall.ParryCount();
            status.chatActive = chatActive;
            menu.SetStatus(status);

            MenuActions actions;
            menu.Draw(config, actions);

            overlay.SetPanelRect(menu.PanelX(), menu.PanelY(), menu.PanelWidth(), menu.PanelHeight());
            overlay.SetVisible(menu.Visible());

            if (actions.startStructure)
                TryStartStructure();
            if (actions.exitRequested)
                quit = true;

            Autosave();

            /*
                Terminate on the next run loop pass: this frame is still inside
                ImGui's Render, and applicationWillTerminate shuts ImGui down.
            */
            if (quit && !m_Terminating)
            {
                m_Terminating = true;
                dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
            }
        }

        void Autosave()
        {
            /*
                Cheap change detection: the config is a flat aggregate of ints,
                doubles and bools, so a byte compare catches every edit. Padding
                bytes can only cause an extra save, never a missed one.
            */
            if (std::memcmp(&config, &lastSaved, sizeof(AppConfig)) == 0)
                return;

            const auto now = std::chrono::steady_clock::now();

            if (lastSave.time_since_epoch().count() != 0 &&
                now - lastSave < std::chrono::milliseconds(600))
                return;

            if (Settings::Save(config))
            {
                lastSaved = config;
                lastSave = now;
            }
        }
    };

    AppCore* g_Core = nullptr;
}

@interface DribtardAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation DribtardAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;

    g_Core = new AppCore();
    if (!g_Core->Start())
    {
        delete g_Core;
        g_Core = nullptr;
        [NSApp terminate:nil];
    }
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    (void)notification;

    if (g_Core != nullptr)
    {
        g_Core->Shutdown();
        delete g_Core;
        g_Core = nullptr;
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    (void)sender;
    return YES;
}

@end

int RunMacApp()
{
    @autoreleasepool
    {
        NSApplication* app = [NSApplication sharedApplication];

        // Accessory: no Dock icon and no menu bar takeover, like the Windows
        // build sitting on top of the game.
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];

        DribtardAppDelegate* delegate = [[DribtardAppDelegate alloc] init];
        app.delegate = delegate;

        [app run];
    }

    return 0;
}
