#pragma once

#include "RegionSelector.hpp"

namespace gsr {
    class RegionSelectorMock : public RegionSelector {
    public:
        static bool is_supported();

        explicit RegionSelectorMock();
        ~RegionSelectorMock() override;

        bool start(SelectionType selection_type, mgl::Color border_color) override;
        void stop() override;
        void cancel() override;
        bool is_started() const override;

        bool failed() const override;
        void handle_event(void *native_event) override;
        bool take_selection() override;
        bool take_canceled() override;

        Region get_region_selection(Display *x11_dpy, struct wl_display *wayland_dpy) const override;
        Window get_window_selection() const override;

        SelectionType get_selection_type() const override;
    private:
        bool started = false;

        unsigned long ticks_counter = 0;
        bool selected = false;
        bool canceled = false;

        SelectionType selection_type = SelectionType::NONE;
    };
}
