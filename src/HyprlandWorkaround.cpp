#include "../include/HyprlandWorkaround.hpp"
#include "../include/Process.hpp"

#include <cstddef>
#include <iostream>
#include <sys/types.h>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace gsr {
    static ActiveHyprlandWindow *active_hyprland_window = nullptr;
    static bool hyprland_listener_thread_started = false;

    static void hyprland_listener_thread() {
        const bool inside_flatpak = access("/app/manifest.json", F_OK) == 0;

        const std::string hyprland_helper_bin = (
            inside_flatpak ? 
            "flatpak-spawn --host -- /var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/bin/gsr-hyprland-helper" 
            : "/usr/bin/gsr-hyprland-helper"
        );

        FILE* pipe = popen(hyprland_helper_bin.c_str(), "r");
        if (!pipe) {
            std::cerr << "Failed to start gsr-hyprland-helper process\n";
            return;
        }

        std::cerr << "Started Hyprland helper thread\n";

        char buffer[4096];
        const std::string prefix = "Window title changed: ";

        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);

            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }

            size_t pos = line.find(prefix);
            if (pos != std::string::npos) {
                std::string title = line.substr(pos + prefix.length());

                active_hyprland_window->title = title;
            }
        }

        pclose(pipe);
    }

    std::string get_current_hyprland_window_title() {
        if (active_hyprland_window == nullptr) {
            return "Game";
        }

        return active_hyprland_window->title;
    }

    void start_hyprland_listener_thread() {
        if (hyprland_listener_thread_started) {
            return;
        }

        if (active_hyprland_window == nullptr) {
            active_hyprland_window = new ActiveHyprlandWindow();
        }

        hyprland_listener_thread_started = true;

        std::thread([&] {
            hyprland_listener_thread();
        }).detach();
    }
}