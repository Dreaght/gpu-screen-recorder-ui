#pragma once

#include <X11/Xlib.h>

#include <mglpp/graphics/Color.hpp>

namespace gsr {
    class WindowSelector {
    public:
        WindowSelector();
        WindowSelector(const WindowSelector&) = delete;
        WindowSelector& operator=(const WindowSelector&) = delete;
        ~WindowSelector();

        bool start(mgl::Color border_color);
        void stop();
        bool is_started() const;

        bool failed() const;
        bool poll_events();
        bool take_selection();
        bool take_canceled();
        Window get_selection() const;
    private:
        Display *dpy = nullptr;
        Cursor crosshair_cursor = None;
        Colormap border_window_colormap = None;
        Window border_window = None;
        Window selected_window = None;
        bool selected = false;
        bool canceled = false;
    };
}