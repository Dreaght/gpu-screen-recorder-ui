#pragma once

#include "DesktopEnvironment.hpp"
#include <sys/types.h>
#include <stdio.h>

namespace gsr {
    class DesktopEnvironmentHyprland : public DesktopEnvironment {
    public:
        DesktopEnvironmentHyprland() = default;
        DesktopEnvironmentHyprland(const DesktopEnvironmentHyprland&) = delete;
        DesktopEnvironmentHyprland& operator=(const DesktopEnvironmentHyprland&) = delete;
        ~DesktopEnvironmentHyprland();

        bool start() override;
        void update() override;
        std::string get_focused_window_title() override;
        //std::string get_focused_monitor_name() override;
    private:
        void shutdown();
    private:
        pid_t process_id = -1;
        FILE *stdout_file = nullptr;
        int read_fd = -1;
        char line_buffer[1024];
        std::string line;

        std::string window_title;
    };
}