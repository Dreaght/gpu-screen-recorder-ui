
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <optional>

namespace gsr {
    struct ConfigHotkey {
        int64_t keysym = 0;
        uint32_t modifiers = 0;
    };

    struct RecordOptions {
        std::string record_area_option = "screen";
        int32_t record_area_width = 0;
        int32_t record_area_height = 0;
        int32_t fps = 60;
        bool merge_audio_tracks = true;
        std::vector<std::string> audio_tracks;
        std::string color_range = "limited";
        std::string video_quality = "very_high";
        std::string video_codec = "auto";
        std::string audio_codec = "opus";
        std::string framerate_mode = "vfr";
        bool advanced_view = false;
        bool overclock = false;
        bool record_cursor = true;
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
        std::string container = "flv";
    };

    struct StreamingConfig {
        RecordOptions record_options;
        bool show_streaming_started_notifications = true;
        bool show_streaming_stopped_notifications = true;
        std::string streaming_service = "twitch";
        YoutubeStreamConfig youtube;
        TwitchStreamConfig twitch;
        CustomStreamConfig custom;
        ConfigHotkey start_stop_recording_hotkey;
    };

    struct RecordConfig {
        RecordOptions record_options;
        bool show_recording_started_notifications = true;
        bool show_video_saved_notifications = true;
        std::string save_directory;
        std::string container = "mp4";
        ConfigHotkey start_stop_recording_hotkey;
        ConfigHotkey pause_unpause_recording_hotkey;
    };

    struct ReplayConfig {
        RecordOptions record_options;
        bool show_replay_started_notifications = true;
        bool show_replay_stopped_notifications = true;
        bool show_replay_saved_notifications = true;
        std::string save_directory;
        std::string container = "mp4";
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

    std::optional<Config> read_config();
    void save_config(Config &config);
}