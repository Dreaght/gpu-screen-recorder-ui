#include "../include/Utils.hpp"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

namespace gsr {
    void string_split_char(std::string_view str, char delimiter, StringSplitCallback callback_func) {
        size_t index = 0;
        while(index < str.size()) {
            size_t new_index = str.find(delimiter, index);
            if(new_index == std::string_view::npos)
                new_index = str.size();

            if(!callback_func(str.substr(index, new_index - index)))
                break;

            index = new_index + 1;
        }
    }

    std::string get_home_dir() {
        const char *home_dir = getenv("HOME");
        if(!home_dir) {
            passwd *pw = getpwuid(getuid());
            home_dir = pw->pw_dir;
        }

        if(!home_dir) {
            fprintf(stderr, "Error: Failed to get home directory of user, using /tmp directory\n");
            home_dir = "/tmp";
        }

        return home_dir;
    }

    std::string get_config_dir() {
        std::string config_dir;
        const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
        if(xdg_config_home) {
            config_dir = xdg_config_home;
        } else {
            config_dir = get_home_dir() + "/.config";
        }
        config_dir += "/gpu-screen-recorder";
        return config_dir;
    }

    // Whoever designed xdg-user-dirs is retarded. Why are some XDG variables environment variables
    // while others are in this pseudo shell config file ~/.config/user-dirs.dirs
    std::map<std::string, std::string> get_xdg_variables() {
        std::string user_dirs_filepath;
        const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
        if(xdg_config_home) {
            user_dirs_filepath = xdg_config_home;
        } else {
            user_dirs_filepath = get_home_dir() + "/.config";
        }

        user_dirs_filepath += "/user-dirs.dirs";

        std::map<std::string, std::string> result;
        FILE *f = fopen(user_dirs_filepath.c_str(), "rb");
        if(!f)
            return result;

        char line[PATH_MAX];
        while(fgets(line, sizeof(line), f)) {
            int len = strlen(line);
            if(len < 2)
                continue;

            if(line[0] == '#')
                continue;

            if(line[len - 1] == '\n') {
                line[len - 1] = '\0';
                len--;
            }

            if(line[len - 1] != '"')
                continue;

            line[len - 1] = '\0';
            len--;

            const char *sep = strchr(line, '=');
            if(!sep)
                continue;

            if(sep[1] != '\"')
                continue;

            std::string value(sep + 2);
            if(strncmp(value.c_str(), "$HOME/", 6) == 0)
                value = get_home_dir() + value.substr(5);

            std::string key(line, sep - line);
            result[std::move(key)] = std::move(value);
        }

        fclose(f);
        return result;
    }

    std::string get_videos_dir() {
        auto xdg_vars = get_xdg_variables();
        std::string xdg_videos_dir = xdg_vars["XDG_VIDEOS_DIR"];
        if(xdg_videos_dir.empty())
            xdg_videos_dir = get_home_dir() + "/Videos";
        return xdg_videos_dir;
    }

    int create_directory_recursive(char *path) {
        int path_len = strlen(path);
        char *p = path;
        char *end = path + path_len;
        for(;;) {
            char *slash_p = strchr(p, '/');

            // Skips first '/', we don't want to try and create the root directory
            if(slash_p == path) {
                ++p;
                continue;
            }

            if(!slash_p)
                slash_p = end;

            char prev_char = *slash_p;
            *slash_p = '\0';
            int err = mkdir(path, S_IRWXU);
            *slash_p = prev_char;

            if(err == -1 && errno != EEXIST)
                return err;

            if(slash_p == end)
                break;
            else
                p = slash_p + 1;
        }
        return 0;
    }

    bool file_get_content(const char *filepath, std::string &file_content) {
        file_content.clear();
        bool success = false;

        FILE *file = fopen(filepath, "rb");
        if(!file)
            return success;

        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        if(file_size != -1) {
            file_content.resize(file_size);
            fseek(file, 0, SEEK_SET);
            if((long)fread(&file_content[0], 1, file_size, file) == file_size)
                success = true;
        }

        fclose(file);
        return success;
    }

    bool file_overwrite(const char *filepath, const std::string &data) {
        bool success = false;

        FILE *file = fopen(filepath, "wb");
        if(!file)
            return success;

        if(fwrite(data.data(), 1, data.size(), file) == data.size())
            success = true;

        fclose(file);
        return success;
    }

    std::string get_parent_directory(std::string_view directory) {
        std::string result;

        while(directory.size() > 1 && directory.back() == '/') {
            directory.remove_suffix(1);
        }

        const size_t prev_slash_index = directory.rfind('/');
        if(prev_slash_index == 0) {
            result = "/";
        } else if(prev_slash_index == std::string_view::npos) {
            result = ".";
        } else {
            result = directory.substr(0, prev_slash_index);
        }
        return result;
    }
}