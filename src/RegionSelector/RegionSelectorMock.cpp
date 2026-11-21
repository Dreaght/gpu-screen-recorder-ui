#include "../../include/RegionSelector/RegionSelectorMock.hpp"

#include <assert.h>

namespace gsr {
    bool RegionSelectorMock::is_supported() {
        return true;
    }

    RegionSelectorMock::RegionSelectorMock() {

    }

    RegionSelectorMock::~RegionSelectorMock() {
        stop();
    }

    bool RegionSelectorMock::start(SelectionType selection_type, mgl::Color border_color) {
        (void)border_color;

        if(started)
            return false;

        selected = false;
        canceled = false;
        this->selection_type = selection_type;
        started = true;
        return true;
    }

    void RegionSelectorMock::stop() {
        if(!started)
            return;

        started = false;
    }

    void RegionSelectorMock::cancel() {
        canceled = true;
        selected = false;
        stop();
    }

    bool RegionSelectorMock::is_started() const {
        return started;
    }

    bool RegionSelectorMock::failed() const {
        return false;
    }

    void RegionSelectorMock::handle_event(void *native_event) {
        (void)native_event;

        if(!started)
            return;

        if(selected || canceled) {
            stop();
            return;
        }
    }

    bool RegionSelectorMock::take_selection() {
        const bool result = selected;
        selected = false;
        return result;
    }

    bool RegionSelectorMock::take_canceled() {
        const bool result = canceled;
        canceled = false;
        return result;
    }

    Region RegionSelectorMock::get_region_selection(Display *x11_dpy, struct wl_display *wayland_dpy) const {
        (void)x11_dpy;
        (void)wayland_dpy;
        assert(selection_type == SelectionType::REGION);
        return { {0, 0}, {100, 100} };
    }

    Window RegionSelectorMock::get_window_selection() const {
        assert(selection_type == SelectionType::WINDOW);
        return None;
    }

    RegionSelector::SelectionType RegionSelectorMock::get_selection_type() const {
        return selection_type;
    }
}
