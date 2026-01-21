#include "../include/HyprlandWorkaround.hpp"

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
    static ActiveHyprlandWindow *active_hyprland_window = nullptr;
    static bool hyprland_listener_thread_started = false;

    static std::string get_hyprland_socket_path() {
        const char* xdg_runtime_dir = std::getenv("XDG_RUNTIME_DIR");
        const char* instance_sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");

        if (!xdg_runtime_dir || !instance_sig) {
            std::cerr << "Error: Env vars XDG_RUNTIME_DIR or HYPRLAND_INSTANCE_SIGNATURE not set." << std::endl;
            exit(1);
        }

        return std::string(xdg_runtime_dir) + "/hypr/" + instance_sig + "/.socket2.sock";
    }

    static void hyprland_listener_thread() {
        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_fd == -1) {
            perror("socket");
            return;
        }

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;

        std::string socket_path = get_hyprland_socket_path();
        std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("connect");
            close(sock_fd);
            return;
        }

        std::cout << "Connected to Hyprland event socket: " << socket_path << std::endl;

        std::array<char, 4096> buffer; // fixed buffer, scary!
        std::string incomplete_line;

        while (hyprland_listener_thread_started) {
            ssize_t bytes_read = read(sock_fd, buffer.data(), buffer.size());

            if (bytes_read == 0) {
                std::cerr << "Connection closed by Hyprland" << std::endl;
                hyprland_listener_thread_started = false;
                break;
            }

            if (bytes_read < 0) {
                perror("read");
                break;
            }

            std::string chunk(buffer.data(), bytes_read);
            std::string data = incomplete_line + chunk;

            size_t start_pos = 0;
            size_t newline_pos;

            while ((newline_pos = data.find('\n', start_pos)) != std::string::npos) {
                std::string line = data.substr(start_pos, newline_pos - start_pos);

                size_t delimiter = line.find(">>");

                if (delimiter != std::string::npos) {
                    std::string event_name = line.substr(0, delimiter);
                    std::string event_data = line.substr(delimiter+2);

                    if (event_name == "activewindowv2") {
                        active_hyprland_window->window_id = event_data;
                    }

                    if (event_name == "activewindow") {
                        size_t title_delimiter = event_data.find(",");
                        if (title_delimiter != std::string::npos) {
                            std::string window_title = event_data.substr(title_delimiter+1);
                            if (window_title == "gsr ui" || window_title == "gsr notify") {
                                // ignoring ourselves
                                start_pos = newline_pos + 1;
                                continue;
                            }
                            active_hyprland_window->title = window_title;
                        }

                    }

                    if (event_name == "windowtitlev2") {
                        size_t data_delimiter = event_data.find(",");
                        if (data_delimiter != std::string::npos) {
                            std::string win_id = event_data.substr(0, data_delimiter);
                            std::string new_window_title = event_data.substr(data_delimiter+1);
                            if (new_window_title == "gsr ui" || new_window_title == "gsr notify") {
                                // ignoring ourselves
                                start_pos = newline_pos + 1;
                                continue;
                            }
                            if (active_hyprland_window->window_id != win_id) {
                                // not our case
                                start_pos = newline_pos + 1;
                                continue;
                            }
                            active_hyprland_window->title = new_window_title;
                        }
                    }
                }

                start_pos = newline_pos + 1;
            }

            if (start_pos < data.length()) {
                incomplete_line = data.substr(start_pos);
            } else {
                incomplete_line.clear();
            }
        }

        close(sock_fd);
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