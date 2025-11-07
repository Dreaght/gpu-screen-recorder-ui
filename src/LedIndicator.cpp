#include "../include/LedIndicator.hpp"

#include <X11/XKBlib.h>

namespace gsr {
    LedIndicator::LedIndicator(Display *dpy) : dpy(dpy) {
        scroll_lock_atom = XInternAtom(dpy, "Scroll Lock", False);
        XkbSetNamedIndicator(dpy, scroll_lock_atom, True, False, False, NULL);
    }

    LedIndicator::~LedIndicator() {
        XkbSetNamedIndicator(dpy, scroll_lock_atom, True, False, False, NULL);
    }

    void LedIndicator::set_led(bool enabled) {
        led_enabled = enabled;
        perform_blink = false;
    }

    void LedIndicator::blink() {
        perform_blink = true;
        blink_timer.restart();
    }

    void LedIndicator::update_led(bool new_state) {
        if(new_state == led_indicator_on)
            return;

        led_indicator_on = new_state;
        XkbSetNamedIndicator(dpy, scroll_lock_atom, True, new_state, False, NULL);
    }

    void LedIndicator::update() {
        if(perform_blink) {
            const double blink_elapsed_sec = blink_timer.get_elapsed_time_seconds();
            if(blink_elapsed_sec < 0.2) {
                update_led(false);
            } else if(blink_elapsed_sec < 0.4) {
                update_led(true);
            } else if(blink_elapsed_sec < 0.6) {
                update_led(false);
            } else if(blink_elapsed_sec < 0.8) {
                update_led(true);
            } else {
                perform_blink = false;
            }
        } else {
            update_led(led_enabled);
        }
    }
}