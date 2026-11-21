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

        fprintf(stderr, "RegionSelectorMock: started selection.\n");

        return true;
    }

    void RegionSelectorMock::stop() {
        if(!started)
            return;

        started = false;

        fprintf(stderr, "RegionSelectorMock: stopped selection.\n");
    }

    void RegionSelectorMock::cancel() {
        fprintf(stderr, "RegionSelectorMock: canceling selection.\n");

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

        ++ticks_counter;
        fprintf(stderr, "RegionSelectorMock: current tick: %ld.\n", ticks_counter);
        if(ticks_counter >= 5)
            selected = true;

        if(selected || canceled) {
            fprintf(stderr, "RegionSelectorMock: stopping, selected = %hhd, canceled = %hhd.\n", selected, canceled);

            stop();
            return;
        }
    }

    bool RegionSelectorMock::take_selection() {
        const bool result = selected;
        selected = false;

        if(result)
            fprintf(stderr, "RegionSelectorMock: took selection.\n");

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
