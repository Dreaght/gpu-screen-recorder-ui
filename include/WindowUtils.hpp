#pragma once

#include <mglpp/system/vec.hpp>
#include <string>
#include <vector>
#include <X11/Xlib.h>

namespace gsr {
    enum class WindowCaptureType {
        FOCUSED,
        CURSOR
    };

    struct Monitor {
        mgl::vec2i position;
        mgl::vec2i size;
    };

    Window get_focused_window(Display *dpy, WindowCaptureType cap_type);
    std::string get_focused_window_name(Display *dpy, WindowCaptureType window_capture_type);
    std::string get_window_name_at_position(Display *dpy, mgl::vec2i position, Window ignore_window);
    std::string get_window_name_at_cursor_position(Display *dpy, Window ignore_window);
    mgl::vec2i get_cursor_position(Display *dpy, Window *window);
    mgl::vec2i create_window_get_center_position(Display *display);
    std::string get_window_manager_name(Display *display);
    bool is_compositor_running(Display *dpy, int screen);
    std::vector<Monitor> get_monitors(Display *dpy);
}