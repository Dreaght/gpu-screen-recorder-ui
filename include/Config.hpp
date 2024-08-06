
#pragma once

#include "Utils.hpp"
#include <stdint.h>

namespace gsr {
    struct ConfigHotkey {
        int64_t keysym = 0;
        uint32_t modifiers = 0;
    };

    struct RecordOptions {
        std::string record_area_option;
        int32_t record_area_width = 0;
        int32_t record_area_height = 0;
        int32_t fps = 60;
        bool merge_audio_tracks = true;
        std::vector<std::string> audio_input;
        std::string color_range;
        std::string quality;
        std::string video_codec;
        std::string audio_codec;
        std::string framerate_mode;
        bool advanced_view = false;
        bool overclock = false;
        bool show_recording_started_notifications = false;
        bool show_recording_stopped_notifications = false;
        bool show_recording_saved_notifications = true;
        bool record_cursor = true;
        bool hide_window_when_recording = false;
        bool restore_portal_session = true;
    };

    struct MainConfig {
        int32_t config_file_version = 0;
        bool software_encoding_warning_shown = false;
    };

    struct YoutubeStreamConfig {
        std::string stream_key;
    };

    struct TwitchStreamConfig {
        std::string stream_key;
    };

    struct CustomStreamConfig {
        std::string url;
        std::string container;
    };

    struct StreamingConfig {
        RecordOptions record_options;
        std::string streaming_service;
        YoutubeStreamConfig youtube;
        TwitchStreamConfig twitch;
        CustomStreamConfig custom;
        ConfigHotkey start_stop_recording_hotkey;
    };

    struct RecordConfig {
        RecordOptions record_options;
        std::string save_directory;
        std::string container;
        ConfigHotkey start_stop_recording_hotkey;
        ConfigHotkey pause_unpause_recording_hotkey;
    };

    struct ReplayConfig {
        RecordOptions record_options;
        std::string save_directory;
        std::string container;
        int32_t replay_time = 60;
        ConfigHotkey start_stop_recording_hotkey;
        ConfigHotkey save_recording_hotkey;
    };

    struct Config {
        MainConfig main_config;
        StreamingConfig streaming_config;
        RecordConfig record_config;
        ReplayConfig replay_config;
    };

    Config read_config(bool &config_empty);
    void save_config(Config &config);
}