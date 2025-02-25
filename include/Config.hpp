
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <optional>

#define GSR_CONFIG_FILE_VERSION 1

namespace gsr {
    struct SupportedCaptureOptions;

    enum class ReplayStartupMode {
        DONT_TURN_ON_AUTOMATICALLY,
        TURN_ON_AT_SYSTEM_STARTUP,
        TURN_ON_AT_FULLSCREEN,
        TURN_ON_AT_POWER_SUPPLY_CONNECTED
    };

    ReplayStartupMode replay_startup_string_to_type(const char *startup_mode_str);

    struct ConfigHotkey {
        int64_t key = 0;        // Mgl key
        uint32_t modifiers = 0; // HotkeyModifier

        bool operator==(const ConfigHotkey &other) const;
        bool operator!=(const ConfigHotkey &other) const;

        std::string to_string(bool spaces = true, bool modifier_side = true) const;
    };

    struct RecordOptions {
        std::string record_area_option = "screen";
        int32_t record_area_width = 0;
        int32_t record_area_height = 0;
        int32_t video_width = 0;
        int32_t video_height = 0;
        int32_t fps = 60;
        int32_t video_bitrate = 15000;
        bool merge_audio_tracks = true; // Currently unused for streaming because all known streaming sites only support 1 audio track
        bool application_audio_invert = false;
        bool change_video_resolution = false;
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
        int32_t config_file_version = GSR_CONFIG_FILE_VERSION;
        bool software_encoding_warning_shown = false;
        std::string hotkeys_enable_option = "enable_hotkeys";
        std::string joystick_hotkeys_enable_option = "disable_hotkeys";
        std::string tint_color;
        ConfigHotkey show_hide_hotkey;
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
        ConfigHotkey start_stop_hotkey;
    };

    struct RecordConfig {
        RecordOptions record_options;
        bool save_video_in_game_folder = false;
        bool show_recording_started_notifications = true;
        bool show_video_saved_notifications = true;
        std::string save_directory;
        std::string container = "mp4";
        ConfigHotkey start_stop_hotkey;
        ConfigHotkey pause_unpause_hotkey;
    };

    struct ReplayConfig {
        RecordOptions record_options;
        std::string turn_on_replay_automatically_mode = "dont_turn_on_automatically";
        bool save_video_in_game_folder = false;
        bool restart_replay_on_save = false;
        bool show_replay_started_notifications = true;
        bool show_replay_stopped_notifications = true;
        bool show_replay_saved_notifications = true;
        std::string save_directory;
        std::string container = "mp4";
        int32_t replay_time = 60;
        ConfigHotkey start_stop_hotkey;
        ConfigHotkey save_hotkey;
    };

    struct ScreenshotConfig {
        std::string record_area_option = "screen";
        int32_t image_width = 0;
        int32_t image_height = 0;
        bool change_image_resolution = false;
        std::string image_quality = "very_high";
        std::string image_format = "jpg";
        bool record_cursor = true;
        bool restore_portal_session = true;

        bool save_screenshot_in_game_folder = false;
        bool show_screenshot_saved_notifications = true;
        std::string save_directory;
        ConfigHotkey take_screenshot_hotkey;
    };

    struct Config {
        Config(const SupportedCaptureOptions &capture_options);
        bool operator==(const Config &other);
        bool operator!=(const Config &other);

        void set_hotkeys_to_default();

        MainConfig main_config;
        StreamingConfig streaming_config;
        RecordConfig record_config;
        ReplayConfig replay_config;
        ScreenshotConfig screenshot_config;
    };

    std::optional<Config> read_config(const SupportedCaptureOptions &capture_options);
    void save_config(Config &config);
}