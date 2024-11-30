#pragma once

#include <string>
#include <X11/Xlib.h>

namespace gsr {
    enum class WindowCaptureType {
        FOCUSED,
        CURSOR
    };

    Window get_focused_window(Display *dpy, WindowCaptureType cap_type);
    std::string get_focused_window_name(Display *dpy, WindowCaptureType window_capture_type);
}