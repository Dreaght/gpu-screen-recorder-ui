#pragma once
#include <string>

namespace gsr {
    struct ActiveHyprlandWindow {
        std::string window_id = "";
        std::string title = "Game";
    };

    void start_hyprland_listener_thread();
    std::string get_current_hyprland_window_title();
}
