#include "DisplayProbe.h"

#include <cstdlib>

#if defined(__linux__)
#include <X11/Xlib.h>
#endif

namespace TestSupport
{

bool displayAvailable()
{
#if defined(__linux__)
    if (std::getenv("DISPLAY") == nullptr)
        return false;
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr)
        return false;
    XCloseDisplay(display);
    return true;
#else
    return true;
#endif
}

} // namespace TestSupport
