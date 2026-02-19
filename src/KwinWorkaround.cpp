#include "../include/KwinWorkaround.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace gsr {
    static ActiveKwinWindow active_kwin_window;
    static bool kwin_helper_thread_started = false;

    void kwin_script_thread() {
        FILE* pipe = popen("gsr-kwin-helper", "r");
        if (!pipe) {
            std::cerr << "Failed to start gsr-kwin-helper process\n";
            return;
        }

        std::cerr << "Started a KWin helper thread\n";

        char buffer[4096];
        const std::string prefix_title = "Active window title set to: ";
        const std::string prefix_fullscreen = "Active window fullscreen state set to: ";
        const std::string prefix_monitor = "Active window monitor name set to: ";

        std::string line;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            line = buffer;

            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }

            size_t pos = std::string::npos;
            if ((pos = line.find(prefix_title)) != std::string::npos) {
                std::string title = line.substr(pos + prefix_title.length());
                active_kwin_window.title = std::move(title);
            } else if ((pos = line.find(prefix_fullscreen)) != std::string::npos) {
                std::string fullscreen = line.substr(pos + prefix_fullscreen.length());
                active_kwin_window.fullscreen = fullscreen == "1";
            } else if ((pos = line.find(prefix_monitor)) != std::string::npos) {
                std::string monitorName = line.substr(pos + prefix_monitor.length());
                active_kwin_window.monitorName = std::move(monitorName);
            }
        }

        pclose(pipe);
    }

    std::string get_current_kwin_window_title() {
        return active_kwin_window.title;
    }

    bool get_current_kwin_window_fullscreen() {
        return active_kwin_window.fullscreen;
    }

    std::string get_current_kwin_window_monitor_name() {
        return active_kwin_window.monitorName;
    }

    void start_kwin_helper_thread() {
        if (kwin_helper_thread_started) {
            return;
        }

        kwin_helper_thread_started = true;

        std::thread([&] {
            kwin_script_thread();
        }).detach();
    }
}