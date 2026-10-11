#pragma once

// A GL context can only be created when the process can open the X11 display.
// SFML aborts the process (uncatchable SIGABRT) from its X11 backend otherwise,
// so any test that creates a RenderTexture must ask first.
//
// This lives in its own translation unit because <X11/Xlib.h> defines macros
// (Status, None, Success, ...) that clash with doctest and SFML; keeping the
// include out of the test headers avoids that pollution.
namespace TestSupport
{

// True when a usable display is available (always true off Linux). An unset
// DISPLAY is not the only failure: a set-but-dead DISPLAY (xvfb-run losing its
// server) also aborts, so the X server is probed directly.
bool displayAvailable();

} // namespace TestSupport
