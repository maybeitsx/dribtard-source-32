#pragma once

// Current semantic version of the application.
// Bump this before building a new release and running release-tools/publish_release.mjs.
#ifndef APP_VERSION
#define APP_VERSION "1.0.2"
#endif

namespace Version
{

inline constexpr const char* kAppVersion = APP_VERSION;

} // namespace Version
