#pragma once

#include "DesktopEnvironment.hpp"
#include <memory>
#include <X11/Xlib.h>

struct wl_display;

namespace gsr {
    class DesktopEnvironmentWlroots : public DesktopEnvironment {
    public:
        static bool is_supported(struct wl_display *dpy);

        explicit DesktopEnvironmentWlroots(struct wl_display *dpy, Display *x11_dpy);
        DesktopEnvironmentWlroots(const DesktopEnvironmentWlroots&) = delete;
        DesktopEnvironmentWlroots& operator=(const DesktopEnvironmentWlroots&) = delete;
        ~DesktopEnvironmentWlroots() override;

        bool start() override;
        void update() override;
        FocusedWindow get_focused_window() override;

        struct Impl;
    private:
        std::unique_ptr<Impl> impl;
    };
}
