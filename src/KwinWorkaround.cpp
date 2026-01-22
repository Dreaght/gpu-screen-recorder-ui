#include "../include/KwinWorkaround.hpp"

#include <cstddef>
#include <iostream>
#include <sys/types.h>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <array>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace gsr {
    static ActiveKwinWindow *active_kwin_window = nullptr;
    static bool kwin_helper_thread_started = false;

    std::string get_current_kwin_window_title() {
        if (active_kwin_window == nullptr) {
            return "Game";
        }

        return active_kwin_window->title;
    }

    void kwin_script_thread() {
        const bool inside_flatpak = access("/app/manifest.json", F_OK) == 0;

        const std::string kwin_helper_bin = inside_flatpak ? "/app/bin/gsr-kwin-helper" : "/usr/bin/gsr-kwin-helper";

        FILE* pipe = popen(kwin_helper_bin.c_str(), "r");
        if (!pipe) {
            std::cerr << "Failed to start gsr-kwin-helper process\n";
            return;
        }

        std::cerr << "Started a KWin helper thread\n";

        char buffer[4096];
        const std::string prefix = "Active window title set to: ";

        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);

            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }

            size_t pos = line.find(prefix);
            if (pos != std::string::npos) {
                std::string title = line.substr(pos + prefix.length());

                if (title == "gsr ui") {
                    continue; // ignore the overlay
                }

                active_kwin_window->title = title;
            }
        }

        pclose(pipe);
    }

    void start_kwin_helper_thread() {
        if (kwin_helper_thread_started) {
            return;
        }

        if (active_kwin_window == nullptr) {
            active_kwin_window = new ActiveKwinWindow();
        }

        kwin_helper_thread_started = true;

        std::thread([&] {
            kwin_script_thread();
        }).detach();
    }
}