#include "../../include/DesktopEnvironment/DesktopEnvironmentGnome.hpp"
#include "../../include/WindowUtils.hpp"
#include "../../include/Process.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>

namespace gsr {
    static constexpr std::string_view prefix_title = "Active window title set to: ";
    static constexpr std::string_view prefix_fullscreen = "Active window fullscreen state set to: ";
    static constexpr std::string_view prefix_monitor = "Active window monitor name set to: ";
    static constexpr std::string_view prefix_pid = "Active window pid set to: ";

    DesktopEnvironmentGnome::~DesktopEnvironmentGnome() {
        shutdown();
    }

    bool DesktopEnvironmentGnome::start() {
        if(process_id > 0) {
            fprintf(stderr, "Error: DesktopEnvironmentGnome: already running\n");
            return false;
        }

        const char *args[] = { "gsr-gnome-helper", NULL };
        process_id = exec_program(args, &read_fd, false);
        if(process_id == -1) {
            fprintf(stderr, "Error: DesktopEnvironmentGnome: failed to execute gsr-gnome-helper\n");
            return false;
        }

        fcntl(read_fd, F_SETFL, fcntl(read_fd, F_GETFL) | O_NONBLOCK);

        stdout_file = fdopen(read_fd, "r");
        if(!stdout_file) {
            perror("Error: DesktopEnvironmentGnome: fdopen");
            shutdown();
            return false;
        }
        read_fd = -1;

        fprintf(stderr, "Info: DesktopEnvironmentGnome: started gnome helper process\n");
        return true;
    }

    void DesktopEnvironmentGnome::shutdown() {
        if(process_id > 0) {
            kill(process_id, SIGKILL);
            int status;
            if(waitpid(process_id, &status, 0) == -1) {
                perror("waitpid failed");
            }
            process_id = -1;
        }

        if(stdout_file) {
            fclose(stdout_file);
            stdout_file = nullptr;
        }

        if(read_fd > 0) {
            close(read_fd);
            read_fd = -1;
        }
    }

    void DesktopEnvironmentGnome::update() {
        while(fgets(line_buffer, sizeof(line_buffer), stdout_file) != nullptr) {
            line = line_buffer;

            if(!line.empty() && line.back() == '\n')
                line.pop_back();

            size_t pos = std::string::npos;
            if((pos = line.find(prefix_title)) != std::string::npos) {
                window_title = line.substr(pos + prefix_title.length());
            } else if((pos = line.find(prefix_fullscreen)) != std::string::npos) {
                // unused
            } else if((pos = line.find(prefix_monitor)) != std::string::npos) {
                monitor_name = line.substr(pos + prefix_monitor.length());
            } else if((pos = line.find(prefix_pid)) != std::string::npos) {
                window_pid = atoi(line.c_str() + pos + prefix_pid.length());
            }
        }
    }

    FocusedWindow DesktopEnvironmentGnome::get_focused_window() {
        if(!window_title.empty())
            return FocusedWindow{ window_title, window_pid > 0 ? window_pid : -1 };

        // The gnome extension is not loaded on the first install. In that case fallback to x11.
        // The user has to logout and in to load the gnome extension.
        FocusedWindow focused_window;
        if(!x11_dpy)
            return focused_window;

        Window window = None;
        focused_window.title = get_window_name_at_cursor_position(x11_dpy, "gsr-ui", &window);
        if(focused_window.title.empty()) {
            window = None;
            focused_window.title = get_focused_window_name(x11_dpy, WindowCaptureType::FOCUSED, false, "gsr-ui", &window);
        }

        if(window != None)
            focused_window.pid = get_window_pid(x11_dpy, window);

        return focused_window;
    }
}
