#include "../include/HyprlandWorkaround.hpp"
#include "../include/Process.hpp"

#include <sys/wait.h>
#include <unistd.h>
#include <thread>

namespace gsr {
    static ActiveHyprlandWindow active_hyprland_window;
    static bool hyprland_listener_thread_started = false;

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

    static void hyprland_listener_thread() {
        char hyprland_socket_path[256];
        char buffer[4096];
        const std::string prefix = "Window title changed: ";
        std::string line;
        FILE *stdout_file = nullptr;

        // Get path inside the flatpak before flatpak-spawn is called because of a bug in flatpak:
        // https://github.com/flatpak/flatpak/issues/6486
        // where environment variables are missing in flatpak-spawn --host
        if(!get_hyprland_socket_path(hyprland_socket_path, sizeof(hyprland_socket_path))) {
            fprintf(stderr, "Error: HyprlandWorkaround: failed to get hyprland socket path\n");
            return;
        }

        const bool inside_flatpak = access("/app/manifest.json", F_OK) == 0;

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

        int read_fd = -1;
        const pid_t process_id = exec_program(args, &read_fd, false);
        if(process_id == -1) {
            fprintf(stderr, "Error: HyprlandWorkaround: failed to execute gsr-hyprland-helper\n");
            return;
        }

        stdout_file = fdopen(read_fd, "r");
        if (!stdout_file) {
            perror("Error: HyprlandWorkaround: fdopen");
            goto done;
            return;
        }
        read_fd = -1;

        fprintf(stderr, "Info: HyprlandWorkaround: started Hyprland helper thread\n");

        while (fgets(buffer, sizeof(buffer), stdout_file) != nullptr) {
            line = buffer;

            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }

            size_t pos = line.find(prefix);
            if (pos != std::string::npos) {
                active_hyprland_window.title = line.substr(pos + prefix.length());
            }
        }

        done:
        if(stdout_file)
            fclose(stdout_file);

        if(read_fd > 0)
            close(read_fd);

        if(process_id > 0) {
            kill(process_id, SIGKILL);
            int status;
            if(waitpid(process_id, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }
        }
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
