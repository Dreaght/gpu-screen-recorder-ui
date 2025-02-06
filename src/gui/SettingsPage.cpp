#include "../../include/gui/SettingsPage.hpp"
#include "../../include/gui/GsrPage.hpp"
#include "../../include/gui/Label.hpp"
#include "../../include/gui/PageStack.hpp"
#include "../../include/gui/FileChooser.hpp"
#include "../../include/gui/Subsection.hpp"
#include "../../include/Theme.hpp"
#include "../../include/GsrInfo.hpp"
#include "../../include/Utils.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/window/Window.hpp>

#include <string.h>

namespace gsr {
    enum class AudioTrackType {
        DEVICE,
        APPLICATION,
        APPLICATION_CUSTOM
    };

    SettingsPage::SettingsPage(Type type, const GsrInfo *gsr_info, Config &config, PageStack *page_stack) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        type(type),
        config(config),
        gsr_info(gsr_info),
        page_stack(page_stack)
    {
        audio_devices = get_audio_devices();
        application_audio = get_application_audio();
        capture_options = get_supported_capture_options(*gsr_info);

        auto content_page = std::make_unique<GsrPage>();
        content_page->add_button("Back", "back", get_color_theme().page_bg_color);
        content_page->on_click = [page_stack](const std::string &id) {
            if(id == "back")
                page_stack->pop();
        };
        content_page_ptr = content_page.get();
        add_widget(std::move(content_page));

        add_widgets();
        add_page_specific_widgets();
        load();
    }

    std::unique_ptr<RadioButton> SettingsPage::create_view_radio_button() {
        auto view_radio_button = std::make_unique<RadioButton>(&get_theme().body_font, RadioButton::Orientation::HORIZONTAL);
        view_radio_button->add_item("Simple view", "simple");
        view_radio_button->add_item("Advanced view", "advanced");
        view_radio_button->set_horizontal_alignment(Widget::Alignment::CENTER);
        view_radio_button_ptr = view_radio_button.get();
        return view_radio_button;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_record_area_box() {
        auto record_area_box = std::make_unique<ComboBox>(&get_theme().body_font);
        // TODO: Show options not supported but disable them
        // TODO: Enable this
        //if(capture_options.window)
        //    record_area_box->add_item("Window", "window");
        if(capture_options.focused)
            record_area_box->add_item("Follow focused window", "focused");
        for(const auto &monitor : capture_options.monitors) {
            char name[256];
            snprintf(name, sizeof(name), "Monitor %s (%dx%d)", monitor.name.c_str(), monitor.size.x, monitor.size.y);
            record_area_box->add_item(name, monitor.name);
        }
        if(capture_options.portal)
            record_area_box->add_item("Desktop portal", "portal");
        record_area_box_ptr = record_area_box.get();
        return record_area_box;
    }

    std::unique_ptr<Widget> SettingsPage::create_record_area() {
        auto record_area_list = std::make_unique<List>(List::Orientation::VERTICAL);
        record_area_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Capture target:", get_color_theme().text_color));
        record_area_list->add_widget(create_record_area_box());
        return record_area_list;
    }

    std::unique_ptr<List> SettingsPage::create_select_window() {
        auto select_window_list = std::make_unique<List>(List::Orientation::VERTICAL);
        select_window_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Select window:", get_color_theme().text_color));
        select_window_list->add_widget(std::make_unique<Button>(&get_theme().body_font, "Click here to select a window...", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120)));
        select_window_list_ptr = select_window_list.get();
        return select_window_list;
    }

    std::unique_ptr<Entry> SettingsPage::create_area_width_entry() {
        auto area_width_entry = std::make_unique<Entry>(&get_theme().body_font, "1920", get_theme().body_font.get_character_size() * 3);
        area_width_entry->validate_handler = create_entry_validator_integer_in_range(1, 1 << 15);
        area_width_entry_ptr = area_width_entry.get();
        return area_width_entry;
    }

    std::unique_ptr<Entry> SettingsPage::create_area_height_entry() {
        auto area_height_entry = std::make_unique<Entry>(&get_theme().body_font, "1080", get_theme().body_font.get_character_size() * 3);
        area_height_entry->validate_handler = create_entry_validator_integer_in_range(1, 1 << 15);
        area_height_entry_ptr = area_height_entry.get();
        return area_height_entry;
    }

    std::unique_ptr<List> SettingsPage::create_area_size() {
        auto area_size_params_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        area_size_params_list->add_widget(create_area_width_entry());
        area_size_params_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "x", get_color_theme().text_color));
        area_size_params_list->add_widget(create_area_height_entry());
        return area_size_params_list;
    }

    std::unique_ptr<List> SettingsPage::create_area_size_section() {
        auto area_size_list = std::make_unique<List>(List::Orientation::VERTICAL);
        area_size_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Area size:", get_color_theme().text_color));
        area_size_list->add_widget(create_area_size());
        area_size_list_ptr = area_size_list.get();
        return area_size_list;
    }

    std::unique_ptr<Entry> SettingsPage::create_video_width_entry() {
        auto video_width_entry = std::make_unique<Entry>(&get_theme().body_font, "1920", get_theme().body_font.get_character_size() * 3);
        video_width_entry->validate_handler = create_entry_validator_integer_in_range(1, 1 << 15);
        video_width_entry_ptr = video_width_entry.get();
        return video_width_entry;
    }

    std::unique_ptr<Entry> SettingsPage::create_video_height_entry() {
        auto video_height_entry = std::make_unique<Entry>(&get_theme().body_font, "1080", get_theme().body_font.get_character_size() * 3);
        video_height_entry->validate_handler = create_entry_validator_integer_in_range(1, 1 << 15);
        video_height_entry_ptr = video_height_entry.get();
        return video_height_entry;
    }

    std::unique_ptr<List> SettingsPage::create_video_resolution() {
        auto area_size_params_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        area_size_params_list->add_widget(create_video_width_entry());
        area_size_params_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "x", get_color_theme().text_color));
        area_size_params_list->add_widget(create_video_height_entry());
        return area_size_params_list;
    }

    std::unique_ptr<List> SettingsPage::create_video_resolution_section() {
        auto video_resolution_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_resolution_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Video resolution limit:", get_color_theme().text_color));
        video_resolution_list->add_widget(create_video_resolution());
        video_resolution_list_ptr = video_resolution_list.get();
        return video_resolution_list;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_restore_portal_session_checkbox() {
        auto restore_portal_session_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Restore portal session");
        restore_portal_session_checkbox->set_checked(true);
        restore_portal_session_checkbox_ptr = restore_portal_session_checkbox.get();
        return restore_portal_session_checkbox;
    }

    std::unique_ptr<List> SettingsPage::create_restore_portal_session_section() {
        auto restore_portal_session_list = std::make_unique<List>(List::Orientation::VERTICAL);
        restore_portal_session_list->add_widget(std::make_unique<Label>(&get_theme().body_font, " ", get_color_theme().text_color));
        restore_portal_session_list->add_widget(create_restore_portal_session_checkbox());
        restore_portal_session_list_ptr = restore_portal_session_list.get();
        return restore_portal_session_list;
    }

    std::unique_ptr<Widget> SettingsPage::create_change_video_resolution_section() {
        auto checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Change video resolution");
        change_video_resolution_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Widget> SettingsPage::create_capture_target() {
        auto ll = std::make_unique<List>(List::Orientation::VERTICAL);

        auto capture_target_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        capture_target_list->add_widget(create_record_area());
        capture_target_list->add_widget(create_select_window());
        capture_target_list->add_widget(create_area_size_section());
        capture_target_list->add_widget(create_video_resolution_section());
        capture_target_list->add_widget(create_restore_portal_session_section());

        ll->add_widget(std::move(capture_target_list));
        ll->add_widget(create_change_video_resolution_section());
        return std::make_unique<Subsection>("Record area", std::move(ll), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<ComboBox> SettingsPage::create_audio_device_selection_combobox() {
        auto audio_device_box = std::make_unique<ComboBox>(&get_theme().body_font);
        for(const auto &audio_device : audio_devices) {
            audio_device_box->add_item(audio_device.description, audio_device.name);
        }
        return audio_device_box;
    }

    std::unique_ptr<Button> SettingsPage::create_remove_audio_device_button(List *audio_device_list_ptr) {
        auto remove_audio_track_button = std::make_unique<Button>(&get_theme().body_font, "Remove", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        remove_audio_track_button->on_click = [this, audio_device_list_ptr]() {
            audio_track_list_ptr->remove_widget(audio_device_list_ptr);
        };
        return remove_audio_track_button;
    }

    std::unique_ptr<List> SettingsPage::create_audio_device() {
        auto audio_device_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        audio_device_list->userdata = (void*)(uintptr_t)AudioTrackType::DEVICE;
        audio_device_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Device:", get_color_theme().text_color));
        audio_device_list->add_widget(create_audio_device_selection_combobox());
        audio_device_list->add_widget(create_remove_audio_device_button(audio_device_list.get()));
        return audio_device_list;
    }

    std::unique_ptr<Button> SettingsPage::create_add_audio_device_button() {
        auto add_audio_track_button = std::make_unique<Button>(&get_theme().body_font, "Add audio device", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        add_audio_track_button->on_click = [this]() {
            audio_devices = get_audio_devices();
            audio_track_list_ptr->add_widget(create_audio_device());
        };
        return add_audio_track_button;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_application_audio_selection_combobox() {
        auto audio_device_box = std::make_unique<ComboBox>(&get_theme().body_font);
        for(const auto &app_audio : application_audio) {
            audio_device_box->add_item(app_audio, app_audio);
        }
        return audio_device_box;
    }

    std::unique_ptr<List> SettingsPage::create_application_audio() {
        auto application_audio_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        application_audio_list->userdata = (void*)(uintptr_t)AudioTrackType::APPLICATION;
        application_audio_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "App:     ", get_color_theme().text_color));
        application_audio_list->add_widget(create_application_audio_selection_combobox());
        application_audio_list->add_widget(create_remove_audio_device_button(application_audio_list.get()));
        return application_audio_list;
    }

    std::unique_ptr<List> SettingsPage::create_custom_application_audio() {
        auto application_audio_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        application_audio_list->userdata = (void*)(uintptr_t)AudioTrackType::APPLICATION_CUSTOM;
        application_audio_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "App:     ", get_color_theme().text_color));
        application_audio_list->add_widget(std::make_unique<Entry>(&get_theme().body_font, "", (int)(get_theme().body_font.get_character_size() * 10.0f)));
        application_audio_list->add_widget(create_remove_audio_device_button(application_audio_list.get()));
        return application_audio_list;
    }

    std::unique_ptr<Button> SettingsPage::create_add_application_audio_button() {
        auto add_audio_track_button = std::make_unique<Button>(&get_theme().body_font, "Add application audio", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        add_application_audio_button_ptr = add_audio_track_button.get();
        add_audio_track_button->on_click = [this]() {
            application_audio = get_application_audio();
            audio_track_list_ptr->add_widget(create_application_audio());
        };
        return add_audio_track_button;
    }

    std::unique_ptr<Button> SettingsPage::create_add_custom_application_audio_button() {
        auto add_audio_track_button = std::make_unique<Button>(&get_theme().body_font, "Add custom application audio", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        add_custom_application_audio_button_ptr = add_audio_track_button.get();
        add_audio_track_button->on_click = [this]() {
            audio_track_list_ptr->add_widget(create_custom_application_audio());
        };
        return add_audio_track_button;
    }

    std::unique_ptr<List> SettingsPage::create_add_audio_buttons() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        list->add_widget(create_add_audio_device_button());
        list->add_widget(create_add_application_audio_button());
        list->add_widget(create_add_custom_application_audio_button());
        return list;
    }

    std::unique_ptr<List> SettingsPage::create_audio_track_track_section() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_track_list_ptr = list.get();
        audio_track_list_ptr->add_widget(create_audio_device()); // Add default_output by default
        return list;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_merge_audio_tracks_checkbox() {
        auto merge_audio_tracks_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Merge audio tracks");
        merge_audio_tracks_checkbox->set_checked(true);
        merge_audio_tracks_checkbox_ptr = merge_audio_tracks_checkbox.get();
        return merge_audio_tracks_checkbox;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_application_audio_invert_checkbox() {
        auto application_audio_invert_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Record audio from all applications except the selected ones");
        application_audio_invert_checkbox->set_checked(false);
        application_audio_invert_checkbox_ptr = application_audio_invert_checkbox.get();
        return application_audio_invert_checkbox;
    }

    std::unique_ptr<Widget> SettingsPage::create_audio_track_section() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(create_add_audio_buttons());
        list->add_widget(create_audio_track_track_section());
        return list;
    }

    std::unique_ptr<Widget> SettingsPage::create_audio_section() {
        auto audio_device_section_list = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_device_section_list->add_widget(create_audio_track_section());
        if(type != Type::STREAM)
            audio_device_section_list->add_widget(create_merge_audio_tracks_checkbox());
        audio_device_section_list->add_widget(create_application_audio_invert_checkbox());
        audio_device_section_list->add_widget(create_audio_codec());
        return std::make_unique<Subsection>("Audio", std::move(audio_device_section_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<List> SettingsPage::create_video_quality_box() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Video quality:", get_color_theme().text_color));

        auto video_quality_box = std::make_unique<ComboBox>(&get_theme().body_font);
        if(type == Type::REPLAY || type == Type::STREAM)
            video_quality_box->add_item("Constant bitrate (Recommended)", "custom");
        else
            video_quality_box->add_item("Constant bitrate", "custom");
        video_quality_box->add_item("Medium", "medium");
        video_quality_box->add_item("High", "high");
        if(type == Type::REPLAY || type == Type::STREAM)
            video_quality_box->add_item("Very high", "very_high");
        else
            video_quality_box->add_item("Very high (Recommended)", "very_high");
        video_quality_box->add_item("Ultra", "ultra");

        if(type == Type::REPLAY || type == Type::STREAM)
            video_quality_box->set_selected_item("custom");
        else
            video_quality_box->set_selected_item("very_high");

        video_quality_box_ptr = video_quality_box.get();
        list->add_widget(std::move(video_quality_box));

        return list;
    }

    std::unique_ptr<List> SettingsPage::create_video_bitrate_entry() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        auto video_bitrate_entry = std::make_unique<Entry>(&get_theme().body_font, "15000", (int)(get_theme().body_font.get_character_size() * 4.0f));
        video_bitrate_entry->validate_handler = create_entry_validator_integer_in_range(1, 500000);
        video_bitrate_entry_ptr = video_bitrate_entry.get();
        list->add_widget(std::move(video_bitrate_entry));

        if(type == Type::STREAM) {
            auto size_mb_label = std::make_unique<Label>(&get_theme().body_font, "1.92MB", get_color_theme().text_color);
            Label *size_mb_label_ptr = size_mb_label.get();
            list->add_widget(std::move(size_mb_label));

            video_bitrate_entry_ptr->on_changed = [size_mb_label_ptr](const std::string &text) {
                const double video_bitrate_mb_per_seconds = (double)atoi(text.c_str()) / 1000LL / 8LL * 1.024;
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%.2fMB", video_bitrate_mb_per_seconds);
                size_mb_label_ptr->set_text(buffer);
            };
        }

        return list;
    }

    std::unique_ptr<List> SettingsPage::create_video_bitrate() {
        auto video_bitrate_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_bitrate_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Video bitrate (kbps):", get_color_theme().text_color));
        video_bitrate_list->add_widget(create_video_bitrate_entry());
        video_bitrate_list_ptr = video_bitrate_list.get();
        return video_bitrate_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_color_range_box() {
        auto color_range_box = std::make_unique<ComboBox>(&get_theme().body_font);
        color_range_box->add_item("Limited", "limited");
        color_range_box->add_item("Full", "full");
        color_range_box_ptr = color_range_box.get();
        return color_range_box;
    }

    std::unique_ptr<List> SettingsPage::create_color_range() {
        auto color_range_list = std::make_unique<List>(List::Orientation::VERTICAL);
        color_range_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Color range:", get_color_theme().text_color));
        color_range_list->add_widget(create_color_range_box());
        color_range_list_ptr = color_range_list.get();
        return color_range_list;
    }

    std::unique_ptr<List> SettingsPage::create_video_quality_section() {
        auto quality_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        quality_list->add_widget(create_video_quality_box());
        quality_list->add_widget(create_video_bitrate());
        quality_list->add_widget(create_color_range());
        return quality_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_video_codec_box() {
        auto video_codec_box = std::make_unique<ComboBox>(&get_theme().body_font);
        // TODO: Show options not supported but disable them.
        // TODO: Show error if no encoders are supported.
        // TODO: Show warning (once) if only software encoder is available.
        video_codec_box->add_item("Auto (Recommended)", "auto");
        if(gsr_info->supported_video_codecs.h264)
            video_codec_box->add_item("H264", "h264");
        if(gsr_info->supported_video_codecs.hevc)
            video_codec_box->add_item("HEVC", "hevc");
        if(gsr_info->supported_video_codecs.hevc_10bit)
            video_codec_box->add_item("HEVC (10 bit, reduces banding)", "hevc_10bit");
        if(gsr_info->supported_video_codecs.hevc_hdr)
            video_codec_box->add_item("HEVC (HDR)", "hevc_hdr");
        if(gsr_info->supported_video_codecs.av1)
            video_codec_box->add_item("AV1", "av1");
        if(gsr_info->supported_video_codecs.av1_10bit)
            video_codec_box->add_item("AV1 (10 bit, reduces banding)", "av1_10bit");
        if(gsr_info->supported_video_codecs.av1_hdr)
            video_codec_box->add_item("AV1 (HDR)", "av1_hdr");
        if(gsr_info->supported_video_codecs.vp8)
            video_codec_box->add_item("VP8", "vp8");
        if(gsr_info->supported_video_codecs.vp9)
            video_codec_box->add_item("VP9", "vp9");
        if(gsr_info->supported_video_codecs.h264_software)
            video_codec_box->add_item("H264 Software Encoder (Slow, not recommended)", "h264_software");
        video_codec_box_ptr = video_codec_box.get();
        return video_codec_box;
    }

    std::unique_ptr<List> SettingsPage::create_video_codec() {
        auto video_codec_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_codec_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Video codec:", get_color_theme().text_color));
        video_codec_list->add_widget(create_video_codec_box());
        video_codec_ptr = video_codec_list.get();
        return video_codec_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_audio_codec_box() {
        auto audio_codec_box = std::make_unique<ComboBox>(&get_theme().body_font);
        audio_codec_box->add_item("Opus (Recommended)", "opus");
        audio_codec_box->add_item("AAC", "aac");
        audio_codec_box_ptr = audio_codec_box.get();
        return audio_codec_box;
    }

    std::unique_ptr<List> SettingsPage::create_audio_codec() {
        auto audio_codec_list = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_codec_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Audio codec:", get_color_theme().text_color));
        audio_codec_list->add_widget(create_audio_codec_box());
        audio_codec_ptr = audio_codec_list.get();
        return audio_codec_list;
    }

    std::unique_ptr<Entry> SettingsPage::create_framerate_entry() {
        auto framerate_entry = std::make_unique<Entry>(&get_theme().body_font, "60", (int)(get_theme().body_font.get_character_size() * 2.5f));
        framerate_entry->validate_handler = create_entry_validator_integer_in_range(1, 500);
        framerate_entry_ptr = framerate_entry.get();
        return framerate_entry;
    }

    std::unique_ptr<List> SettingsPage::create_framerate() {
        auto framerate_list = std::make_unique<List>(List::Orientation::VERTICAL);
        framerate_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Frame rate:", get_color_theme().text_color));
        framerate_list->add_widget(create_framerate_entry());
        return framerate_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_framerate_mode_box() {
        auto framerate_mode_box = std::make_unique<ComboBox>(&get_theme().body_font);
        framerate_mode_box->add_item("Auto (Recommended)", "auto");
        framerate_mode_box->add_item("Constant", "cfr");
        framerate_mode_box->add_item("Variable", "vfr");
        framerate_mode_box_ptr = framerate_mode_box.get();
        return framerate_mode_box;
    }

    std::unique_ptr<List> SettingsPage::create_framerate_mode() {
        auto framerate_mode_list = std::make_unique<List>(List::Orientation::VERTICAL);
        framerate_mode_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Frame rate mode:", get_color_theme().text_color));
        framerate_mode_list->add_widget(create_framerate_mode_box());
        framerate_mode_list_ptr = framerate_mode_list.get();
        return framerate_mode_list;
    }

    std::unique_ptr<List> SettingsPage::create_framerate_section() {
        auto framerate_info_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        framerate_info_list->add_widget(create_framerate());
        framerate_info_list->add_widget(create_framerate_mode());
        return framerate_info_list;
    }
    
    std::unique_ptr<Widget> SettingsPage::create_record_cursor_section() {
        auto record_cursor_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Record cursor");
        record_cursor_checkbox->set_checked(true);
        record_cursor_checkbox_ptr = record_cursor_checkbox.get();
        return record_cursor_checkbox;
    }

    std::unique_ptr<Widget> SettingsPage::create_video_section() {
        auto video_section_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_section_list->add_widget(create_video_quality_section());
        video_section_list->add_widget(create_video_codec());
        video_section_list->add_widget(create_framerate_section());
        video_section_list->add_widget(create_record_cursor_section());
        return std::make_unique<Subsection>("Video", std::move(video_section_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Widget> SettingsPage::create_settings() {
        auto page_list = std::make_unique<List>(List::Orientation::VERTICAL);
        page_list->set_spacing(0.018f);
        page_list->add_widget(create_view_radio_button());
        auto scrollable_page = std::make_unique<ScrollablePage>(content_page_ptr->get_inner_size() - mgl::vec2f(0.0f, page_list->get_size().y + 0.018f * get_theme().window_height));
        settings_scrollable_page_ptr = scrollable_page.get();
        page_list->add_widget(std::move(scrollable_page));

        auto settings_list = std::make_unique<List>(List::Orientation::VERTICAL);
        settings_list->set_spacing(0.018f);
        settings_list->add_widget(create_capture_target());
        settings_list->add_widget(create_audio_section());
        settings_list->add_widget(create_video_section());
        settings_list_ptr = settings_list.get();
        settings_scrollable_page_ptr->add_widget(std::move(settings_list));
        return page_list;
    }

    void SettingsPage::add_widgets() {
        content_page_ptr->add_widget(create_settings());

        record_area_box_ptr->on_selection_changed = [this](const std::string &text, const std::string &id) {
            (void)text;
            const bool window_selected = id == "window";
            const bool focused_selected = id == "focused";
            const bool portal_selected = id == "portal";
            select_window_list_ptr->set_visible(window_selected);
            area_size_list_ptr->set_visible(focused_selected);
            video_resolution_list_ptr->set_visible(!focused_selected && change_video_resolution_checkbox_ptr->is_checked());
            change_video_resolution_checkbox_ptr->set_visible(!focused_selected);
            restore_portal_session_list_ptr->set_visible(portal_selected);
            return true;
        };

        change_video_resolution_checkbox_ptr->on_changed = [this](bool checked) {
            const bool focused_selected = record_area_box_ptr->get_selected_id() == "focused";
            video_resolution_list_ptr->set_visible(!focused_selected && checked);
        };

        video_quality_box_ptr->on_selection_changed = [this](const std::string &text, const std::string &id) {
            (void)text;
            const bool custom_selected = id == "custom";
            video_bitrate_list_ptr->set_visible(custom_selected);

            if(estimated_file_size_ptr)
                estimated_file_size_ptr->set_visible(custom_selected);

            return true;
        };
        video_quality_box_ptr->on_selection_changed("", video_quality_box_ptr->get_selected_id());

        if(!capture_options.monitors.empty())
            record_area_box_ptr->set_selected_item(capture_options.monitors.front().name);
        else if(capture_options.portal)
            record_area_box_ptr->set_selected_item("portal");
        else if(capture_options.window)
            record_area_box_ptr->set_selected_item("window");
        else
            record_area_box_ptr->on_selection_changed("", "");

        if(!gsr_info->system_info.supports_app_audio) {
            add_application_audio_button_ptr->set_visible(false);
            add_custom_application_audio_button_ptr->set_visible(false);
            application_audio_invert_checkbox_ptr->set_visible(false);
        }
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

    std::unique_ptr<List> SettingsPage::create_save_directory(const char *label) {
        auto save_directory_list = std::make_unique<List>(List::Orientation::VERTICAL);
        save_directory_list->add_widget(std::make_unique<Label>(&get_theme().body_font, label, get_color_theme().text_color));
        auto save_directory_button = std::make_unique<Button>(&get_theme().body_font, get_videos_dir().c_str(), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        save_directory_button_ptr = save_directory_button.get();
        save_directory_button->on_click = [this]() {
            auto select_directory_page = std::make_unique<GsrPage>();
            select_directory_page->add_button("Save", "save", get_color_theme().tint_color);
            select_directory_page->add_button("Cancel", "cancel", get_color_theme().page_bg_color);
            
            auto file_chooser = std::make_unique<FileChooser>(save_directory_button_ptr->get_text().c_str(), select_directory_page->get_inner_size());
            FileChooser *file_chooser_ptr = file_chooser.get();
            select_directory_page->add_widget(std::move(file_chooser));

            select_directory_page->on_click = [this, file_chooser_ptr](const std::string &id) {
                if(id == "save") {
                    save_directory_button_ptr->set_text(file_chooser_ptr->get_current_directory());
                    page_stack->pop();
                } else if(id == "cancel") {
                    page_stack->pop();
                }
            };

            page_stack->push(std::move(select_directory_page));
        };
        save_directory_list->add_widget(std::move(save_directory_button));
        return save_directory_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_container_box() {
        auto container_box = std::make_unique<ComboBox>(&get_theme().body_font);
        container_box->add_item("mp4", "mp4");
        container_box->add_item("mkv", "matroska");
        container_box->add_item("flv", "flv");
        container_box->add_item("mov", "mov");
        container_box_ptr = container_box.get();
        return container_box;
    }

    std::unique_ptr<List> SettingsPage::create_container_section() {
        auto container_list = std::make_unique<List>(List::Orientation::VERTICAL);
        container_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Container:", get_color_theme().text_color));
        container_list->add_widget(create_container_box());
        return container_list;
    }

    std::unique_ptr<List> SettingsPage::create_replay_time_entry() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        auto replay_time_entry = std::make_unique<Entry>(&get_theme().body_font, "60", get_theme().body_font.get_character_size() * 3);
        replay_time_entry->validate_handler = create_entry_validator_integer_in_range(1, 10800);
        replay_time_entry_ptr = replay_time_entry.get();
        list->add_widget(std::move(replay_time_entry));

        auto replay_time_label = std::make_unique<Label>(&get_theme().body_font, "00h:00m:00s", get_color_theme().text_color);
        replay_time_label_ptr = replay_time_label.get();
        list->add_widget(std::move(replay_time_label));

        return list;
    }

    std::unique_ptr<List> SettingsPage::create_replay_time() {
        auto replay_time_list = std::make_unique<List>(List::Orientation::VERTICAL);
        replay_time_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Replay duration in seconds:", get_color_theme().text_color));
        replay_time_list->add_widget(create_replay_time_entry());
        return replay_time_list;
    }

    std::unique_ptr<RadioButton> SettingsPage::create_start_replay_automatically() {
        char fullscreen_text[256];
        snprintf(fullscreen_text, sizeof(fullscreen_text), "Turn on replay when starting a fullscreen application%s", gsr_info->system_info.display_server == DisplayServer::X11 ? "" : " (X11 applications only)");

        auto radiobutton = std::make_unique<RadioButton>(&get_theme().body_font, RadioButton::Orientation::VERTICAL);
        radiobutton->add_item("Don't turn on replay automatically", "dont_turn_on_automatically");
        radiobutton->add_item("Turn on replay when this program starts", "turn_on_at_system_startup");
        radiobutton->add_item(fullscreen_text, "turn_on_at_fullscreen");
        radiobutton->add_item("Turn on replay when power supply is connected", "turn_on_at_power_supply_connected");
        turn_on_replay_automatically_mode_ptr = radiobutton.get();
        return radiobutton;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_save_replay_in_game_folder() {
        char text[256];
        snprintf(text, sizeof(text), "Save video in a folder with the name of the game%s", gsr_info->system_info.display_server == DisplayServer::X11 ? "" : " (X11 applications only)");
        auto checkbox = std::make_unique<CheckBox>(&get_theme().body_font, text);
        save_replay_in_game_folder_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_restart_replay_on_save() {
        auto checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Restart replay on save");
        restart_replay_on_save = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Label> SettingsPage::create_estimated_replay_file_size() {
        auto label = std::make_unique<Label>(&get_theme().body_font, "Estimated video max file size in RAM: 57.60MB", get_color_theme().text_color);
        estimated_file_size_ptr = label.get();
        return label;
    }

    void SettingsPage::update_estimated_replay_file_size() {
        const int64_t replay_time_seconds = atoi(replay_time_entry_ptr->get_text().c_str());
        const int64_t video_bitrate_bps = atoi(video_bitrate_entry_ptr->get_text().c_str()) * 1000LL / 8LL;
        const double video_filesize_mb = ((double)replay_time_seconds * (double)video_bitrate_bps) / 1000.0 / 1000.0 * 1.024;

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Estimated video max file size in RAM: %.2fMB.\nChange video bitrate or replay duration to change file size.", video_filesize_mb);
        estimated_file_size_ptr->set_text(buffer);
    }

    void SettingsPage::update_replay_time_text() {
        int seconds = atoi(replay_time_entry_ptr->get_text().c_str());

        const int hours = seconds / 60 / 60;
        seconds -= (hours * 60 * 60);

        const int minutes = seconds / 60;
        seconds -= (minutes * 60);

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "%02dh:%02dm:%02ds", hours, minutes, seconds);
        replay_time_label_ptr->set_text(buffer);
    }

    void SettingsPage::add_replay_widgets() {
        auto file_info_list = std::make_unique<List>(List::Orientation::VERTICAL);
        auto file_info_data_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        file_info_data_list->add_widget(create_save_directory("Directory to save replays:"));
        file_info_data_list->add_widget(create_container_section());
        file_info_data_list->add_widget(create_replay_time());
        file_info_list->add_widget(std::move(file_info_data_list));
        file_info_list->add_widget(create_estimated_replay_file_size());
        settings_list_ptr->add_widget(std::make_unique<Subsection>("File info", std::move(file_info_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        auto general_list = std::make_unique<List>(List::Orientation::VERTICAL);
        general_list->add_widget(create_start_replay_automatically());
        general_list->add_widget(create_save_replay_in_game_folder());
        if(gsr_info->system_info.gsr_version >= GsrVersion{5, 0, 3})
            general_list->add_widget(create_restart_replay_on_save());
        settings_list_ptr->add_widget(std::make_unique<Subsection>("General", std::move(general_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        auto checkboxes_list = std::make_unique<List>(List::Orientation::VERTICAL);

        auto show_replay_started_notification_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Show replay started notification");
        show_replay_started_notification_checkbox->set_checked(true);
        show_replay_started_notification_checkbox_ptr = show_replay_started_notification_checkbox.get();
        checkboxes_list->add_widget(std::move(show_replay_started_notification_checkbox));

        auto show_replay_stopped_notification_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Show replay stopped notification");
        show_replay_stopped_notification_checkbox->set_checked(true);
        show_replay_stopped_notification_checkbox_ptr = show_replay_stopped_notification_checkbox.get();
        checkboxes_list->add_widget(std::move(show_replay_stopped_notification_checkbox));

        auto show_replay_saved_notification_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Show replay saved notification");
        show_replay_saved_notification_checkbox->set_checked(true);
        show_replay_saved_notification_checkbox_ptr = show_replay_saved_notification_checkbox.get();
        checkboxes_list->add_widget(std::move(show_replay_saved_notification_checkbox));

        auto notifications_subsection = std::make_unique<Subsection>("Notifications", std::move(checkboxes_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));        
        Subsection *notifications_subsection_ptr = notifications_subsection.get();
        settings_list_ptr->add_widget(std::move(notifications_subsection));

        view_radio_button_ptr->on_selection_changed = [this, notifications_subsection_ptr](const std::string &text, const std::string &id) {
            (void)text;
            const bool advanced_view = id == "advanced";
            color_range_list_ptr->set_visible(advanced_view);
            audio_codec_ptr->set_visible(advanced_view);
            video_codec_ptr->set_visible(advanced_view);
            framerate_mode_list_ptr->set_visible(advanced_view);
            notifications_subsection_ptr->set_visible(advanced_view);
            settings_scrollable_page_ptr->reset_scroll();
            return true;
        };
        view_radio_button_ptr->on_selection_changed("Simple", "simple");

        replay_time_entry_ptr->on_changed = [this](const std::string&) {
            update_estimated_replay_file_size();
            update_replay_time_text();
        };

        video_bitrate_entry_ptr->on_changed = [this](const std::string&) {
            update_estimated_replay_file_size();
        };
    }

    std::unique_ptr<CheckBox> SettingsPage::create_save_recording_in_game_folder() {
        char text[256];
        snprintf(text, sizeof(text), "Save video in a folder with the name of the game%s", gsr_info->system_info.display_server == DisplayServer::X11 ? "" : " (X11 applications only)");
        auto checkbox = std::make_unique<CheckBox>(&get_theme().body_font, text);
        save_recording_in_game_folder_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Label> SettingsPage::create_estimated_record_file_size() {
        auto label = std::make_unique<Label>(&get_theme().body_font, "Estimated video file size per minute (excluding audio): 345.60MB", get_color_theme().text_color);
        estimated_file_size_ptr = label.get();
        return label;
    }

    void SettingsPage::update_estimated_record_file_size() {
        const int64_t video_bitrate_bps = atoi(video_bitrate_entry_ptr->get_text().c_str()) * 1000LL / 8LL;
        const double video_filesize_mb_per_minute = (60.0 * (double)video_bitrate_bps) / 1000.0 / 1000.0 * 1.024;

        char buffer[512];
        snprintf(buffer, sizeof(buffer), "Estimated video file size per minute (excluding audio): %.2fMB", video_filesize_mb_per_minute);
        estimated_file_size_ptr->set_text(buffer);
    }

    void SettingsPage::add_record_widgets() {
        auto file_info_list = std::make_unique<List>(List::Orientation::VERTICAL);
        auto file_info_data_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        file_info_data_list->add_widget(create_save_directory("Directory to save the video:"));
        file_info_data_list->add_widget(create_container_section());
        file_info_list->add_widget(std::move(file_info_data_list));
        file_info_list->add_widget(create_estimated_record_file_size());
        settings_list_ptr->add_widget(std::make_unique<Subsection>("File info", std::move(file_info_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        auto general_list = std::make_unique<List>(List::Orientation::VERTICAL);
        general_list->add_widget(create_save_recording_in_game_folder());
        settings_list_ptr->add_widget(std::make_unique<Subsection>("General", std::move(general_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        auto checkboxes_list = std::make_unique<List>(List::Orientation::VERTICAL);

        auto show_recording_started_notification_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Show recording started notification");
        show_recording_started_notification_checkbox->set_checked(true);
        show_recording_started_notification_checkbox_ptr = show_recording_started_notification_checkbox.get();
        checkboxes_list->add_widget(std::move(show_recording_started_notification_checkbox));

        auto show_video_saved_notification_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Show video saved notification");
        show_video_saved_notification_checkbox->set_checked(true);
        show_video_saved_notification_checkbox_ptr = show_video_saved_notification_checkbox.get();
        checkboxes_list->add_widget(std::move(show_video_saved_notification_checkbox));

        auto notifications_subsection = std::make_unique<Subsection>("Notifications", std::move(checkboxes_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));        
        Subsection *notifications_subsection_ptr = notifications_subsection.get();
        settings_list_ptr->add_widget(std::move(notifications_subsection));

        view_radio_button_ptr->on_selection_changed = [this, notifications_subsection_ptr](const std::string &text, const std::string &id) {
            (void)text;
            const bool advanced_view = id == "advanced";
            color_range_list_ptr->set_visible(advanced_view);
            audio_codec_ptr->set_visible(advanced_view);
            video_codec_ptr->set_visible(advanced_view);
            framerate_mode_list_ptr->set_visible(advanced_view);
            notifications_subsection_ptr->set_visible(advanced_view);
            settings_scrollable_page_ptr->reset_scroll();
            return true;
        };
        view_radio_button_ptr->on_selection_changed("Simple", "simple");

        video_bitrate_entry_ptr->on_changed = [this](const std::string&) {
            update_estimated_record_file_size();
        };
    }

    std::unique_ptr<ComboBox> SettingsPage::create_streaming_service_box() {
        auto streaming_service_box = std::make_unique<ComboBox>(&get_theme().body_font);
        streaming_service_box->add_item("Twitch", "twitch");
        streaming_service_box->add_item("YouTube", "youtube");
        streaming_service_box->add_item("Custom", "custom");
        streaming_service_box_ptr = streaming_service_box.get();
        return streaming_service_box;
    }

    std::unique_ptr<List> SettingsPage::create_streaming_service_section() {
        auto streaming_service_list = std::make_unique<List>(List::Orientation::VERTICAL);
        streaming_service_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Stream service:", get_color_theme().text_color));
        streaming_service_list->add_widget(create_streaming_service_box());
        return streaming_service_list;
    }

    std::unique_ptr<List> SettingsPage::create_stream_key_section() {
        auto stream_key_list = std::make_unique<List>(List::Orientation::VERTICAL);
        stream_key_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Stream key:", get_color_theme().text_color));

        auto twitch_stream_key_entry = std::make_unique<Entry>(&get_theme().body_font, "", get_theme().body_font.get_character_size() * 20);
        twitch_stream_key_entry_ptr = twitch_stream_key_entry.get();
        stream_key_list->add_widget(std::move(twitch_stream_key_entry));

        auto youtube_stream_key_entry = std::make_unique<Entry>(&get_theme().body_font, "", get_theme().body_font.get_character_size() * 20);
        youtube_stream_key_entry_ptr = youtube_stream_key_entry.get();
        stream_key_list->add_widget(std::move(youtube_stream_key_entry));

        stream_key_list_ptr = stream_key_list.get();
        return stream_key_list;
    }

    std::unique_ptr<List> SettingsPage::create_stream_url_section() {
        auto stream_url_list = std::make_unique<List>(List::Orientation::VERTICAL);
        stream_url_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "URL:", get_color_theme().text_color));

        auto stream_url_entry = std::make_unique<Entry>(&get_theme().body_font, "", get_theme().body_font.get_character_size() * 20);
        stream_url_entry_ptr = stream_url_entry.get();
        stream_url_list->add_widget(std::move(stream_url_entry));

        stream_url_list_ptr = stream_url_list.get();
        return stream_url_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_stream_container_box() {
        auto container_box = std::make_unique<ComboBox>(&get_theme().body_font);
        container_box->add_item("mp4", "mp4");
        container_box->add_item("flv", "flv");
        container_box->add_item("ts", "mpegts");
        container_box->add_item("m3u8", "hls");
        container_box_ptr = container_box.get();
        return container_box;
    }
    
    std::unique_ptr<List> SettingsPage::create_stream_container_section() {
        auto container_list = std::make_unique<List>(List::Orientation::VERTICAL);
        container_list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Container:", get_color_theme().text_color));
        container_list->add_widget(create_stream_container_box());
        container_list_ptr = container_list.get();
        return container_list;
    }

    void SettingsPage::add_stream_widgets() {
        auto streaming_info_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        streaming_info_list->add_widget(create_streaming_service_section());
        streaming_info_list->add_widget(create_stream_key_section());
        streaming_info_list->add_widget(create_stream_url_section());
        streaming_info_list->add_widget(create_stream_container_section());
        settings_list_ptr->add_widget(std::make_unique<Subsection>("Streaming info", std::move(streaming_info_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        auto checkboxes_list = std::make_unique<List>(List::Orientation::VERTICAL);

        auto show_streaming_started_notification_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Show streaming started notification");
        show_streaming_started_notification_checkbox->set_checked(true);
        show_streaming_started_notification_checkbox_ptr = show_streaming_started_notification_checkbox.get();
        checkboxes_list->add_widget(std::move(show_streaming_started_notification_checkbox));

        auto show_streaming_stopped_notification_checkbox = std::make_unique<CheckBox>(&get_theme().body_font, "Show streaming stopped notification");
        show_streaming_stopped_notification_checkbox->set_checked(true);
        show_streaming_stopped_notification_checkbox_ptr = show_streaming_stopped_notification_checkbox.get();
        checkboxes_list->add_widget(std::move(show_streaming_stopped_notification_checkbox));

        auto notifications_subsection = std::make_unique<Subsection>("Notifications", std::move(checkboxes_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));        
        Subsection *notifications_subsection_ptr = notifications_subsection.get();
        settings_list_ptr->add_widget(std::move(notifications_subsection));

        streaming_service_box_ptr->on_selection_changed = [this](const std::string &text, const std::string &id) {
            (void)text;
            const bool twitch_option = id == "twitch";
            const bool youtube_option = id == "youtube";
            const bool custom_option = id == "custom";
            stream_key_list_ptr->set_visible(!custom_option);
            stream_url_list_ptr->set_visible(custom_option);
            container_list_ptr->set_visible(custom_option);
            twitch_stream_key_entry_ptr->set_visible(twitch_option);
            youtube_stream_key_entry_ptr->set_visible(youtube_option);
            return true;
        };
        streaming_service_box_ptr->on_selection_changed("Twitch", "twitch");

        view_radio_button_ptr->on_selection_changed = [this, notifications_subsection_ptr](const std::string &text, const std::string &id) {
            (void)text;
            const bool advanced_view = id == "advanced";
            color_range_list_ptr->set_visible(advanced_view);
            audio_codec_ptr->set_visible(advanced_view);
            video_codec_ptr->set_visible(advanced_view);
            framerate_mode_list_ptr->set_visible(advanced_view);
            notifications_subsection_ptr->set_visible(advanced_view);
            settings_scrollable_page_ptr->reset_scroll();
            return true;
        };
        view_radio_button_ptr->on_selection_changed("Simple", "simple");
    }

    void SettingsPage::on_navigate_away_from_page() {
        save();
    }

    void SettingsPage::load() {
        switch(type) {
            case Type::REPLAY:
                load_replay();
                break;
            case Type::RECORD:
                load_record();
                break;
            case Type::STREAM:
                load_stream();
                break;
        }
    }

    void SettingsPage::save() {
        Config prev_config = config;
        switch(type) {
            case Type::REPLAY:
                save_replay();
                break;
            case Type::RECORD:
                save_record();
                break;
            case Type::STREAM:
                save_stream();
                break;
        }
        save_config(config);

        if(on_config_changed && config != prev_config)
            on_config_changed();
    }

    static const std::string* get_application_audio_by_name_case_insensitive(const std::vector<std::string> &application_audio, const std::string &name) {
        for(const auto &app_audio : application_audio) {
            if(strcasecmp(app_audio.c_str(), name.c_str()) == 0)
                return &app_audio;
        }
        return nullptr;
    }

    static bool starts_with(std::string_view str, const char *substr) {
        size_t len = strlen(substr);
        return str.size() >= len && memcmp(str.data(), substr, len) == 0;
    }

    void SettingsPage::load_audio_tracks(const RecordOptions &record_options) {
        audio_track_list_ptr->clear();
        for(const std::string &audio_track : record_options.audio_tracks) {
            if(starts_with(audio_track, "app:")) {
                if(!gsr_info->system_info.supports_app_audio)
                    continue;

                std::string audio_track_name = audio_track.substr(4);
                const std::string *app_audio = get_application_audio_by_name_case_insensitive(application_audio, audio_track_name);
                if(app_audio) {
                    std::unique_ptr<List> application_audio_widget = create_application_audio();
                    ComboBox *application_audio_box = static_cast<ComboBox*>(application_audio_widget->get_child_widget_by_index(1));
                    application_audio_box->set_selected_item(*app_audio);
                    audio_track_list_ptr->add_widget(std::move(application_audio_widget));
                } else {
                    std::unique_ptr<List> application_audio_widget = create_custom_application_audio();
                    Entry *application_audio_entry = static_cast<Entry*>(application_audio_widget->get_child_widget_by_index(1));
                    application_audio_entry->set_text(std::move(audio_track_name));
                    audio_track_list_ptr->add_widget(std::move(application_audio_widget));
                }
            } else if(starts_with(audio_track, "device:")) {
                std::unique_ptr<List> audio_track_widget = create_audio_device();
                ComboBox *audio_device_box = static_cast<ComboBox*>(audio_track_widget->get_child_widget_by_index(1));
                audio_device_box->set_selected_item(audio_track.substr(7));
                audio_track_list_ptr->add_widget(std::move(audio_track_widget));
            } else {
                std::unique_ptr<List> audio_track_widget = create_audio_device();
                ComboBox *audio_device_box = static_cast<ComboBox*>(audio_track_widget->get_child_widget_by_index(1));
                audio_device_box->set_selected_item(audio_track);
                audio_track_list_ptr->add_widget(std::move(audio_track_widget));
            }
        }
    }

    void SettingsPage::load_common(RecordOptions &record_options) {
        record_area_box_ptr->set_selected_item(record_options.record_area_option);
        if(merge_audio_tracks_checkbox_ptr)
            merge_audio_tracks_checkbox_ptr->set_checked(record_options.merge_audio_tracks);
        application_audio_invert_checkbox_ptr->set_checked(record_options.application_audio_invert);
        change_video_resolution_checkbox_ptr->set_checked(record_options.change_video_resolution);
        load_audio_tracks(record_options);
        color_range_box_ptr->set_selected_item(record_options.color_range);
        video_quality_box_ptr->set_selected_item(record_options.video_quality);
        video_codec_box_ptr->set_selected_item(record_options.video_codec);
        audio_codec_box_ptr->set_selected_item(record_options.audio_codec);
        framerate_mode_box_ptr->set_selected_item(record_options.framerate_mode);
        view_radio_button_ptr->set_selected_item(record_options.advanced_view ? "advanced" : "simple");
        // TODO:
        //record_options.overclock = false;
        record_cursor_checkbox_ptr->set_checked(record_options.record_cursor);
        restore_portal_session_checkbox_ptr->set_checked(record_options.restore_portal_session);

        if(record_options.record_area_width == 0)
            record_options.record_area_width = 1920;

        if(record_options.record_area_height == 0)
            record_options.record_area_height = 1080;

        if(record_options.video_width == 0)
            record_options.video_width = 1920;

        if(record_options.video_height == 0)
            record_options.video_height = 1080;

        if(record_options.record_area_width < 32)
            record_options.record_area_width = 32;
        area_width_entry_ptr->set_text(std::to_string(record_options.record_area_width));

        if(record_options.record_area_height < 32)
            record_options.record_area_height = 32;
        area_height_entry_ptr->set_text(std::to_string(record_options.record_area_height));

        if(record_options.video_width < 32)
            record_options.video_width = 32;
        video_width_entry_ptr->set_text(std::to_string(record_options.video_width));

        if(record_options.video_height < 32)
            record_options.video_height = 32;
        video_height_entry_ptr->set_text(std::to_string(record_options.video_height));

        if(record_options.fps < 1)
            record_options.fps = 1;
        framerate_entry_ptr->set_text(std::to_string(record_options.fps));

        if(record_options.video_bitrate < 1)
            record_options.video_bitrate = 1;
        video_bitrate_entry_ptr->set_text(std::to_string(record_options.video_bitrate));
    }

    void SettingsPage::load_replay() {
        load_common(config.replay_config.record_options);
        turn_on_replay_automatically_mode_ptr->set_selected_item(config.replay_config.turn_on_replay_automatically_mode);
        save_replay_in_game_folder_ptr->set_checked(config.replay_config.save_video_in_game_folder);
        if(restart_replay_on_save)
            restart_replay_on_save->set_checked(config.replay_config.restart_replay_on_save);
        show_replay_started_notification_checkbox_ptr->set_checked(config.replay_config.show_replay_started_notifications);
        show_replay_stopped_notification_checkbox_ptr->set_checked(config.replay_config.show_replay_stopped_notifications);
        show_replay_saved_notification_checkbox_ptr->set_checked(config.replay_config.show_replay_saved_notifications);
        save_directory_button_ptr->set_text(config.replay_config.save_directory);
        container_box_ptr->set_selected_item(config.replay_config.container);

        if(config.replay_config.replay_time < 2)
            config.replay_config.replay_time = 2;
        if(config.replay_config.replay_time > 10800)
            config.replay_config.replay_time = 10800;
        replay_time_entry_ptr->set_text(std::to_string(config.replay_config.replay_time));
    }

    void SettingsPage::load_record() {
        load_common(config.record_config.record_options);
        save_recording_in_game_folder_ptr->set_checked(config.record_config.save_video_in_game_folder);
        show_recording_started_notification_checkbox_ptr->set_checked(config.record_config.show_recording_started_notifications);
        show_video_saved_notification_checkbox_ptr->set_checked(config.record_config.show_video_saved_notifications);
        save_directory_button_ptr->set_text(config.record_config.save_directory);
        container_box_ptr->set_selected_item(config.record_config.container);
    }

    void SettingsPage::load_stream() {
        load_common(config.streaming_config.record_options);
        show_streaming_started_notification_checkbox_ptr->set_checked(config.streaming_config.show_streaming_started_notifications);
        show_streaming_stopped_notification_checkbox_ptr->set_checked(config.streaming_config.show_streaming_stopped_notifications);
        streaming_service_box_ptr->set_selected_item(config.streaming_config.streaming_service);
        youtube_stream_key_entry_ptr->set_text(config.streaming_config.youtube.stream_key);
        twitch_stream_key_entry_ptr->set_text(config.streaming_config.twitch.stream_key);
        stream_url_entry_ptr->set_text(config.streaming_config.custom.url);
        container_box_ptr->set_selected_item(config.streaming_config.custom.container);
    }

    static void save_audio_tracks(std::vector<std::string> &audio_devices, List *audio_devices_list_ptr) {
        audio_devices.clear();
        audio_devices_list_ptr->for_each_child_widget([&audio_devices](std::unique_ptr<Widget> &child_widget) {
            List *audio_track_line = static_cast<List*>(child_widget.get());
            const AudioTrackType audio_track_type = (AudioTrackType)(uintptr_t)audio_track_line->userdata;
            switch(audio_track_type) {
                case AudioTrackType::DEVICE: {
                    ComboBox *audio_device_box = static_cast<ComboBox*>(audio_track_line->get_child_widget_by_index(1));
                    audio_devices.push_back("device:" + audio_device_box->get_selected_id());
                    break;
                }
                case AudioTrackType::APPLICATION: {
                    ComboBox *application_audio_box = static_cast<ComboBox*>(audio_track_line->get_child_widget_by_index(1));
                    audio_devices.push_back("app:" + application_audio_box->get_selected_id());
                    break;
                }
                case AudioTrackType::APPLICATION_CUSTOM: {
                    Entry *application_audio_entry = static_cast<Entry*>(audio_track_line->get_child_widget_by_index(1));
                    audio_devices.push_back("app:" + application_audio_entry->get_text());
                    break;
                }
            }
            return true;
        });
    }

    void SettingsPage::save_common(RecordOptions &record_options) {
        record_options.record_area_option = record_area_box_ptr->get_selected_id();
        record_options.record_area_width = atoi(area_width_entry_ptr->get_text().c_str());
        record_options.record_area_height = atoi(area_height_entry_ptr->get_text().c_str());
        record_options.video_width = atoi(video_width_entry_ptr->get_text().c_str());
        record_options.video_height = atoi(video_height_entry_ptr->get_text().c_str());
        record_options.fps = atoi(framerate_entry_ptr->get_text().c_str());
        record_options.video_bitrate = atoi(video_bitrate_entry_ptr->get_text().c_str());
        if(merge_audio_tracks_checkbox_ptr)
            record_options.merge_audio_tracks = merge_audio_tracks_checkbox_ptr->is_checked();
        record_options.application_audio_invert = application_audio_invert_checkbox_ptr->is_checked();
        record_options.change_video_resolution = change_video_resolution_checkbox_ptr->is_checked();
        save_audio_tracks(record_options.audio_tracks, audio_track_list_ptr);
        record_options.color_range = color_range_box_ptr->get_selected_id();
        record_options.video_quality = video_quality_box_ptr->get_selected_id();
        record_options.video_codec = video_codec_box_ptr->get_selected_id();
        record_options.audio_codec = audio_codec_box_ptr->get_selected_id();
        record_options.framerate_mode = framerate_mode_box_ptr->get_selected_id();
        record_options.advanced_view = view_radio_button_ptr->get_selected_id() == "advanced";
        // TODO:
        //record_options.overclock = false;
        record_options.record_cursor = record_cursor_checkbox_ptr->is_checked();
        record_options.restore_portal_session = restore_portal_session_checkbox_ptr->is_checked();

        if(record_options.record_area_width == 0)
            record_options.record_area_width = 1920;

        if(record_options.record_area_height == 0)
            record_options.record_area_height = 1080;

        if(record_options.video_width == 0)
            record_options.video_width = 1920;

        if(record_options.video_height == 0)
            record_options.video_height = 1080;

        if(record_options.record_area_width < 32) {
            record_options.record_area_width = 32;
            area_width_entry_ptr->set_text("32");
        }

        if(record_options.record_area_height < 32) {
            record_options.record_area_height = 32;
            area_height_entry_ptr->set_text("32");
        }

        if(record_options.video_width < 32) {
            record_options.video_width = 32;
            video_width_entry_ptr->set_text("32");
        }

        if(record_options.video_height < 32) {
            record_options.video_height = 32;
            video_height_entry_ptr->set_text("32");
        }

        if(record_options.fps < 1) {
            record_options.fps = 1;
            framerate_entry_ptr->set_text("1");
        }

        if(record_options.video_bitrate < 1) {
            record_options.video_bitrate = 1;
            video_bitrate_entry_ptr->set_text("1");
        }
    }

    void SettingsPage::save_replay() {
        save_common(config.replay_config.record_options);
        config.replay_config.turn_on_replay_automatically_mode = turn_on_replay_automatically_mode_ptr->get_selected_id();
        config.replay_config.save_video_in_game_folder = save_replay_in_game_folder_ptr->is_checked();
        if(restart_replay_on_save)
            config.replay_config.restart_replay_on_save = restart_replay_on_save->is_checked();
        config.replay_config.show_replay_started_notifications = show_replay_started_notification_checkbox_ptr->is_checked();
        config.replay_config.show_replay_stopped_notifications = show_replay_stopped_notification_checkbox_ptr->is_checked();
        config.replay_config.show_replay_saved_notifications = show_replay_saved_notification_checkbox_ptr->is_checked();
        config.replay_config.save_directory = save_directory_button_ptr->get_text();
        config.replay_config.container = container_box_ptr->get_selected_id();
        config.replay_config.replay_time = atoi(replay_time_entry_ptr->get_text().c_str());

        if(config.replay_config.replay_time < 5) {
            config.replay_config.replay_time = 5;
            replay_time_entry_ptr->set_text("5");
        }
    }

    void SettingsPage::save_record() {
        save_common(config.record_config.record_options);
        config.record_config.save_video_in_game_folder = save_recording_in_game_folder_ptr->is_checked();
        config.record_config.show_recording_started_notifications = show_recording_started_notification_checkbox_ptr->is_checked();
        config.record_config.show_video_saved_notifications = show_video_saved_notification_checkbox_ptr->is_checked();
        config.record_config.save_directory = save_directory_button_ptr->get_text();
        config.record_config.container = container_box_ptr->get_selected_id();
    }

    void SettingsPage::save_stream() {
        save_common(config.streaming_config.record_options);
        config.streaming_config.show_streaming_started_notifications = show_streaming_started_notification_checkbox_ptr->is_checked();
        config.streaming_config.show_streaming_stopped_notifications = show_streaming_stopped_notification_checkbox_ptr->is_checked();
        config.streaming_config.streaming_service = streaming_service_box_ptr->get_selected_id();
        config.streaming_config.youtube.stream_key = youtube_stream_key_entry_ptr->get_text();
        config.streaming_config.twitch.stream_key = twitch_stream_key_entry_ptr->get_text();
        config.streaming_config.custom.url = stream_url_entry_ptr->get_text();
        config.streaming_config.custom.container = container_box_ptr->get_selected_id();
    }
}