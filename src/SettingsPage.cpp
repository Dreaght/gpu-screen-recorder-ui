#include "../include/SettingsPage.hpp"
#include "../include/Theme.hpp"
#include "../include/gui/Button.hpp"
#include "../include/gui/RadioButton.hpp"
#include "../include/gui/List.hpp"
#include "../include/gui/ComboBox.hpp"
#include "../include/gui/Label.hpp"
#include "../include/gui/Entry.hpp"
#include "../include/gui/CheckBox.hpp"
#include "../include/gui/ScrollablePage.hpp"
#include "../include/GsrInfo.hpp"

namespace gsr {
    SettingsPage::SettingsPage(Type type, const GsrInfo &gsr_info, const std::vector<AudioDevice> &audio_devices, std::function<void()> back_button_callback) :
        page(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        type(type)
    {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = (window_size * mgl::vec2f(0.3333f, 0.7f)).floor();
        const mgl::vec2f content_page_position = mgl::vec2f(window_size * 0.5f - content_page_size * 0.5f).floor();
        const float settings_body_margin = 0.02f;

        auto content_page = std::make_unique<ScrollablePage>(content_page_size);
        content_page->set_position(content_page_position);
        content_page->set_margins(settings_body_margin, settings_body_margin, settings_body_margin, settings_body_margin);
        content_page_ptr = content_page.get();
        page.add_widget(std::move(content_page));

        add_widgets(gsr_info, audio_devices, back_button_callback);
        add_page_specific_widgets();
    }
    
    Page& SettingsPage::get_page() {
        return page;
    }

    void SettingsPage::add_widgets(const GsrInfo &gsr_info, const std::vector<AudioDevice> &audio_devices, std::function<void()> back_button_callback) {
        RadioButton *view_radio_button_ptr = nullptr;
        ComboBox *record_area_box_ptr = nullptr;
        List *select_window_list_ptr = nullptr;
        List *area_size_list_ptr = nullptr;
        Widget *color_range_list_ptr = nullptr;
        Widget *codec_list_ptr = nullptr;
        Widget *framerate_mode_list_ptr = nullptr;

        const mgl::vec2i window_size(get_theme().window_width, get_theme().window_height);

        auto back_button = std::make_unique<Button>(&get_theme().title_font, "Back", mgl::vec2f(window_size.x / 10, window_size.y / 15).floor(), get_theme().scrollable_page_bg_color);
        back_button->set_position(content_page_ptr->get_position().floor() + mgl::vec2f(content_page_ptr->get_size().x + window_size.x / 50, 0.0f).floor());
        back_button->set_border_scale(0.003f);
        back_button->on_click = back_button_callback;
        page.add_widget(std::move(back_button));

        auto settings_list = std::make_unique<List>(List::Orientation::VERTICAL);
        {
            auto view_radio_button = std::make_unique<RadioButton>(&get_theme().body_font);
            view_radio_button->add_item("Simple view", "simple");
            view_radio_button->add_item("Advanced view", "advanced");
            view_radio_button->set_horizontal_alignment(Widget::Alignment::CENTER);
            view_radio_button_ptr = view_radio_button.get();
            settings_list->add_widget(std::move(view_radio_button));

            auto capture_target_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
            {
                auto record_area_list = std::make_unique<List>(List::Orientation::VERTICAL);
                {
                    record_area_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Record area:", get_theme().text_color));
                    auto record_area_box = std::make_unique<ComboBox>(&get_theme().body_font);
                    // TODO: Show options not supported but disable them
                    if(gsr_info.supported_capture_options.window)
                        record_area_box->add_item("Window", "window");
                    if(gsr_info.supported_capture_options.focused)
                        record_area_box->add_item("Follow focused window", "focused");
                    if(gsr_info.supported_capture_options.screen)
                        record_area_box->add_item("All monitors", "screen");
                    for(const auto &monitor : gsr_info.supported_capture_options.monitors) {
                        char name[256];
                        snprintf(name, sizeof(name), "Monitor %s (%dx%d)", monitor.name.c_str(), monitor.size.x, monitor.size.y);
                        record_area_box->add_item(name, monitor.name);
                    }
                    if(gsr_info.supported_capture_options.portal)
                        record_area_box->add_item("Desktop portal", "portal");
                    record_area_box_ptr = record_area_box.get();
                    record_area_list->add_widget(std::move(record_area_box));
                }
                capture_target_list->add_widget(std::move(record_area_list));

                auto select_window_list = std::make_unique<List>(List::Orientation::VERTICAL);
                {
                    select_window_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Select window:", get_theme().text_color));
                    select_window_list->add_widget(std::make_unique<Button>(&get_theme().body_font, "Click here to select a window...", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120)));
                }
                select_window_list_ptr = select_window_list.get();
                capture_target_list->add_widget(std::move(select_window_list));

                auto area_size_list = std::make_unique<List>(List::Orientation::VERTICAL);
                {
                    area_size_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Area size:", get_theme().text_color));
                    auto area_size_params_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
                    {
                        auto area_width_entry = std::make_unique<Entry>(&get_theme().body_font, "1920", get_theme().body_font.get_character_size() * 5);
                        area_width_entry->validate_handler = create_entry_validator_integer_in_range(1, 1 << 15);
                        area_size_params_list->add_widget(std::move(area_width_entry));

                        area_size_params_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "x", get_theme().text_color));

                        auto area_height_entry = std::make_unique<Entry>(&get_theme().body_font, "1080", get_theme().body_font.get_character_size() * 5);
                        area_height_entry->validate_handler = create_entry_validator_integer_in_range(1, 1 << 15);
                        area_size_params_list->add_widget(std::move(area_height_entry));
                    }
                    area_size_list->add_widget(std::move(area_size_params_list));
                }
                area_size_list_ptr = area_size_list.get();
                capture_target_list->add_widget(std::move(area_size_list));
            }
            settings_list->add_widget(std::move(capture_target_list));

            auto audio_device_section_list = std::make_unique<List>(List::Orientation::VERTICAL);
            {
                audio_device_section_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Audio:", get_theme().text_color));
                auto audio_devices_list = std::make_unique<List>(List::Orientation::VERTICAL);
                List *audio_devices_list_ptr = audio_devices_list.get();

                auto add_audio_track_button = std::make_unique<Button>(&get_theme().body_font, "Add audio track", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
                auto add_audio_track = [&audio_devices, audio_devices_list_ptr]() {
                    auto audio_device_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
                    List *audio_device_list_ptr = audio_device_list.get();
                    {
                        audio_device_list->add_widget(std::make_unique<Label>(&get_theme().body_font, ">", get_theme().text_color));
                        auto audio_device_box = std::make_unique<ComboBox>(&get_theme().body_font);
                        for(const auto &audio_device : audio_devices) {
                            audio_device_box->add_item(audio_device.description, audio_device.name);
                        }
                        audio_device_list->add_widget(std::move(audio_device_box));
                        auto remove_audio_track_button = std::make_unique<Button>(&get_theme().body_font, "Remove", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
                        remove_audio_track_button->on_click = [=]() {
                            audio_devices_list_ptr->remove_widget(audio_device_list_ptr);
                        };
                        audio_device_list->add_widget(std::move(remove_audio_track_button));
                    }
                    audio_devices_list_ptr->add_widget(std::move(audio_device_list));
                };
                add_audio_track_button->on_click = add_audio_track;
                audio_device_section_list->add_widget(std::move(add_audio_track_button));

                for(int i = 0; i < 3; ++i) {
                    add_audio_track();
                }
                audio_device_section_list->add_widget(std::move(audio_devices_list));
            }
            settings_list->add_widget(std::move(audio_device_section_list));

            auto quality_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
            {
                auto video_quality_list = std::make_unique<List>(List::Orientation::VERTICAL);
                {
                    video_quality_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Video quality:", get_theme().text_color));
                    auto video_quality_box = std::make_unique<ComboBox>(&get_theme().body_font);
                    video_quality_box->add_item("Medium", "medium");
                    video_quality_box->add_item("High (Recommended for live streaming)", "high");
                    video_quality_box->add_item("Very high (Recommended)", "very_high");
                    video_quality_box->add_item("Ultra", "ultra");
                    video_quality_box->set_selected_item("very_high");
                    video_quality_list->add_widget(std::move(video_quality_box));
                }
                quality_list->add_widget(std::move(video_quality_list));

                auto color_range_list = std::make_unique<List>(List::Orientation::VERTICAL);
                {
                    color_range_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Color range:", get_theme().text_color));
                    auto color_range_box = std::make_unique<ComboBox>(&get_theme().body_font);
                    color_range_box->add_item("Limited", "limited");
                    color_range_box->add_item("Full", "full");
                    color_range_list->add_widget(std::move(color_range_box));
                }
                color_range_list_ptr = color_range_list.get();
                quality_list->add_widget(std::move(color_range_list));
            }
            settings_list->add_widget(std::move(quality_list));

            auto codec_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
            {
                auto video_codec_list = std::make_unique<List>(List::Orientation::VERTICAL);
                {
                    video_codec_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Video codec:", get_theme().text_color));
                    auto video_codec_box = std::make_unique<ComboBox>(&get_theme().body_font);
                    // TODO: Show options not supported but disable them
                    video_codec_box->add_item("Auto (Recommended)", "auto");
                    if(gsr_info.supported_video_codecs.h264)
                        video_codec_box->add_item("H264", "h264");
                    if(gsr_info.supported_video_codecs.hevc)
                        video_codec_box->add_item("HEVC", "hevc");
                    if(gsr_info.supported_video_codecs.av1)
                        video_codec_box->add_item("AV1", "av1");
                    if(gsr_info.supported_video_codecs.vp8)
                        video_codec_box->add_item("VP8", "vp8");
                    if(gsr_info.supported_video_codecs.vp9)
                        video_codec_box->add_item("VP9", "vp9");
                    // TODO: Add hdr options
                    if(gsr_info.supported_video_codecs.h264_software)
                        video_codec_box->add_item("H264 Software Encoder (Slow, not recommended)", "h264_software");
                    video_codec_list->add_widget(std::move(video_codec_box));
                }
                codec_list->add_widget(std::move(video_codec_list));

                auto audio_codec_list = std::make_unique<List>(List::Orientation::VERTICAL);
                {
                    audio_codec_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Audio codec:", get_theme().text_color));
                    auto audio_codec_box = std::make_unique<ComboBox>(&get_theme().body_font);
                    audio_codec_box->add_item("Opus (Recommended)", "opus");
                    audio_codec_box->add_item("AAC", "aac");
                    audio_codec_list->add_widget(std::move(audio_codec_box));
                }
                codec_list->add_widget(std::move(audio_codec_list));
            }
            codec_list_ptr = codec_list.get();
            settings_list->add_widget(std::move(codec_list));

            auto framerate_info_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
            {
                auto framerate_list = std::make_unique<List>(List::Orientation::VERTICAL);
                {
                    framerate_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Frame rate:", get_theme().text_color));
                    auto framerate_entry = std::make_unique<Entry>(&get_theme().body_font, "60", get_theme().body_font.get_character_size() * 3);
                    framerate_entry->validate_handler = create_entry_validator_integer_in_range(1, 500);
                    framerate_list->add_widget(std::move(framerate_entry));
                }
                framerate_info_list->add_widget(std::move(framerate_list));

                auto framerate_mode_list = std::make_unique<List>(List::Orientation::VERTICAL);
                {
                    framerate_mode_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Frame rate mode:", get_theme().text_color));
                    auto framerate_mode_box = std::make_unique<ComboBox>(&get_theme().body_font);
                    framerate_mode_box->add_item("Auto (Recommended)", "auto");
                    framerate_mode_box->add_item("Constant", "cfr");
                    framerate_mode_box->add_item("Variable", "vfr");
                    framerate_mode_list->add_widget(std::move(framerate_mode_box));
                }
                framerate_mode_list_ptr = framerate_mode_list.get();
                framerate_info_list->add_widget(std::move(framerate_mode_list));
            }
            settings_list->add_widget(std::move(framerate_info_list));
        }
        settings_list_ptr = settings_list.get();
        content_page_ptr->add_widget(std::move(settings_list));

        record_area_box_ptr->on_selection_changed = [=](const std::string &text, const std::string &id) {
            (void)text;
            const bool window_selected = id == "window";
            const bool focused_selected = id == "focused";
            select_window_list_ptr->set_visible(window_selected);
            area_size_list_ptr->set_visible(focused_selected);
        };

        view_radio_button_ptr->on_selection_changed = [=](const std::string &text, const std::string &id) {
            (void)text;
            const bool advanced_view = id == "advanced";
            color_range_list_ptr->set_visible(advanced_view);
            codec_list_ptr->set_visible(advanced_view);
            framerate_mode_list_ptr->set_visible(advanced_view);
        };
        view_radio_button_ptr->on_selection_changed("Simple", "simple");

        if(!gsr_info.supported_capture_options.monitors.empty())
            record_area_box_ptr->set_selected_item(gsr_info.supported_capture_options.monitors.front().name);
        else if(gsr_info.supported_capture_options.portal)
            record_area_box_ptr->set_selected_item("portal");
    }

    void SettingsPage::add_page_specific_widgets() {
        switch(type) {
            case Type::REPLAY:
                add_replay_widgets();
                break;
            case Type::RECORD:
                add_record_widgets();
                break;
            case Type::STREAM:
                add_stream_widgets();
                break;
        }
    }

    void SettingsPage::add_replay_widgets() {
        auto file_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        {
            auto save_directory_list = std::make_unique<List>(List::Orientation::VERTICAL);
            {
                save_directory_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Directory to save replays:", get_theme().text_color));
                // TODO:
                save_directory_list->add_widget(std::make_unique<Entry>(&get_theme().body_font, "/home/dec05eba/Videos", get_theme().body_font.get_character_size() * 20));
            }
            file_list->add_widget(std::move(save_directory_list));

            auto container_list = std::make_unique<List>(List::Orientation::VERTICAL);
            {
                container_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Container:", get_theme().text_color));
                auto container_box = std::make_unique<ComboBox>(&get_theme().body_font);
                container_box->add_item("mp4", "mp4");
                container_box->add_item("mkv", "matroska");
                container_box->add_item("flv", "flv");
                container_box->add_item("mov", "mov");
                container_list->add_widget(std::move(container_box));
            }
            file_list->add_widget(std::move(container_list));
        }
        settings_list_ptr->add_widget(std::move(file_list));

        settings_list_ptr->add_widget(std::make_unique<CheckBox>(&get_theme().body_font, "Record cursor"));
        settings_list_ptr->add_widget(std::make_unique<CheckBox>(&get_theme().body_font, "Show replay started notification"));
        settings_list_ptr->add_widget(std::make_unique<CheckBox>(&get_theme().body_font, "Show replay stopped notification"));
        settings_list_ptr->add_widget(std::make_unique<CheckBox>(&get_theme().body_font, "Show replay saved notification"));
    }

    void SettingsPage::add_record_widgets() {
        auto file_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        {
            auto save_directory_list = std::make_unique<List>(List::Orientation::VERTICAL);
            {
                save_directory_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Directory to save the video:", get_theme().text_color));
                // TODO:
                save_directory_list->add_widget(std::make_unique<Entry>(&get_theme().body_font, "/home/dec05eba/Videos", get_theme().body_font.get_character_size() * 20));
            }
            file_list->add_widget(std::move(save_directory_list));

            auto container_list = std::make_unique<List>(List::Orientation::VERTICAL);
            {
                container_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Container:", get_theme().text_color));
                auto container_box = std::make_unique<ComboBox>(&get_theme().body_font);
                container_box->add_item("mp4", "mp4");
                container_box->add_item("mkv", "matroska");
                container_box->add_item("flv", "flv");
                container_box->add_item("mov", "mov");
                container_list->add_widget(std::move(container_box));
            }
            file_list->add_widget(std::move(container_list));
        }
        settings_list_ptr->add_widget(std::move(file_list));

        settings_list_ptr->add_widget(std::make_unique<CheckBox>(&get_theme().body_font, "Record cursor"));
        settings_list_ptr->add_widget(std::make_unique<CheckBox>(&get_theme().body_font, "Show recording started notification"));
        settings_list_ptr->add_widget(std::make_unique<CheckBox>(&get_theme().body_font, "Show video saved notification"));
    }

    void SettingsPage::add_stream_widgets() {
        ComboBox *streaming_service_box_ptr = nullptr;
        List *stream_key_list_ptr = nullptr;
        List *stream_url_list_ptr = nullptr;
        List *container_list_ptr = nullptr;

        auto streaming_info_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        {
            auto streaming_service_list = std::make_unique<List>(List::Orientation::VERTICAL);
            {
                streaming_service_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Stream service:", get_theme().text_color));
                auto streaming_service_box = std::make_unique<ComboBox>(&get_theme().body_font);
                streaming_service_box->add_item("Twitch", "twitch");
                streaming_service_box->add_item("YouTube", "youtube");
                streaming_service_box->add_item("Custom", "custom");
                streaming_service_box_ptr = streaming_service_box.get();
                streaming_service_list->add_widget(std::move(streaming_service_box));
            }
            streaming_info_list->add_widget(std::move(streaming_service_list));

            auto stream_key_list = std::make_unique<List>(List::Orientation::VERTICAL);
            {
                stream_key_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Stream key:", get_theme().text_color));
                stream_key_list->add_widget(std::make_unique<Entry>(&get_theme().body_font, "", get_theme().body_font.get_character_size() * 20));
            }
            stream_key_list_ptr = stream_key_list.get();
            streaming_info_list->add_widget(std::move(stream_key_list));
            
            auto stream_url_list = std::make_unique<List>(List::Orientation::VERTICAL);
            {
                stream_url_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "URL:", get_theme().text_color));
                stream_url_list->add_widget(std::make_unique<Entry>(&get_theme().body_font, "", get_theme().body_font.get_character_size() * 20));
            }
            stream_url_list_ptr = stream_url_list.get();
            streaming_info_list->add_widget(std::move(stream_url_list));

            auto container_list = std::make_unique<List>(List::Orientation::VERTICAL);
            {
                container_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Container:", get_theme().text_color));
                auto container_box = std::make_unique<ComboBox>(&get_theme().body_font);
                container_box->add_item("mp4", "mp4");
                container_box->add_item("mkv", "matroska");
                container_box->add_item("flv", "flv");
                container_box->add_item("mov", "mov");
                container_box->add_item("ts", "mpegts");
                container_box->add_item("m3u8", "hls");
                container_list->add_widget(std::move(container_box));
            }
            container_list_ptr = container_list.get();
            streaming_info_list->add_widget(std::move(container_list));
        }
        settings_list_ptr->add_widget(std::move(streaming_info_list));

        settings_list_ptr->add_widget(std::make_unique<CheckBox>(&get_theme().body_font, "Record cursor"));
        settings_list_ptr->add_widget(std::make_unique<CheckBox>(&get_theme().body_font, "Show streaming started notification"));
        settings_list_ptr->add_widget(std::make_unique<CheckBox>(&get_theme().body_font, "Show streaming stopped notification"));

        streaming_service_box_ptr->on_selection_changed = [=](const std::string &text, const std::string &id) {
            (void)text;
            const bool custom_option = id == "custom";
            stream_key_list_ptr->set_visible(!custom_option);
            stream_url_list_ptr->set_visible(custom_option);
            container_list_ptr->set_visible(custom_option);
        };
        streaming_service_box_ptr->on_selection_changed("Twitch", "twitch");
    }
}