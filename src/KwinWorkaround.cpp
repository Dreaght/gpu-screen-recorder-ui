#include "../include/KwinWorkaround.hpp"

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
        const std::string prefix = "Active window title set to: ";

        std::string line;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            line = buffer;

            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }

            size_t pos = line.find(prefix);
            if (pos != std::string::npos) {
                std::string title = line.substr(pos + prefix.length());

                if (title == "gsr ui" || title == "gsr notify") {
                    continue; // ignore the overlay and notification
                }

                active_kwin_window.title = std::move(title);
            }
        }

        pclose(pipe);
    }

    std::string get_current_kwin_window_title() {
        return active_kwin_window.title;
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