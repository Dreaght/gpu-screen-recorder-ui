#pragma once

#include "StaticPage.hpp"
#include "Button.hpp"
#include "RadioButton.hpp"
#include "../RecentVideos.hpp"
#include "TimelineWidget.hpp"

#include <mglpp/graphics/Text.hpp>

namespace gsr {
    struct GsrInfo;
    class PageStack;
    class Widget;
    class ComboBox;
    class Entry;
    class CheckBox;
    class Label;
    class List;

    class ExportPage : public StaticPage {
    public:
        struct SourceVideoInfo {
            VideoMetadata metadata;
            std::vector<TimelineWidget::TimelineChunk> chunks;
            std::string container = "mp4";
            std::string video_codec;
            std::string audio_codec;
            int64_t total_bitrate_kbps = 0;
            int64_t video_bitrate_kbps = 0;
            int64_t audio_bitrate_kbps = 0;
            double fps = 0.0;
            bool has_video_bitrate = false;
            bool has_audio_bitrate = false;
        };

        ExportPage(const GsrInfo *gsr_info, PageStack *page_stack, VideoMetadata video_metadata, std::vector<TimelineWidget::TimelineChunk> chunks);
        ExportPage(const ExportPage&) = delete;
        ExportPage& operator=(const ExportPage&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        mgl::vec2f get_inner_size() override;
    private:
        void draw_page_label(mgl::Window &window, mgl::vec2f body_pos);
        void draw_buttons(mgl::Window &window, mgl::vec2f body_pos, mgl::vec2f body_size);
        void draw_children(mgl::Window &window, mgl::vec2f position);

        std::unique_ptr<Widget> create_file_info_section();
        std::unique_ptr<Widget> create_save_directory(const char *label);
        std::unique_ptr<ComboBox> create_container_box();
        std::unique_ptr<Widget> create_container_section();

        std::unique_ptr<Widget> create_source_info_section();
        std::unique_ptr<Label> create_source_summary_label();
        std::unique_ptr<Label> create_estimated_file_size();
        std::unique_ptr<RadioButton> create_view_radio_button();

        std::unique_ptr<Widget> create_video_section();
        std::unique_ptr<Widget> create_reencode_video_checkbox();
        std::unique_ptr<Widget> create_reencode_audio_checkbox();
        std::unique_ptr<Widget> create_video_codec();
        std::unique_ptr<ComboBox> create_video_codec_box();
        std::unique_ptr<Widget> create_audio_codec();
        std::unique_ptr<ComboBox> create_audio_codec_box();
        std::unique_ptr<Widget> create_framerate();
        std::unique_ptr<Widget> create_video_bitrate();
        std::unique_ptr<Widget> create_audio_bitrate();
        std::unique_ptr<Entry> create_framerate_entry();
        std::unique_ptr<Entry> create_video_bitrate_entry();
        std::unique_ptr<Entry> create_audio_bitrate_entry();

        std::unique_ptr<Widget> create_settings();
        void add_widgets();

        void set_margins(float top, float bottom, float left, float right);
        void add_button(const std::string &text, const std::string &id, mgl::Color color);
        void load_source_video_info();
        void apply_source_defaults();
        bool start_export();
        bool is_video_reencode_active() const;
        bool is_audio_reencode_active() const;
        void view_changed(bool advanced_view);
        void update_reencode_options_visibility();
        void update_source_summary();
        void update_estimated_file_size();
        int64_t get_selected_duration_ms() const;
        float get_border_size() const;
        float get_horizontal_spacing() const;
        float get_settings_content_width() const;
        mgl::vec2f get_content_position();
    private:
        struct ButtonItem {
            std::unique_ptr<Button> button;
            std::string id;
        };

        float margin_top_scale = 0.0f;
        float margin_bottom_scale = 0.0f;
        float margin_left_scale = 0.0f;
        float margin_right_scale = 0.0f;

        const GsrInfo *gsr_info = nullptr;
        PageStack *page_stack = nullptr;
        mgl::Text top_text;
        mgl::Text bottom_text;
        std::vector<ButtonItem> buttons;
        SourceVideoInfo source_info;

        List *settings_list_ptr = nullptr;
        Button *save_directory_button_ptr = nullptr;
        RadioButton *view_radio_button_ptr = nullptr;
        ComboBox *container_box_ptr = nullptr;
        ComboBox *video_codec_box_ptr = nullptr;
        ComboBox *audio_codec_box_ptr = nullptr;
        Entry *framerate_entry_ptr = nullptr;
        Entry *video_bitrate_entry_ptr = nullptr;
        Entry *audio_bitrate_entry_ptr = nullptr;
        std::string source_framerate_text;
        bool source_framerate_known = false;
        bool framerate_modified = false;
        CheckBox *reencode_video_checkbox_ptr = nullptr;
        CheckBox *reencode_audio_checkbox_ptr = nullptr;
        Widget *advanced_compression_options_ptr = nullptr;
        Widget *video_reencode_options_ptr = nullptr;
        Widget *audio_reencode_options_ptr = nullptr;
        Label *source_summary_label_ptr = nullptr;
        Label *estimated_file_size_ptr = nullptr;

        std::function<void(const std::string &id)> on_click;
    };
}
