#include "../include/Config.hpp"
#include <variant>
#include <limits.h>
#include <inttypes.h>
#include <libgen.h>

namespace gsr {
    #define FORMAT_I32 "%" PRIi32
    #define FORMAT_I64 "%" PRIi64
    #define FORMAT_U32 "%" PRIu32

    using ConfigValue = std::variant<bool*, std::string*, int32_t*, ConfigHotkey*, std::vector<std::string>*>;

    static std::map<std::string_view, ConfigValue> get_config_options(Config &config) {
        return {
            {"main.record_area_option", &config.main_config.record_area_option},
            {"main.record_area_width", &config.main_config.record_area_width},
            {"main.record_area_height", &config.main_config.record_area_height},
            {"main.fps", &config.main_config.fps},
            {"main.merge_audio_tracks", &config.main_config.merge_audio_tracks},
            {"main.audio_input", &config.main_config.audio_input},
            {"main.color_range", &config.main_config.color_range},
            {"main.quality", &config.main_config.quality},
            {"main.codec", &config.main_config.video_codec},
            {"main.audio_codec", &config.main_config.audio_codec},
            {"main.framerate_mode", &config.main_config.framerate_mode},
            {"main.advanced_view", &config.main_config.advanced_view},
            {"main.overclock", &config.main_config.overclock},
            {"main.show_recording_started_notifications", &config.main_config.show_recording_started_notifications},
            {"main.show_recording_stopped_notifications", &config.main_config.show_recording_stopped_notifications},
            {"main.show_recording_saved_notifications", &config.main_config.show_recording_saved_notifications},
            {"main.record_cursor", &config.main_config.record_cursor},
            {"main.hide_window_when_recording", &config.main_config.hide_window_when_recording},
            {"main.software_encoding_warning_shown", &config.main_config.software_encoding_warning_shown},
            {"main.restore_portal_session", &config.main_config.restore_portal_session},

            {"streaming.service", &config.streaming_config.streaming_service},
            {"streaming.youtube.key", &config.streaming_config.youtube.stream_key},
            {"streaming.twitch.key", &config.streaming_config.twitch.stream_key},
            {"streaming.custom.url", &config.streaming_config.custom.url},
            {"streaming.custom.container", &config.streaming_config.custom.container},
            {"streaming.start_stop_recording_hotkey", &config.streaming_config.start_stop_recording_hotkey},

            {"record.save_directory", &config.record_config.save_directory},
            {"record.container", &config.record_config.container},
            {"record.start_stop_recording_hotkey", &config.record_config.start_stop_recording_hotkey},
            {"record.pause_unpause_recording_hotkey", &config.record_config.pause_unpause_recording_hotkey},

            {"replay.save_directory", &config.replay_config.save_directory},
            {"replay.container", &config.replay_config.container},
            {"replay.time", &config.replay_config.replay_time},
            {"replay.start_stop_recording_hotkey", &config.replay_config.start_stop_recording_hotkey},
            {"replay.save_recording_hotkey", &config.replay_config.save_recording_hotkey}
        };
    }

    Config read_config(bool &config_empty) {
        Config config;

        const std::string config_path = get_config_dir() + "/config";
        std::string file_content;
        if(!file_get_content(config_path.c_str(), file_content)) {
            fprintf(stderr, "Warning: Failed to read config file: %s\n", config_path.c_str());
            config_empty = true;
            return config;
        }

        auto config_options = get_config_options(config);

        string_split_char(file_content, '\n', [&](std::string_view line) {
            const std::optional<KeyValue> key_value = parse_key_value(line);
            if(!key_value) {
                fprintf(stderr, "Warning: Invalid config option format: %.*s\n", (int)line.size(), line.data());
                return true;
            }

            if(key_value->key.empty() || key_value->value.empty())
                return true;

            auto it = config_options.find(key_value->key);
            if(it == config_options.end())
                return true;

            if(std::holds_alternative<bool*>(it->second)) {
                *std::get<bool*>(it->second) = key_value->value == "true";
            } else if(std::holds_alternative<std::string*>(it->second)) {
                std::get<std::string*>(it->second)->assign(key_value->value.data(), key_value->value.size());
            } else if(std::holds_alternative<int32_t*>(it->second)) {
                std::string value_str(key_value->value);
                int32_t *value = std::get<int32_t*>(it->second);
                if(sscanf(value_str.c_str(), FORMAT_I32, value) != 1) {
                    fprintf(stderr, "Warning: Invalid config option value for %.*s\n", (int)key_value->key.size(), key_value->key.data());
                    *value = 0;
                }
            } else if(std::holds_alternative<ConfigHotkey*>(it->second)) {
                std::string value_str(key_value->value);
                ConfigHotkey *config_hotkey = std::get<ConfigHotkey*>(it->second);
                if(sscanf(value_str.c_str(), FORMAT_I64 " " FORMAT_U32, &config_hotkey->keysym, &config_hotkey->modifiers) != 2) {
                    fprintf(stderr, "Warning: Invalid config option value for %.*s\n", (int)key_value->key.size(), key_value->key.data());
                    config_hotkey->keysym = 0;
                    config_hotkey->modifiers = 0;
                }
            } else if(std::holds_alternative<ConfigHotkey*>(it->second)) {
                std::string array_value(key_value->value);
                std::get<std::vector<std::string>*>(it->second)->push_back(std::move(array_value));
            }

            return true;
        });

        return config;
    }

    void save_config(Config &config) {
        const std::string config_path = get_config_dir() + "/config";

        char dir_tmp[PATH_MAX];
        snprintf(dir_tmp, sizeof(dir_tmp), "%s", config_path.c_str());
        char *dir = dirname(dir_tmp);

        if(create_directory_recursive(dir) != 0) {
            fprintf(stderr, "Warning: Failed to create config directory: %s\n", dir);
            return;
        }

        FILE *file = fopen(config_path.c_str(), "wb");
        if(!file) {
            fprintf(stderr, "Warning: Failed to create config file: %s\n", config_path.c_str());
            return;
        }

        const auto config_options = get_config_options(config);
        for(auto it : config_options) {
            if(std::holds_alternative<bool*>(it.second)) {
                fprintf(file, "%.*s %s\n", (int)it.first.size(), it.first.data(), *std::get<bool*>(it.second) ? "true" : "false");
            } else if(std::holds_alternative<std::string*>(it.second)) {
                fprintf(file, "%.*s %s\n", (int)it.first.size(), it.first.data(), std::get<std::string*>(it.second)->c_str());
            } else if(std::holds_alternative<int32_t*>(it.second)) {
                fprintf(file, "%.*s " FORMAT_I32 "\n", (int)it.first.size(), it.first.data(), *std::get<int32_t*>(it.second));
            } else if(std::holds_alternative<ConfigHotkey*>(it.second)) {
                const ConfigHotkey *config_hotkey = std::get<ConfigHotkey*>(it.second);
                    fprintf(file, "%.*s " FORMAT_I64 " " FORMAT_U32 "\n", (int)it.first.size(), it.first.data(), config_hotkey->keysym, config_hotkey->modifiers);
            } else if(std::holds_alternative<ConfigHotkey*>(it.second)) {
                std::vector<std::string> *array = std::get<std::vector<std::string>*>(it.second);
                for(const std::string &value : *array) {
                    fprintf(file, "%.*s %s\n", (int)it.first.size(), it.first.data(), value.c_str());
                }
            }
        }

        fclose(file);
    }
}