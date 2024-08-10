#pragma once

#include "StaticPage.hpp"
#include "List.hpp"
#include "ComboBox.hpp"
#include "Entry.hpp"
#include "RadioButton.hpp"
#include "CheckBox.hpp"
#include "Button.hpp"
#include "CustomRendererWidget.hpp"
#include "../GsrInfo.hpp"
#include "../Config.hpp"

#include <functional>

namespace gsr {
    class ScrollablePage;

    class SettingsPage : public StaticPage {
    public:
        enum class Type {
            REPLAY,
            RECORD,
            STREAM
        };

        SettingsPage(Type type, const GsrInfo &gsr_info, const std::vector<AudioDevice> &audio_devices, std::optional<Config> &config);
        SettingsPage(const SettingsPage&) = delete;
        SettingsPage& operator=(const SettingsPage&) = delete;

        void save();
        void on_navigate_away_from_page() override;

        std::function<void()> on_back_button_handler;
    private:
        std::unique_ptr<Button> create_back_button();
        std::unique_ptr<CustomRendererWidget> create_settings_icon();
        std::unique_ptr<RadioButton> create_view_radio_button();
        std::unique_ptr<ComboBox> create_record_area_box(const GsrInfo &gsr_info);
        std::unique_ptr<List> create_record_area(const GsrInfo &gsr_info);
        std::unique_ptr<List> create_select_window();
        std::unique_ptr<Entry> create_area_width_entry();
        std::unique_ptr<Entry> create_area_height_entry();
        std::unique_ptr<List> create_area_size();
        std::unique_ptr<List> create_area_size_section();
        std::unique_ptr<CheckBox> create_restore_portal_session_checkbox();
        std::unique_ptr<List> create_restore_portal_session_section();
        std::unique_ptr<List> create_capture_target(const GsrInfo &gsr_info);
        std::unique_ptr<ComboBox> create_audio_track_selection_checkbox(const std::vector<AudioDevice> &audio_devices);
        std::unique_ptr<Button> create_remove_audio_track_button(List *audio_device_list_ptr);
        std::unique_ptr<List> create_audio_track(const std::vector<AudioDevice> &audio_devices);
        std::unique_ptr<Button> create_add_audio_track_button(const std::vector<AudioDevice> &audio_devices);
        std::unique_ptr<List> create_audio_track_section(const std::vector<AudioDevice> &audio_devices);
        std::unique_ptr<CheckBox> create_merge_audio_tracks_checkbox();
        std::unique_ptr<List> create_audio_device_section(const std::vector<AudioDevice> &audio_devices);
        std::unique_ptr<ComboBox> create_video_quality_box();
        std::unique_ptr<List> create_video_quality();
        std::unique_ptr<ComboBox> create_color_range_box();
        std::unique_ptr<List> create_color_range();
        std::unique_ptr<List> create_video_quality_section();
        std::unique_ptr<ComboBox> create_video_codec_box(const GsrInfo &gsr_info);
        std::unique_ptr<List> create_video_codec(const GsrInfo &gsr_info);
        std::unique_ptr<ComboBox> create_audio_codec_box();
        std::unique_ptr<List> create_audio_codec();
        std::unique_ptr<List> create_codec_section(const GsrInfo &gsr_info);
        std::unique_ptr<Entry> create_framerate_entry();
        std::unique_ptr<List> create_framerate();
        std::unique_ptr<ComboBox> create_framerate_mode_box();
        std::unique_ptr<List> create_framerate_mode();
        std::unique_ptr<List> create_framerate_section();
        std::unique_ptr<List> create_settings(const GsrInfo &gsr_info, const std::vector<AudioDevice> &audio_devices);
        void add_widgets(const gsr::GsrInfo &gsr_info, const std::vector<gsr::AudioDevice> &audio_devices);

        void add_page_specific_widgets();

        std::unique_ptr<List> create_save_directory(const char *label);
        std::unique_ptr<ComboBox> create_container_box();
        std::unique_ptr<List> create_container_section();
        std::unique_ptr<Entry> create_replay_time_entry();
        std::unique_ptr<List> create_replay_time();
        void add_replay_widgets();
        void add_record_widgets();

        std::unique_ptr<ComboBox> create_streaming_service_box();
        std::unique_ptr<List> create_streaming_service_section();
        std::unique_ptr<List> create_stream_key_section();
        std::unique_ptr<List> create_stream_url_section();
        std::unique_ptr<ComboBox> create_stream_container_box();
        std::unique_ptr<List> create_stream_container_section();
        void add_stream_widgets();

        void save_common(RecordOptions &record_options);
        void save_replay();
        void save_record();
        void save_stream();
    private:
        Type type;
        std::optional<Config> &config;

        ScrollablePage *content_page_ptr = nullptr;
        List *settings_list_ptr = nullptr;
        List *select_window_list_ptr = nullptr;
        List *area_size_list_ptr = nullptr;
        List *restore_portal_session_list_ptr = nullptr;
        List *color_range_list_ptr = nullptr;
        List *codec_list_ptr = nullptr;
        List *framerate_mode_list_ptr = nullptr;
        ComboBox *record_area_box_ptr = nullptr;
        Entry *area_width_entry_ptr = nullptr;
        Entry *area_height_entry_ptr = nullptr;
        Entry *framerate_entry_ptr = nullptr;
        List *audio_devices_list_ptr = nullptr;
        CheckBox *merge_audio_tracks_checkbox_ptr = nullptr;
        ComboBox *color_range_box_ptr = nullptr;
        ComboBox *video_quality_box_ptr = nullptr;
        ComboBox *video_codec_box_ptr = nullptr;
        ComboBox *audio_codec_box_ptr = nullptr;
        ComboBox *framerate_mode_box_ptr = nullptr;
        RadioButton *view_radio_button_ptr = nullptr;
        CheckBox *record_cursor_checkbox_ptr = nullptr;
        CheckBox *restore_portal_session_checkbox_ptr = nullptr;
        ComboBox *container_box_ptr = nullptr;
        ComboBox *streaming_service_box_ptr = nullptr;
        List *stream_key_list_ptr = nullptr;
        List *stream_url_list_ptr = nullptr;
        List *container_list_ptr = nullptr;
        CheckBox *show_replay_started_notification_checkbox_ptr = nullptr;
        CheckBox *show_replay_stopped_notification_checkbox_ptr = nullptr;
        CheckBox *show_replay_saved_notification_checkbox_ptr = nullptr;
        CheckBox *show_recording_started_notification_checkbox_ptr = nullptr;
        CheckBox *show_video_saved_notification_checkbox_ptr = nullptr;
        CheckBox *show_streaming_started_notification_checkbox_ptr = nullptr;
        CheckBox *show_streaming_stopped_notification_checkbox_ptr = nullptr;
        Entry *save_directory_entry_ptr = nullptr;
        Entry *twitch_stream_key_entry_ptr = nullptr;
        Entry *youtube_stream_key_entry_ptr = nullptr;
        Entry *stream_url_entry_ptr = nullptr;
        Entry *replay_time_entry_ptr = nullptr;

        mgl::Text settings_title_text;
    };
}