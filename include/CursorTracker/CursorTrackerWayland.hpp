#pragma once

#include "CursorTracker.hpp"
#include <stdint.h>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_output;
struct zxdg_output_manager_v1;
struct zxdg_output_v1;

namespace gsr {
    struct WaylandOutput {
        uint32_t wl_name;
        struct wl_output *output;
        struct zxdg_output_v1 *xdg_output;
        mgl::vec2i pos;
        mgl::vec2i size;
        int32_t transform;
        std::string name;
    };

    class CursorTrackerWayland : public CursorTracker {
    public:
        CursorTrackerWayland(const char *card_path);
        CursorTrackerWayland(const CursorTrackerWayland&) = delete;
        CursorTrackerWayland& operator=(const CursorTrackerWayland&) = delete;
        ~CursorTrackerWayland();

        void update() override;
        std::optional<CursorInfo> get_latest_cursor_info() override;

        std::vector<WaylandOutput> monitors;
        struct zxdg_output_manager_v1 *xdg_output_manager = nullptr;
    private:
        void set_monitor_outputs_from_xdg_output(struct wl_display *dpy);
    private:
        int drm_fd = -1;
        mgl::vec2i latest_cursor_position; // Position of the cursor within the monitor
        int latest_crtc_id = -1;
    };
}