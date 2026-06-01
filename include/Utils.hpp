#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>

namespace gsr {
    struct KeyValue {
        std::string_view key;
        std::string_view value;
    };

    using StringSplitCallback = std::function<bool(std::string_view line)>;

    void string_split_char(std::string_view str, char delimiter, StringSplitCallback callback_func);
    bool starts_with(std::string_view str, const char *substr);
    bool ends_with(std::string_view str, const char *substr);
    std::string strip(const std::string &str);

    std::string get_home_dir();
    std::string get_config_dir();
    std::string get_state_dir();
    std::string get_cache_dir();

    // Whoever designed xdg-user-dirs is retarded. Why are some XDG variables environment variables
    // while others are in this pseudo shell config file ~/.config/user-dirs.dirs
    std::map<std::string, std::string> get_xdg_variables();

    std::string get_videos_dir();
    std::string get_pictures_dir();

    // Returns 0 on success
    int create_directory_recursive(char *path);
    bool file_get_content(const char *filepath, std::string &file_content);
    bool file_overwrite(const char *filepath, const std::string &data);
    bool purge_regular_files_from_cache_dir(const std::string &cache_dir, size_t max_files, const std::unordered_set<std::string> &paths_to_keep, const char *cache_label, const std::function<bool(const std::string&)> &should_keep_path = {}, const std::function<bool(const std::string&)> &remove_path = {}, const std::function<void(const std::string&)> &on_remove_path = {});

    // Returns the path to the parent directory (ignoring trailing /)
    // of "." if there is no parent directory and the directory path is relative
    std::string get_parent_directory(std::string_view directory);

    // XDG Autostart helpers — toggle ~/.config/autostart/gpu-screen-recorder-ui.desktop
    bool is_xdg_autostart_enabled();
    // Returns 0 on success
    int set_xdg_autostart(bool enable);
    void replace_xdg_autostart_with_current_gsr_type();

    // Systemd user service helpers
    bool wait_until_systemd_user_service_available();
    bool is_systemd_service_enabled(const char *service_name);
    bool disable_systemd_service(const char *service_name);

    // True when the current session is a Wayland session running on a compositor
    // we use the wlr-layer-shell native overlay path on (Hyprland, niri, sway, river).
    // Decided purely from environment variables so it can be called before any X11
    // or Wayland connection has been opened.
    bool is_wayland_layer_shell_overlay_session();

    bool has_connector_with_hdr_enabled();
}