#include "../include/Config.hpp"
#include "../include/Utils.hpp"
#include "../include/GsrInfo.hpp"
#include "../include/GlobalHotkeys.hpp"
#include <variant>
#include <limits.h>
#include <inttypes.h>
#include <libgen.h>
#include <assert.h>
#include <mglpp/window/Keyboard.hpp>

#define FORMAT_I32 "%" PRIi32
#define FORMAT_I64 "%" PRIi64
#define FORMAT_U32 "%" PRIu32

namespace gsr {
    static std::vector<mgl::Keyboard::Key> hotkey_modifiers_to_mgl_keys(uint32_t modifiers) {
        std::vector<mgl::Keyboard::Key> result;
        if(modifiers & HOTKEY_MOD_LCTRL)
            result.push_back(mgl::Keyboard::LControl);
        if(modifiers & HOTKEY_MOD_LSHIFT)
            result.push_back(mgl::Keyboard::LShift);
        if(modifiers & HOTKEY_MOD_LALT)
            result.push_back(mgl::Keyboard::LAlt);
        if(modifiers & HOTKEY_MOD_LSUPER)
            result.push_back(mgl::Keyboard::LSystem);
        if(modifiers & HOTKEY_MOD_RCTRL)
            result.push_back(mgl::Keyboard::RControl);
        if(modifiers & HOTKEY_MOD_RSHIFT)
            result.push_back(mgl::Keyboard::RShift);
        if(modifiers & HOTKEY_MOD_RALT)
            result.push_back(mgl::Keyboard::RAlt);
        if(modifiers & HOTKEY_MOD_RSUPER)
            result.push_back(mgl::Keyboard::RSystem);
        return result;
    }

    static void string_remove_all(std::string &str, const std::string &substr) {
        size_t index = 0;
        while(true) {
            index = str.find(substr, index);
            if(index == std::string::npos)
                break;
            str.erase(index, substr.size());
        }
    }

    bool ConfigHotkey::operator==(const ConfigHotkey &other) const {
        return key == other.key && modifiers == other.modifiers;
    }

    bool ConfigHotkey::operator!=(const ConfigHotkey &other) const {
        return !operator==(other);
    }

    std::string ConfigHotkey::to_string(bool spaces, bool modifier_side) const {
        std::string result;

        const std::vector<mgl::Keyboard::Key> modifier_keys = hotkey_modifiers_to_mgl_keys(modifiers);
        std::string modifier_str;
        for(const mgl::Keyboard::Key modifier_key : modifier_keys) {
            if(!result.empty()) {
                if(spaces)
                    result += " + ";
                else
                    result += "+";
            }

            modifier_str = mgl::Keyboard::key_to_string(modifier_key);
            if(!modifier_side) {
                string_remove_all(modifier_str, "Left");
                string_remove_all(modifier_str, "Right");
            }
            result += modifier_str;
        }

        if(key != 0) {
            if(!result.empty()) {
                if(spaces)
                    result += " + ";
                else
                    result += "+";
            }
            result += mgl::Keyboard::key_to_string((mgl::Keyboard::Key)key);
        }

        return result;
    }

    Config::Config(const SupportedCaptureOptions &capture_options) {
        const std::string default_videos_save_directory = get_videos_dir();
        const std::string default_pictures_save_directory = get_pictures_dir();

        set_hotkeys_to_default();

        streaming_config.record_options.video_quality = "custom";
        streaming_config.record_options.audio_tracks.push_back("default_output");
        streaming_config.record_options.video_bitrate = 15000;

        record_config.save_directory = default_videos_save_directory;
        record_config.record_options.audio_tracks.push_back("default_output");
        record_config.record_options.video_bitrate = 45000;

        replay_config.record_options.video_quality = "custom";
        replay_config.save_directory = default_videos_save_directory;
        replay_config.record_options.audio_tracks.push_back("default_output");
        replay_config.record_options.video_bitrate = 45000;

        screenshot_config.save_directory = default_pictures_save_directory;

        if(!capture_options.monitors.empty()) {
            streaming_config.record_options.record_area_option = capture_options.monitors.front().name;
            record_config.record_options.record_area_option = capture_options.monitors.front().name;
            replay_config.record_options.record_area_option = capture_options.monitors.front().name;
            screenshot_config.record_area_option = capture_options.monitors.front().name;
        }
    }

    void Config::set_hotkeys_to_default() {
        streaming_config.start_stop_hotkey = {mgl::Keyboard::F8, HOTKEY_MOD_LALT};

        record_config.start_stop_hotkey = {mgl::Keyboard::F9, HOTKEY_MOD_LALT};
        record_config.pause_unpause_hotkey = {mgl::Keyboard::F7, HOTKEY_MOD_LALT};

        replay_config.start_stop_hotkey = {mgl::Keyboard::F10, HOTKEY_MOD_LALT | HOTKEY_MOD_LSHIFT};
        replay_config.save_hotkey = {mgl::Keyboard::F10, HOTKEY_MOD_LALT};

        screenshot_config.take_screenshot_hotkey = {mgl::Keyboard::F1, HOTKEY_MOD_LALT};

        main_config.show_hide_hotkey = {mgl::Keyboard::Z, HOTKEY_MOD_LALT};
    }

    static std::optional<KeyValue> parse_key_value(std::string_view line) {
        const size_t space_index = line.find(' ');
        if(space_index == std::string_view::npos)
            return std::nullopt;
        return KeyValue{line.substr(0, space_index), line.substr(space_index + 1)};
    }

    using ConfigValue = std::variant<bool*, std::string*, int32_t*, ConfigHotkey*, std::vector<std::string>*>;

    static std::map<std::string_view, ConfigValue> get_config_options(Config &config) {
        return {
            {"main.config_file_version", &config.main_config.config_file_version},
            {"main.software_encoding_warning_shown", &config.main_config.software_encoding_warning_shown},
            {"main.hotkeys_enable_option", &config.main_config.hotkeys_enable_option},
            {"main.joystick_hotkeys_enable_option", &config.main_config.joystick_hotkeys_enable_option},
            {"main.tint_color", &config.main_config.tint_color},
            {"main.show_hide_hotkey", &config.main_config.show_hide_hotkey},

            {"streaming.record_options.record_area_option", &config.streaming_config.record_options.record_area_option},
            {"streaming.record_options.record_area_width", &config.streaming_config.record_options.record_area_width},
            {"streaming.record_options.record_area_height", &config.streaming_config.record_options.record_area_height},
            {"streaming.record_options.video_width", &config.streaming_config.record_options.video_width},
            {"streaming.record_options.video_height", &config.streaming_config.record_options.video_height},
            {"streaming.record_options.fps", &config.streaming_config.record_options.fps},
            {"streaming.record_options.video_bitrate", &config.streaming_config.record_options.video_bitrate},
            {"streaming.record_options.merge_audio_tracks", &config.streaming_config.record_options.merge_audio_tracks},
            {"streaming.record_options.application_audio_invert", &config.streaming_config.record_options.application_audio_invert},
            {"streaming.record_options.change_video_resolution", &config.streaming_config.record_options.change_video_resolution},
            {"streaming.record_options.audio_track", &config.streaming_config.record_options.audio_tracks},
            {"streaming.record_options.color_range", &config.streaming_config.record_options.color_range},
            {"streaming.record_options.video_quality", &config.streaming_config.record_options.video_quality},
            {"streaming.record_options.codec", &config.streaming_config.record_options.video_codec},
            {"streaming.record_options.audio_codec", &config.streaming_config.record_options.audio_codec},
            {"streaming.record_options.framerate_mode", &config.streaming_config.record_options.framerate_mode},
            {"streaming.record_options.advanced_view", &config.streaming_config.record_options.advanced_view},
            {"streaming.record_options.overclock", &config.streaming_config.record_options.overclock},
            {"streaming.record_options.record_cursor", &config.streaming_config.record_options.record_cursor},
            {"streaming.record_options.restore_portal_session", &config.streaming_config.record_options.restore_portal_session},
            {"streaming.show_streaming_started_notifications", &config.streaming_config.show_streaming_started_notifications},
            {"streaming.show_streaming_stopped_notifications", &config.streaming_config.show_streaming_stopped_notifications},
            {"streaming.service", &config.streaming_config.streaming_service},
            {"streaming.youtube.key", &config.streaming_config.youtube.stream_key},
            {"streaming.twitch.key", &config.streaming_config.twitch.stream_key},
            {"streaming.custom.url", &config.streaming_config.custom.url},
            {"streaming.custom.container", &config.streaming_config.custom.container},
            {"streaming.start_stop_hotkey", &config.streaming_config.start_stop_hotkey},

            {"record.record_options.record_area_option", &config.record_config.record_options.record_area_option},
            {"record.record_options.record_area_width", &config.record_config.record_options.record_area_width},
            {"record.record_options.record_area_height", &config.record_config.record_options.record_area_height},
            {"record.record_options.video_width", &config.record_config.record_options.video_width},
            {"record.record_options.video_height", &config.record_config.record_options.video_height},
            {"record.record_options.fps", &config.record_config.record_options.fps},
            {"record.record_options.video_bitrate", &config.record_config.record_options.video_bitrate},
            {"record.record_options.merge_audio_tracks", &config.record_config.record_options.merge_audio_tracks},
            {"record.record_options.application_audio_invert", &config.record_config.record_options.application_audio_invert},
            {"record.record_options.change_video_resolution", &config.record_config.record_options.change_video_resolution},
            {"record.record_options.audio_track", &config.record_config.record_options.audio_tracks},
            {"record.record_options.color_range", &config.record_config.record_options.color_range},
            {"record.record_options.video_quality", &config.record_config.record_options.video_quality},
            {"record.record_options.codec", &config.record_config.record_options.video_codec},
            {"record.record_options.audio_codec", &config.record_config.record_options.audio_codec},
            {"record.record_options.framerate_mode", &config.record_config.record_options.framerate_mode},
            {"record.record_options.advanced_view", &config.record_config.record_options.advanced_view},
            {"record.record_options.overclock", &config.record_config.record_options.overclock},
            {"record.record_options.record_cursor", &config.record_config.record_options.record_cursor},
            {"record.record_options.restore_portal_session", &config.record_config.record_options.restore_portal_session},
            {"record.save_video_in_game_folder", &config.record_config.save_video_in_game_folder},
            {"record.show_recording_started_notifications", &config.record_config.show_recording_started_notifications},
            {"record.show_video_saved_notifications", &config.record_config.show_video_saved_notifications},
            {"record.save_directory", &config.record_config.save_directory},
            {"record.container", &config.record_config.container},
            {"record.start_stop_hotkey", &config.record_config.start_stop_hotkey},
            {"record.pause_unpause_hotkey", &config.record_config.pause_unpause_hotkey},

            {"replay.record_options.record_area_option", &config.replay_config.record_options.record_area_option},
            {"replay.record_options.record_area_width", &config.replay_config.record_options.record_area_width},
            {"replay.record_options.record_area_height", &config.replay_config.record_options.record_area_height},
            {"replay.record_options.video_width", &config.replay_config.record_options.video_width},
            {"replay.record_options.video_height", &config.replay_config.record_options.video_height},
            {"replay.record_options.fps", &config.replay_config.record_options.fps},
            {"replay.record_options.video_bitrate", &config.replay_config.record_options.video_bitrate},
            {"replay.record_options.merge_audio_tracks", &config.replay_config.record_options.merge_audio_tracks},
            {"replay.record_options.application_audio_invert", &config.replay_config.record_options.application_audio_invert},
            {"replay.record_options.change_video_resolution", &config.replay_config.record_options.change_video_resolution},
            {"replay.record_options.audio_track", &config.replay_config.record_options.audio_tracks},
            {"replay.record_options.color_range", &config.replay_config.record_options.color_range},
            {"replay.record_options.video_quality", &config.replay_config.record_options.video_quality},
            {"replay.record_options.codec", &config.replay_config.record_options.video_codec},
            {"replay.record_options.audio_codec", &config.replay_config.record_options.audio_codec},
            {"replay.record_options.framerate_mode", &config.replay_config.record_options.framerate_mode},
            {"replay.record_options.advanced_view", &config.replay_config.record_options.advanced_view},
            {"replay.record_options.overclock", &config.replay_config.record_options.overclock},
            {"replay.record_options.record_cursor", &config.replay_config.record_options.record_cursor},
            {"replay.record_options.restore_portal_session", &config.replay_config.record_options.restore_portal_session},
            {"replay.turn_on_replay_automatically_mode", &config.replay_config.turn_on_replay_automatically_mode},
            {"replay.save_video_in_game_folder", &config.replay_config.save_video_in_game_folder},
            {"replay.restart_replay_on_save", &config.replay_config.restart_replay_on_save},
            {"replay.show_replay_started_notifications", &config.replay_config.show_replay_started_notifications},
            {"replay.show_replay_stopped_notifications", &config.replay_config.show_replay_stopped_notifications},
            {"replay.show_replay_saved_notifications", &config.replay_config.show_replay_saved_notifications},
            {"replay.save_directory", &config.replay_config.save_directory},
            {"replay.container", &config.replay_config.container},
            {"replay.time", &config.replay_config.replay_time},
            {"replay.start_stop_hotkey", &config.replay_config.start_stop_hotkey},
            {"replay.save_hotkey", &config.replay_config.save_hotkey},

            {"screenshot.record_area_option", &config.screenshot_config.record_area_option},
            {"screenshot.image_width", &config.screenshot_config.image_width},
            {"screenshot.image_height", &config.screenshot_config.image_height},
            {"screenshot.change_image_resolution", &config.screenshot_config.change_image_resolution},
            {"screenshot.image_quality", &config.screenshot_config.image_quality},
            {"screenshot.image_format", &config.screenshot_config.image_format},
            {"screenshot.record_cursor", &config.screenshot_config.record_cursor},
            {"screenshot.restore_portal_session", &config.screenshot_config.restore_portal_session},
            {"screenshot.save_screenshot_in_game_folder", &config.screenshot_config.save_screenshot_in_game_folder},
            {"screenshot.show_screenshot_saved_notifications", &config.screenshot_config.show_screenshot_saved_notifications},
            {"screenshot.save_directory", &config.screenshot_config.save_directory},
            {"screenshot.take_screenshot_hotkey", &config.screenshot_config.take_screenshot_hotkey}
        };
    }

    bool Config::operator==(const Config &other) {
        const auto config_options = get_config_options(*this);
        const auto config_options_other = get_config_options(const_cast<Config&>(other));
        for(auto it : config_options) {
            auto it_other = config_options_other.find(it.first);
            if(it_other == config_options_other.end() || it_other->second.index() != it.second.index())
                return false;

            if(std::holds_alternative<bool*>(it.second)) {
                if(*std::get<bool*>(it.second) != *std::get<bool*>(it_other->second))
                    return false;
            } else if(std::holds_alternative<std::string*>(it.second)) {
                if(*std::get<std::string*>(it.second) != *std::get<std::string*>(it_other->second))
                    return false;
            } else if(std::holds_alternative<int32_t*>(it.second)) {
                if(*std::get<int32_t*>(it.second) != *std::get<int32_t*>(it_other->second))
                    return false;
            } else if(std::holds_alternative<ConfigHotkey*>(it.second)) {
                if(*std::get<ConfigHotkey*>(it.second) != *std::get<ConfigHotkey*>(it_other->second))
                    return false;
            } else if(std::holds_alternative<std::vector<std::string>*>(it.second)) {
                if(*std::get<std::vector<std::string>*>(it.second) != *std::get<std::vector<std::string>*>(it_other->second))
                    return false;
            } else {
                assert(false);
            }
        }
        return true;
    }

    bool Config::operator!=(const Config &other) {
        return !operator==(other);
    }

    std::optional<Config> read_config(const SupportedCaptureOptions &capture_options) {
        std::optional<Config> config;

        const std::string config_path = get_config_dir() + "/config_ui";
        std::string file_content;
        if(!file_get_content(config_path.c_str(), file_content)) {
            fprintf(stderr, "Warning: Failed to read config file: %s\n", config_path.c_str());
            return config;
        }

        config = Config(capture_options);
        config->streaming_config.record_options.audio_tracks.clear();
        config->record_config.record_options.audio_tracks.clear();
        config->replay_config.record_options.audio_tracks.clear();

        auto config_options = get_config_options(config.value());

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
                if(sscanf(value_str.c_str(), FORMAT_I64 " " FORMAT_U32, &config_hotkey->key, &config_hotkey->modifiers) != 2) {
                    fprintf(stderr, "Warning: Invalid config option value for %.*s\n", (int)key_value->key.size(), key_value->key.data());
                    config_hotkey->key = 0;
                    config_hotkey->modifiers = 0;
                }
            } else if(std::holds_alternative<std::vector<std::string>*>(it->second)) {
                std::string array_value(key_value->value);
                std::get<std::vector<std::string>*>(it->second)->push_back(std::move(array_value));
            } else {
                assert(false);
            }

            return true;
        });

        if(config->main_config.config_file_version != GSR_CONFIG_FILE_VERSION) {
            fprintf(stderr, "Info: the config file is outdated, resetting it\n");
            config = std::nullopt;
        }

        return config;
    }

    void save_config(Config &config) {
        config.main_config.config_file_version = GSR_CONFIG_FILE_VERSION;

        const std::string config_path = get_config_dir() + "/config_ui";

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
                    fprintf(file, "%.*s " FORMAT_I64 " " FORMAT_U32 "\n", (int)it.first.size(), it.first.data(), config_hotkey->key, config_hotkey->modifiers);
            } else if(std::holds_alternative<std::vector<std::string>*>(it.second)) {
                std::vector<std::string> *array = std::get<std::vector<std::string>*>(it.second);
                for(const std::string &value : *array) {
                    fprintf(file, "%.*s %s\n", (int)it.first.size(), it.first.data(), value.c_str());
                }
            } else {
                assert(false);
            }
        }

        fclose(file);
    }
}