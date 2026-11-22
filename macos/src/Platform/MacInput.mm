#include "Platform/MacInput.hpp"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace
{
    bool IsRobloxApp(NSRunningApplication* app)
    {
        if (app == nil)
            return false;

        NSString* bundle = app.bundleIdentifier;
        if (bundle != nil && [bundle.lowercaseString hasPrefix:@"com.roblox"])
            return true;

        NSString* name = app.localizedName;
        return name != nil && [name caseInsensitiveCompare:@"Roblox"] == NSOrderedSame;
    }
}

bool MacInput::PostKey(int keyCode, bool keyUp)
{
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (source == nullptr)
        return false;

    CGEventRef event = CGEventCreateKeyboardEvent(source, static_cast<CGKeyCode>(keyCode), !keyUp);
    if (event == nullptr)
    {
        CFRelease(source);
        return false;
    }

    // A real tap is not an auto-repeat; games treat a repeat as a held key.
    CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, 0);
    CGEventPost(kCGHIDEventTap, event);

    CFRelease(event);
    CFRelease(source);
    return true;
}

bool MacInput::PostMouseMove(int dx, int dy)
{
    if (dx == 0 && dy == 0)
        return true;

    CGPoint location = CGPointMake(0.0, 0.0);
    if (CGEventRef current = CGEventCreate(nullptr))
    {
        location = CGEventGetLocation(current);
        CFRelease(current);
    }

    CGEventRef event = CGEventCreateMouseEvent(
        nullptr, kCGEventMouseMoved, location, kCGMouseButtonLeft);
    if (event == nullptr)
        return false;

    /*
        Roblox reads the relative delta of the event, not the pointer position.
        Posting a move with only the deltas set is what turns the camera
        without teleporting the pointer across the screen.
    */
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, dx);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, dy);
    CGEventPost(kCGHIDEventTap, event);

    CFRelease(event);
    return true;
}

bool MacInput::PostMouseButton(bool rightButton, bool buttonUp)
{
    CGPoint location = CGPointMake(0.0, 0.0);
    if (CGEventRef current = CGEventCreate(nullptr))
    {
        location = CGEventGetLocation(current);
        CFRelease(current);
    }

    const CGMouseButton button = rightButton ? kCGMouseButtonRight : kCGMouseButtonLeft;
    CGEventType type;
    if (rightButton)
        type = buttonUp ? kCGEventRightMouseUp : kCGEventRightMouseDown;
    else
        type = buttonUp ? kCGEventLeftMouseUp : kCGEventLeftMouseDown;

    CGEventRef event = CGEventCreateMouseEvent(nullptr, type, location, button);
    if (event == nullptr)
        return false;

    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return true;
}

bool MacInput::IsKeyDown(int keyCode)
{
    return CGEventSourceKeyState(
        kCGEventSourceStateCombinedSessionState, static_cast<CGKeyCode>(keyCode)) != false;
}

bool MacInput::IsTrusted()
{
    return AXIsProcessTrusted() != false;
}

void MacInput::RequestTrust()
{
    NSDictionary* options = @{ (__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES };
    AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

bool MacInput::GameRunning()
{
    for (NSRunningApplication* app in [[NSWorkspace sharedWorkspace] runningApplications])
    {
        if (IsRobloxApp(app))
            return true;
    }
    return false;
}

bool MacInput::GameFocused()
{
    NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    if (IsRobloxApp(front))
        return true;

    // The menu sits on top of the game. That must not look like Roblox lost focus.
    return front != nil &&
        [front isEqual:[NSRunningApplication currentApplication]] &&
        GameRunning();
}
