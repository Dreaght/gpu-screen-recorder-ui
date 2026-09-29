#include "../../include/DesktopEnvironment/DesktopEnvironmentX11.hpp"
#include "../../include/WindowUtils.hpp"

namespace gsr {
    DesktopEnvironmentX11::~DesktopEnvironmentX11() {
        
    }

    bool DesktopEnvironmentX11::start() {
        return true;
    }

    void DesktopEnvironmentX11::update() {
        
    }

    FocusedWindow DesktopEnvironmentX11::get_focused_window() {
        FocusedWindow focused_window;
        if(!dpy) {
            fprintf(stderr, "Error: DesktopEnvironmentX11: get_focused_window: display object is NULL, returning empty window\n");
            return focused_window;
        }

        Window window = None;
        focused_window.title = get_window_name_at_cursor_position(dpy, "gsr-ui", &window);
        if(focused_window.title.empty()) {
            window = None;
            focused_window.title = get_focused_window_name(dpy, WindowCaptureType::FOCUSED, false, "gsr-ui", &window);
        }

        if(window != None)
            focused_window.pid = get_window_pid(dpy, window);

        return focused_window;
    }

    // std::string DesktopEnvironmentX11::get_focused_monitor_name() {
    //     return "";
    // }
}
