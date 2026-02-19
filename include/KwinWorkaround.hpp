#pragma once

#include <string>

namespace gsr {
    struct ActiveKwinWindow {
        std::string title = "Game";
        bool fullscreen = false;
    };

    void start_kwin_helper_thread();
    std::string get_current_kwin_window_title();
    bool get_current_kwin_window_fullscreen();
}
