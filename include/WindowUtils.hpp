#pragma once

#include <mglpp/system/vec.hpp>
#include <string>
#include <vector>
#include <optional>
#include <X11/Xlib.h>

namespace gsr {
    enum class WindowCaptureType {
        FOCUSED,
        CURSOR
    };

    struct Monitor {
        mgl::vec2i position;
        mgl::vec2i size;
        std::string name;
    };

    std::optional<std::string> get_window_title(Display *dpy, Window window);
    Window get_focused_window(Display *dpy, WindowCaptureType cap_type);
    std::string get_focused_window_name(Display *dpy, WindowCaptureType window_capture_type);
    std::string get_window_name_at_position(Display *dpy, mgl::vec2i position, Window ignore_window);
    std::string get_window_name_at_cursor_position(Display *dpy, Window ignore_window);
    void set_window_size_not_resizable(Display *dpy, Window window, int width, int height);
    mgl::vec2i get_cursor_position(Display *dpy, Window *window);
    mgl::vec2i create_window_get_center_position(Display *display);
    std::string get_window_manager_name(Display *display);
    bool is_compositor_running(Display *dpy, int screen);
    std::vector<Monitor> get_monitors(Display *dpy);
    void xi_grab_all_mouse_devices(Display *dpy);
    void xi_ungrab_all_mouse_devices(Display *dpy);
    void xi_warp_all_mouse_devices(Display *dpy, mgl::vec2i position);
    void window_set_fullscreen(Display *dpy, Window window, bool fullscreen);
    bool window_is_fullscreen(Display *display, Window window);
    bool set_window_wm_state(Display *dpy, Window window, Atom atom);
    void make_window_click_through(Display *display, Window window);
    bool make_window_sticky(Display *dpy, Window window);
    bool hide_window_from_taskbar(Display *dpy, Window window);
}