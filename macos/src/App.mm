#include "App.hpp"

#include "Configuration/Settings.hpp"
#include "Keys.hpp"
#include "Macro/BladeBallMacro.hpp"
#include "Macro/MacroEngine.hpp"
#include "Macro/MoonwalkMacro.hpp"
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
        MoonwalkEngine moonwalk{};
        BladeBallEngine bladeBall{};
        HotkeyMonitor hotkeys{};
        Overlay overlay{};

        AppConfig lastSaved{};
        std::chrono::steady_clock::time_point lastSave{};
        bool moonwalkBlocked = false;
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
            moonwalk.Stop();
            bladeBall.Stop();
            hotkeys.Stop();

            Settings::Save(config);

            overlay.Shutdown();
        }

        void HandleKey(int keyCode)
        {
            // While the menu is rebinding, the next key goes to the setting.
            if (menu.IsCapturingKey())
            {
                menu.CaptureKey(keyCode, config);
                return;
            }

            if (keyCode == Keys::F12)
            {
                structure.EmergencyStop();
                moonwalk.EmergencyStop();
                bladeBall.EmergencyStop();
                moonwalkBlocked = true;
                return;
            }

            if (keyCode == config.ui.menuKey)
            {
                menu.SetVisible(!menu.Visible());
                return;
            }

            if (config.bladeBall.armed && keyCode == config.bladeBall.triggerKey)
            {
                TryStartBladeBall();
                return;
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
                }
            }
        }

        void TryStartStructure()
        {
            if (config.bladeBall.armed || !Licensed())
                return;
            if (structure.IsRunning() || moonwalk.IsRunning() || bladeBall.IsRunning())
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
            if (structure.IsRunning() || moonwalk.IsRunning() || bladeBall.IsRunning())
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

            for (const int key : hotkeys.PollPressed())
                HandleKey(key);

            const bool focused = MacInput::GameFocused();

            AppStatus status;
            status.gameRunning = MacInput::GameRunning();
            status.gameFocused = focused;
            status.structureRunning = structure.IsRunning();
            status.moonwalkRunning = moonwalk.IsRunning();
            status.bladeBallRunning = bladeBall.IsRunning();
            status.parries = bladeBall.ParryCount();
            menu.SetStatus(status);

            // Moonwalk is a hold loop: it runs while its key is down, never
            // alongside another engine, and only with the game focused.
            const bool held = !menu.IsCapturingKey() &&
                !config.bladeBall.armed &&
                focused &&
                MacInput::IsKeyDown(config.moonwalk.holdKey);

            if (!held)
                moonwalkBlocked = false;

            const bool moonwalkAllowed =
                held && !moonwalkBlocked && !structure.IsRunning();

            if (moonwalkAllowed && !moonwalk.IsRunning())
            {
                moonwalk.SetSettings(config.moonwalk);
                if (!moonwalk.Start())
                    moonwalkBlocked = true;
            }
            else if ((!moonwalkAllowed || moonwalkBlocked) && moonwalk.IsRunning())
            {
                moonwalk.Stop();
            }

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
