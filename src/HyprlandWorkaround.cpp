#include "../include/HyprlandWorkaround.hpp"

#include <iostream>
#include <unistd.h>
#include <thread>

namespace gsr {
    static ActiveHyprlandWindow active_hyprland_window;
    static bool hyprland_listener_thread_started = false;

    static void hyprland_listener_thread() {
        const bool inside_flatpak = access("/app/manifest.json", F_OK) == 0;

        const char *hyprland_helper_bin =
            inside_flatpak ? 
            "flatpak-spawn --host -- /var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/bin/gsr-hyprland-helper" 
            : "gsr-hyprland-helper";

        FILE* pipe = popen(hyprland_helper_bin, "r");
        if (!pipe) {
            std::cerr << "Failed to start gsr-hyprland-helper process\n";
            return;
        }

        std::cerr << "Started Hyprland helper thread\n";

        char buffer[4096];
        const std::string prefix = "Window title changed: ";

        std::string line;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            line = buffer;

            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }

            size_t pos = line.find(prefix);
            if (pos != std::string::npos) {
                active_hyprland_window.title = line.substr(pos + prefix.length());
            }
        }

        pclose(pipe);
    }

    std::string get_current_hyprland_window_title() {
        return active_hyprland_window.title;
    }

    void start_hyprland_listener_thread() {
        if (hyprland_listener_thread_started) {
            return;
        }

        hyprland_listener_thread_started = true;

        std::thread([&] {
            hyprland_listener_thread();
        }).detach();
    }
}