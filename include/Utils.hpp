#pragma once

#include <functional>
#include <string_view>
#include <map>
#include <string>

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

    // Whoever designed xdg-user-dirs is retarded. Why are some XDG variables environment variables
    // while others are in this pseudo shell config file ~/.config/user-dirs.dirs
    std::map<std::string, std::string> get_xdg_variables();

    std::string get_videos_dir();
    std::string get_pictures_dir();

    // Returns 0 on success
    int create_directory_recursive(char *path);
    bool file_get_content(const char *filepath, std::string &file_content);
    bool file_overwrite(const char *filepath, const std::string &data);

    // Returns the path to the parent directory (ignoring trailing /)
    // of "." if there is no parent directory and the directory path is relative
    std::string get_parent_directory(std::string_view directory);
}