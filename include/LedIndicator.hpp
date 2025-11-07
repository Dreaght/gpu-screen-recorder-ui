#pragma once

#include <X11/Xlib.h>
#include <mglpp/system/Clock.hpp>

namespace gsr {
    class LedIndicator {
    public:
        LedIndicator(Display *dpy);
        LedIndicator(const LedIndicator&) = delete;
        LedIndicator& operator=(const LedIndicator&) = delete;
        ~LedIndicator();

        void set_led(bool enabled);
        void blink();
        void update();
    private:
        void update_led(bool new_state);
    private:
        Display *dpy = nullptr;
        Atom scroll_lock_atom = None;
        bool led_indicator_on = false;
        bool led_enabled = false;
        bool perform_blink = false;
        mgl::Clock blink_timer;
    };
}