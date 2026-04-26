#include "../../include/DesktopEnvironment/DesktopEnvironmentHyprland.hpp"
#include "../../include/Process.hpp"

#include <fcntl.h>
#include <sys/wait.h>

namespace gsr {
    static constexpr std::string_view prefix = "Window title changed: ";

    static bool get_hyprland_socket_path(char *path, int path_len) {
        const char* xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
        const char* instance_sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (!xdg_runtime_dir || !instance_sig) {
            fprintf(stderr, "Error: HyprlandWorkaround: environment variables not set\n");
            return false;
        }

        if (snprintf(path, path_len, "%s/hypr/%s/.socket2.sock", xdg_runtime_dir, instance_sig) >= path_len) {
            fprintf(stderr, "Error: HyprlandWorkaround: path to hyprland socket (%s/hypr/%s/.socket2.sock) is more than %d characters long\n", xdg_runtime_dir, instance_sig, path_len);
            return false;
        }

        return true;
    }

    DesktopEnvironmentHyprland::~DesktopEnvironmentHyprland() {
        shutdown();
    }

    bool DesktopEnvironmentHyprland::start() {
        if(process_id > 0) {
            fprintf(stderr, "Error: DesktopEnvironmentHyprland: already running\n");
            return false;
        }

        char hyprland_socket_path[256];

        // Get path inside the flatpak before flatpak-spawn is called because of a bug in flatpak:
        // https://github.com/flatpak/flatpak/issues/6486
        // where environment variables are missing in flatpak-spawn --host
        if(!get_hyprland_socket_path(hyprland_socket_path, sizeof(hyprland_socket_path))) {
            fprintf(stderr, "Error: DesktopEnvironmentHyprland: failed to get hyprland socket path\n");
            return false;
        }

        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;

        size_t arg_index = 0;
        const char *args[6];

        if(inside_flatpak) {
            args[arg_index++] = "flatpak-spawn";
            args[arg_index++] = "--host";
            args[arg_index++] = "--";
            args[arg_index++] = "/var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/bin/gsr-hyprland-helper";
        } else {
            args[arg_index++] = "gsr-hyprland-helper";
        }

        args[arg_index++] = hyprland_socket_path;
        args[arg_index++] = nullptr;

        process_id = exec_program(args, &read_fd, false);
        if(process_id == -1) {
            fprintf(stderr, "Error: DesktopEnvironmentHyprland: failed to execute gsr-hyprland-helper\n");
            return false;
        }

        fcntl(read_fd, F_SETFL, fcntl(read_fd, F_GETFL) | O_NONBLOCK);

        stdout_file = fdopen(read_fd, "r");
        if (!stdout_file) {
            perror("Error: DesktopEnvironmentHyprland: fdopen");
            shutdown();
            return false;
        }
        read_fd = -1;

        fprintf(stderr, "Info: DesktopEnvironmentHyprland: started Hyprland helper process\n");
        return true;
    }

    void DesktopEnvironmentHyprland::shutdown() {
        if(process_id > 0) {
            kill(process_id, SIGKILL);
            int status;
            if(waitpid(process_id, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
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

    void DesktopEnvironmentHyprland::update() {
        while (fgets(line_buffer, sizeof(line_buffer), stdout_file) != nullptr) {
            line = line_buffer;

            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }

            const size_t pos = line.find(prefix);
            if (pos != std::string::npos) {
                window_title = line.substr(pos + prefix.length());
            }
        }
    }

    std::string DesktopEnvironmentHyprland::get_focused_window_title() {
        return window_title;
    }

    // std::string DesktopEnvironmentHyprland::get_focused_monitor_name() {
    //     return "";
    // }
}
