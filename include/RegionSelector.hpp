#pragma once

#include "WindowUtils.hpp"
#include <mglpp/system/vec.hpp>
#include <mglpp/graphics/Color.hpp>
#include <vector>

#include <X11/Xlib.h>

struct wl_display;

namespace gsr {
    struct Region {
        mgl::vec2i pos;
        mgl::vec2i size;
    };

    class RegionSelector {
    public:
        RegionSelector();
        RegionSelector(const RegionSelector&) = delete;
        RegionSelector& operator=(const RegionSelector&) = delete;
        ~RegionSelector();

        bool start(mgl::Color border_color);
        void stop();
        bool is_started() const;

        bool failed() const;
        bool poll_events();
        bool take_selection();
        bool take_canceled();
        Region get_selection(Display *x11_dpy, struct wl_display *wayland_dpy) const;
    private:
        void on_button_press(const void *de);
        void on_button_release(const void *de);
        void on_mouse_motion(const void *de);
    private:
        Display *dpy = nullptr;
        unsigned long region_window = 0;
        unsigned long cursor_window = 0;
        unsigned long region_window_colormap = 0;
        int xi_opcode = 0;
        GC region_gc = nullptr;
        GC cursor_gc = nullptr;

        Region region;
        bool selecting_region = false;
        bool selected = false;
        bool canceled = false;
        bool is_wayland = false;
        std::vector<Monitor> monitors;
        mgl::vec2i cursor_pos;
    };
}