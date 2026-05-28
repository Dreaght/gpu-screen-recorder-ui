#include "../../include/gui/ExportPage.hpp"
#include "../../include/Theme.hpp"
#include "../../include/Utils.hpp"
#include "../../include/Process.hpp"
#include "../../include/gui/PageStack.hpp"
#include "../../include/gui/Subsection.hpp"
#include "../../include/gui/List.hpp"
#include "../../include/gui/Label.hpp"
#include "../../include/gui/ComboBox.hpp"
#include "../../include/gui/Entry.hpp"
#include "../../include/gui/CheckBox.hpp"
#include "../../include/gui/FileChooser.hpp"
#include "../../include/gui/GsrPage.hpp"
#include "../../include/gui/ScrollablePage.hpp"
#include "../../include/GsrInfo.hpp"
#include "../../include/Translation.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/window/Window.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <sys/stat.h>

namespace gsr {
    namespace {
        static const float button_spacing_scale = 0.015f;

        template<typename T>
        static T sv_to_int(std::string_view str) {
            long long result = 0;
            sscanf(str.data(), "%lld", &result);
            return (T)result;
        }

        static std::string get_container_from_filepath(const std::string &filepath) {
            const std::string extension = std::filesystem::path(filepath).extension().string();
            if(extension == ".mkv")
                return "matroska";
            if(extension == ".webm")
                return "webm";
            if(extension == ".mov")
                return "mov";
            return "mp4";
        }

        static std::string get_parent_directory(const std::string &filepath) {
            const std::filesystem::path path(filepath);
            if(path.has_parent_path())
                return path.parent_path().string();
            return get_videos_dir();
        }

        static std::string format_duration(int64_t duration_ms) {
            const int64_t total_seconds = std::max<int64_t>(0, duration_ms / 1000);
            const int64_t hours = total_seconds / 3600;
            const int64_t minutes = (total_seconds % 3600) / 60;
            const int64_t seconds = total_seconds % 60;

            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld",
                (long long)hours,
                (long long)minutes,
                (long long)seconds);
            return buffer;
        }

        static std::string format_file_size(int64_t bytes) {
            const char *units[] = {"B", "KB", "MB", "GB", "TB"};
            double size = (double)std::max<int64_t>(0, bytes);
            size_t unit_index = 0;
            while(size >= 1024.0 && unit_index + 1 < std::size(units)) {
                size /= 1024.0;
                ++unit_index;
            }

            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%.2f%s", size, units[unit_index]);
            return buffer;
        }

        static std::string humanize_video_codec(const std::string &codec) {
            if(codec == "h264")
                return "H264";
            if(codec == "hevc")
                return "HEVC";
            if(codec == "av1")
                return "AV1";
            if(codec == "vp8")
                return "VP8";
            if(codec == "vp9")
                return "VP9";
            if(codec == "h264_software")
                return "H264 Software";
            if(codec.empty())
                return TR("Unknown");
            return codec;
        }

        static std::string humanize_audio_codec(const std::string &codec) {
            if(codec == "aac")
                return "AAC";
            if(codec == "opus")
                return "Opus";
            if(codec.empty())
                return TR("Unknown");
            return codec;
        }

        static std::string map_video_codec_to_option_id(const std::string &codec) {
            if(codec == "h264" || codec == "hevc" || codec == "av1" || codec == "vp8" || codec == "vp9" || codec == "h264_software")
                return codec;
            return "auto";
        }

        static std::string map_audio_codec_to_option_id(const std::string &codec) {
            if(codec == "aac" || codec == "opus")
                return codec;
            return "aac";
        }

        static int64_t bitrate_kbps_from_bps(double bitrate_bps) {
            if(bitrate_bps <= 0.0)
                return 0;
            return std::max<int64_t>(1, (int64_t)std::llround(bitrate_bps / 1000.0));
        }

        static bool parse_ffprobe_key_value(const std::string &output, const char *key, std::string &value) {
            std::istringstream iss(output);
            std::string line;
            const std::string prefix = std::string(key) + "=";
            while(std::getline(iss, line)) {
                if(starts_with(line, prefix.c_str())) {
                    value = line.substr(prefix.size());
                    return true;
                }
            }
            return false;
        }
    }

    ExportPage::ExportPage(const GsrInfo *gsr_info, PageStack *page_stack, VideoMetadata video_metadata, std::vector<TimelineWidget::TimelineChunk> chunks) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        gsr_info(gsr_info),
        page_stack(page_stack),
        top_text(TR("Export"), get_theme().title_font_desc.c_str()),
        bottom_text(TR("Video Trimmer"), get_theme().title_font_desc.c_str())
    {
        source_info.metadata = std::move(video_metadata);
        source_info.chunks = std::move(chunks);
        load_source_video_info();

        const float margin = 0.02f;
        set_margins(margin, margin, margin, margin);

        add_button(TR("Export"), "export", get_color_theme().tint_color);
        add_button(TR("Cancel"), "cancel", get_color_theme().page_bg_color);
        on_click = [page_stack](const std::string &id) {
            if(id == "cancel")
                page_stack->pop();
            if(id == "export")
                fprintf(stderr, "Export pressed!\n");
        };

        add_widgets();
        apply_source_defaults();
        update_reencode_options_visibility();
        update_source_summary();
        update_estimated_file_size();
    }

    bool ExportPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        for(size_t i = 0; i < buttons.size(); ++i) {
            ButtonItem &button_item = buttons[i];
            if(!button_item.button->on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
                return false;
        }

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        const mgl::vec2f content_page_position = get_content_position() + mgl::vec2f(margin_left, get_border_size() + margin_top).floor();

        Widget *selected_widget = selected_child_widget;

        if(selected_widget) {
            if(!selected_widget->on_event(event, window, content_page_position))
                return false;
        }

        return widgets.for_each_reverse([selected_widget, &window, &event, content_page_position](std::unique_ptr<Widget> &widget) {
            Widget *p = widget.get();
            if(p != selected_widget) {
                if(!p->on_event(event, window, content_page_position))
                    return false;
            }
            return true;
        });
    }

    void ExportPage::draw(mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return;

        const mgl::vec2f content_page_position = get_content_position();
        const mgl::vec2f content_page_size = get_size();

        mgl::Rectangle background(content_page_size);
        background.set_position(content_page_position);
        background.set_color(get_color_theme().page_bg_color);
        window.draw(background);

        mgl::Rectangle border(mgl::vec2f(content_page_size.x, get_border_size()).floor());
        border.set_position(content_page_position);
        border.set_color(get_color_theme().tint_color);
        window.draw(border);

        draw_page_label(window, content_page_position);
        draw_buttons(window, content_page_position, content_page_size);

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        draw_children(window, content_page_position + mgl::vec2f(margin_left, get_border_size() + margin_top).floor());
    }

    void ExportPage::draw_page_label(mgl::Window &window, mgl::vec2f body_pos) {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();

        mgl::Rectangle background(mgl::vec2f(window_size.x / 10, window_size.x / 10).floor());
        background.set_position(body_pos.floor() - mgl::vec2f(background.get_size().x + get_horizontal_spacing(), 0.0f).floor());
        background.set_color(mgl::Color(0, 0, 0, 255));
        window.draw(background);

        const int text_margin = background.get_size().y * 0.085;

        top_text.set_position((background.get_position() + mgl::vec2f(background.get_size().x * 0.5f - top_text.get_bounds().size.x * 0.5f, text_margin)).floor());
        window.draw(top_text);

        mgl::Sprite icon(&get_theme().trimmer_texture);
        icon.set_height((int)(background.get_size().y * 0.8f));
        icon.set_position((background.get_position() + background.get_size() * 0.5f - icon.get_size() * 0.5f).floor());
        window.draw(icon);

        bottom_text.set_position((background.get_position() + mgl::vec2f(background.get_size().x * 0.5f - bottom_text.get_bounds().size.x * 0.5f, background.get_size().y - bottom_text.get_bounds().size.y - text_margin)).floor());
        window.draw(bottom_text);
    }

    void ExportPage::draw_buttons(mgl::Window &window, mgl::vec2f body_pos, mgl::vec2f body_size) {
        float offset_y = 0.0f;
        for(size_t i = 0; i < buttons.size(); ++i) {
            ButtonItem &button_item = buttons[i];
            button_item.button->set_position(body_pos + mgl::vec2f(body_size.x + get_horizontal_spacing(), offset_y).floor());
            button_item.button->draw(window, mgl::vec2f(0.0f, 0.0f));
            offset_y += button_item.button->get_size().y + (button_spacing_scale * get_theme().window_height);
        }
    }

    void ExportPage::draw_children(mgl::Window &window, mgl::vec2f position) {
        Widget *selected_widget = selected_child_widget;

        const mgl::Scissor prev_scissor = window.get_scissor();
        window.set_scissor({position.to_vec2i(), get_inner_size().to_vec2i()});

        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget)
                widget->draw(window, position);
        }

        if(selected_widget)
            selected_widget->draw(window, position);

        window.set_scissor(prev_scissor);
    }

    std::unique_ptr<Widget> ExportPage::create_save_directory(const char *label) {
        auto save_directory_list = std::make_unique<List>(List::Orientation::VERTICAL);
        save_directory_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), label, get_color_theme().text_color));
        auto save_directory_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), get_parent_directory(source_info.metadata.filepath).c_str(), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        save_directory_button_ptr = save_directory_button.get();
        save_directory_button->on_click = [this]() {
            auto select_directory_page = std::make_unique<GsrPage>(TR("File"), TR("Export"));
            select_directory_page->add_button(TR("Save"), "save", get_color_theme().tint_color);
            select_directory_page->add_button(TR("Cancel"), "cancel", get_color_theme().page_bg_color);

            auto file_chooser = std::make_unique<FileChooser>(save_directory_button_ptr->get_text(), select_directory_page->get_inner_size());
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

    std::unique_ptr<ComboBox> ExportPage::create_container_box() {
        auto container_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        container_box->add_item("mp4", "mp4");
        container_box->add_item("mkv", "matroska");
        container_box->add_item("webm", "webm");
        container_box->add_item("mov", "mov");
        container_box_ptr = container_box.get();
        return container_box;
    }

    std::unique_ptr<Widget> ExportPage::create_container_section() {
        auto container_list = std::make_unique<List>(List::Orientation::VERTICAL);
        container_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Container:"), get_color_theme().text_color));
        container_list->add_widget(create_container_box());
        return container_list;
    }

    std::unique_ptr<Widget> ExportPage::create_file_info_section() {
        auto file_info_data_list = std::make_unique<List>(List::Orientation::VERTICAL);
        file_info_data_list->add_widget(create_save_directory(TR("Directory to save trimmed video:")));
        file_info_data_list->add_widget(create_container_section());
        return std::make_unique<Subsection>(TR("File info"), std::move(file_info_data_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Label> ExportPage::create_source_summary_label() {
        auto label = std::make_unique<Label>(get_theme().body_font_desc.c_str(), "", get_color_theme().text_color);
        label->set_wrap_width(get_inner_size().x);
        source_summary_label_ptr = label.get();
        return label;
    }

    std::unique_ptr<Label> ExportPage::create_estimated_file_size() {
        auto label = std::make_unique<Label>(get_theme().body_font_desc.c_str(), "", get_color_theme().text_color);
        label->set_wrap_width(get_inner_size().x);
        estimated_file_size_ptr = label.get();
        return label;
    }

    std::unique_ptr<Widget> ExportPage::create_source_info_section() {
        auto source_info_list = std::make_unique<List>(List::Orientation::VERTICAL);
        source_info_list->add_widget(create_source_summary_label());
        source_info_list->add_widget(create_estimated_file_size());
        return std::make_unique<Subsection>(TR("Source"), std::move(source_info_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Widget> ExportPage::create_reencode_video_checkbox() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Re-encode video to reduce file size"));
        reencode_video_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Widget> ExportPage::create_reencode_audio_checkbox() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Re-encode audio to reduce file size"));
        reencode_audio_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<ComboBox> ExportPage::create_video_codec_box() {
        auto video_codec_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        video_codec_box->add_item(TR("Auto (Recommended)"), "auto");
        if(gsr_info && gsr_info->supported_video_codecs.h264)
            video_codec_box->add_item(TR("H264"), "h264");
        if(gsr_info && gsr_info->supported_video_codecs.hevc)
            video_codec_box->add_item(TR("HEVC"), "hevc");
        if(gsr_info && gsr_info->supported_video_codecs.av1)
            video_codec_box->add_item(TR("AV1"), "av1");
        if(gsr_info && gsr_info->supported_video_codecs.vp9)
            video_codec_box->add_item(TR("VP9"), "vp9");
        if(gsr_info && gsr_info->supported_video_codecs.vp8)
            video_codec_box->add_item(TR("VP8"), "vp8");
        if(gsr_info && gsr_info->supported_video_codecs.h264_software)
            video_codec_box->add_item(TR("H264 Software Encoder (Slow, not recommended)"), "h264_software");
        video_codec_box_ptr = video_codec_box.get();
        return video_codec_box;
    }

    std::unique_ptr<Widget> ExportPage::create_video_codec() {
        auto video_codec_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_codec_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Video codec:"), get_color_theme().text_color));
        video_codec_list->add_widget(create_video_codec_box());
        return video_codec_list;
    }

    std::unique_ptr<ComboBox> ExportPage::create_audio_codec_box() {
        auto audio_codec_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        audio_codec_box->add_item(TR("AAC"), "aac");
        audio_codec_box->add_item(TR("Opus"), "opus");
        audio_codec_box_ptr = audio_codec_box.get();
        return audio_codec_box;
    }

    std::unique_ptr<Widget> ExportPage::create_audio_codec() {
        auto audio_codec_list = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_codec_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Audio codec:"), get_color_theme().text_color));
        audio_codec_list->add_widget(create_audio_codec_box());
        return audio_codec_list;
    }

    std::unique_ptr<Entry> ExportPage::create_video_bitrate_entry() {
        auto entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "8000", (int)(2.0f * mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 6));
        entry->set_number_mode(true, 1, 500000);
        video_bitrate_entry_ptr = entry.get();
        return entry;
    }

    std::unique_ptr<Entry> ExportPage::create_audio_bitrate_entry() {
        auto entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "128", (int)(2.0f * mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 5));
        entry->set_number_mode(true, 1, 1000);
        audio_bitrate_entry_ptr = entry.get();
        return entry;
    }

    std::unique_ptr<Widget> ExportPage::create_video_bitrate() {
        auto video_bitrate_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_bitrate_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Target video bitrate (Kbps):"), get_color_theme().text_color));
        video_bitrate_list->add_widget(create_video_bitrate_entry());
        return video_bitrate_list;
    }

    std::unique_ptr<Widget> ExportPage::create_audio_bitrate() {
        auto audio_bitrate_list = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_bitrate_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Target audio bitrate (Kbps):"), get_color_theme().text_color));
        audio_bitrate_list->add_widget(create_audio_bitrate_entry());
        return audio_bitrate_list;
    }

    std::unique_ptr<Widget> ExportPage::create_video_section() {
        auto video_section_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_section_list->add_widget(create_reencode_video_checkbox());

        auto video_reencode_options = std::make_unique<List>(List::Orientation::VERTICAL);
        video_reencode_options_ptr = video_reencode_options.get();
        video_reencode_options->add_widget(create_video_codec());
        video_reencode_options->add_widget(create_video_bitrate());
        video_section_list->add_widget(std::move(video_reencode_options));

        video_section_list->add_widget(create_reencode_audio_checkbox());

        auto audio_reencode_options = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_reencode_options_ptr = audio_reencode_options.get();
        audio_reencode_options->add_widget(create_audio_codec());
        audio_reencode_options->add_widget(create_audio_bitrate());
        video_section_list->add_widget(std::move(audio_reencode_options));

        return std::make_unique<Subsection>(TR("Compression"), std::move(video_section_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Widget> ExportPage::create_settings() {
        auto scrollable_page = std::make_unique<ScrollablePage>(get_inner_size());
        settings_scrollable_page_ptr = scrollable_page.get();

        auto settings_list = std::make_unique<List>(List::Orientation::VERTICAL);
        settings_list->set_spacing(0.018f);
        settings_list->add_widget(create_file_info_section());
        settings_list->add_widget(create_source_info_section());
        settings_list->add_widget(create_video_section());
        scrollable_page->add_widget(std::move(settings_list));

        return scrollable_page;
    }

    void ExportPage::add_widgets() {
        add_widget(create_settings());

        reencode_video_checkbox_ptr->on_changed = [this](bool) {
            update_reencode_options_visibility();
            update_estimated_file_size();
        };
        reencode_audio_checkbox_ptr->on_changed = [this](bool) {
            update_reencode_options_visibility();
            update_estimated_file_size();
        };
        video_bitrate_entry_ptr->on_changed = [this](std::string_view) {
            update_estimated_file_size();
        };
        audio_bitrate_entry_ptr->on_changed = [this](std::string_view) {
            update_estimated_file_size();
        };
        video_codec_box_ptr->on_selection_changed = [this](std::string_view, std::string_view) {
            update_estimated_file_size();
            return true;
        };
        audio_codec_box_ptr->on_selection_changed = [this](std::string_view, std::string_view) {
            update_estimated_file_size();
            return true;
        };
        container_box_ptr->on_selection_changed = [this](std::string_view, std::string_view) {
            update_estimated_file_size();
            return true;
        };
    }

    void ExportPage::load_source_video_info() {
        if(source_info.metadata.file_size <= 0) {
            struct stat st;
            if(stat(source_info.metadata.filepath.c_str(), &st) == 0)
                source_info.metadata.file_size = st.st_size;
        }

        {
            const char *args[] = {
                "ffprobe",
                "-v", "error",
                "-select_streams", "v:0",
                "-show_entries", "stream=codec_name,bit_rate,width,height",
                "-of", "default=noprint_wrappers=1:nokey=0",
                source_info.metadata.filepath.c_str(),
                nullptr
            };

            std::string output;
            if(exec_program_on_host_get_stdout(args, output, false) == 0) {
                std::string value;
                if(parse_ffprobe_key_value(output, "codec_name", value))
                    source_info.video_codec = value;
                if(parse_ffprobe_key_value(output, "bit_rate", value)) {
                    source_info.video_bitrate_kbps = bitrate_kbps_from_bps(strtod(value.c_str(), nullptr));
                    source_info.has_video_bitrate = source_info.video_bitrate_kbps > 0;
                }
                if(source_info.metadata.width <= 0 && parse_ffprobe_key_value(output, "width", value))
                    source_info.metadata.width = atoi(value.c_str());
                if(source_info.metadata.height <= 0 && parse_ffprobe_key_value(output, "height", value))
                    source_info.metadata.height = atoi(value.c_str());
            }
        }

        {
            const char *args[] = {
                "ffprobe",
                "-v", "error",
                "-select_streams", "a:0",
                "-show_entries", "stream=codec_name,bit_rate",
                "-of", "default=noprint_wrappers=1:nokey=0",
                source_info.metadata.filepath.c_str(),
                nullptr
            };

            std::string output;
            if(exec_program_on_host_get_stdout(args, output, false) == 0) {
                std::string value;
                if(parse_ffprobe_key_value(output, "codec_name", value))
                    source_info.audio_codec = value;
                if(parse_ffprobe_key_value(output, "bit_rate", value)) {
                    source_info.audio_bitrate_kbps = bitrate_kbps_from_bps(strtod(value.c_str(), nullptr));
                    source_info.has_audio_bitrate = source_info.audio_bitrate_kbps > 0;
                }
            }
        }

        {
            const char *args[] = {
                "ffprobe",
                "-v", "error",
                "-show_entries", "format=duration,bit_rate",
                "-of", "default=noprint_wrappers=1:nokey=0",
                source_info.metadata.filepath.c_str(),
                nullptr
            };

            std::string output;
            if(exec_program_on_host_get_stdout(args, output, false) == 0) {
                std::string value;
                if(source_info.metadata.duration_seconds <= 0.0 && parse_ffprobe_key_value(output, "duration", value))
                    source_info.metadata.duration_seconds = strtod(value.c_str(), nullptr);
                if(parse_ffprobe_key_value(output, "bit_rate", value))
                    source_info.total_bitrate_kbps = bitrate_kbps_from_bps(strtod(value.c_str(), nullptr));
            }
        }

        if(source_info.total_bitrate_kbps <= 0 && source_info.metadata.file_size > 0 && source_info.metadata.duration_seconds > 0.0) {
            source_info.total_bitrate_kbps = std::max<int64_t>(1,
                (int64_t)std::llround(((double)source_info.metadata.file_size * 8.0) / source_info.metadata.duration_seconds / 1000.0));
        }

        if(source_info.video_bitrate_kbps <= 0 && source_info.has_audio_bitrate && source_info.total_bitrate_kbps > source_info.audio_bitrate_kbps) {
            source_info.video_bitrate_kbps = source_info.total_bitrate_kbps - source_info.audio_bitrate_kbps;
            source_info.has_video_bitrate = source_info.video_bitrate_kbps > 0;
        }
        if(source_info.audio_bitrate_kbps <= 0 && source_info.has_video_bitrate && source_info.total_bitrate_kbps > source_info.video_bitrate_kbps) {
            source_info.audio_bitrate_kbps = source_info.total_bitrate_kbps - source_info.video_bitrate_kbps;
            source_info.has_audio_bitrate = source_info.audio_bitrate_kbps > 0;
        }
        if(source_info.audio_bitrate_kbps <= 0 && !source_info.audio_codec.empty())
            source_info.audio_bitrate_kbps = 128;

        source_info.container = get_container_from_filepath(source_info.metadata.filepath);
    }

    void ExportPage::apply_source_defaults() {
        save_directory_button_ptr->set_text(get_parent_directory(source_info.metadata.filepath));
        container_box_ptr->set_selected_item(source_info.container);
        video_codec_box_ptr->set_selected_item(map_video_codec_to_option_id(source_info.video_codec));
        audio_codec_box_ptr->set_selected_item(map_audio_codec_to_option_id(source_info.audio_codec));
        video_bitrate_entry_ptr->set_text(std::to_string(std::max<int64_t>(1, source_info.video_bitrate_kbps)));
        audio_bitrate_entry_ptr->set_text(std::to_string(std::max<int64_t>(1, source_info.audio_bitrate_kbps > 0 ? source_info.audio_bitrate_kbps : 128)));
        reencode_video_checkbox_ptr->set_checked(false);
        reencode_audio_checkbox_ptr->set_checked(false);
    }

    void ExportPage::update_reencode_options_visibility() {
        video_reencode_options_ptr->set_visible(reencode_video_checkbox_ptr->is_checked());
        audio_reencode_options_ptr->set_visible(reencode_audio_checkbox_ptr->is_checked());
    }

    int64_t ExportPage::get_selected_duration_ms() const {
        int64_t duration_ms = 0;
        for(const auto &chunk : source_info.chunks) {
            if(chunk.enabled && chunk.end_ms > chunk.start_ms)
                duration_ms += chunk.end_ms - chunk.start_ms;
        }

        if(duration_ms > 0)
            return duration_ms;
        return std::max<int64_t>(0, (int64_t)std::llround(source_info.metadata.duration_seconds * 1000.0));
    }

    void ExportPage::update_source_summary() {
        const std::string total_bitrate_suffix = source_info.total_bitrate_kbps > 0
            ? std::string(", ") + std::to_string(source_info.total_bitrate_kbps) + "Kbps total"
            : std::string();
        const std::string video_bitrate_suffix = source_info.has_video_bitrate
            ? std::string(" (") + std::to_string(source_info.video_bitrate_kbps) + "Kbps)"
            : std::string();
        const std::string audio_bitrate_suffix = source_info.has_audio_bitrate
            ? std::string(" (") + std::to_string(source_info.audio_bitrate_kbps) + "Kbps)"
            : std::string();

        char buffer[1024];
        snprintf(buffer, sizeof(buffer),
            TR("Source video: %dx%d, %s, %s%s.\nSelected trim duration: %s.\nSource codecs: video %s%s, audio %s%s."),
            source_info.metadata.width,
            source_info.metadata.height,
            format_duration((int64_t)std::llround(source_info.metadata.duration_seconds * 1000.0)).c_str(),
            format_file_size(source_info.metadata.file_size).c_str(),
            total_bitrate_suffix.c_str(),
            format_duration(get_selected_duration_ms()).c_str(),
            humanize_video_codec(source_info.video_codec).c_str(),
            video_bitrate_suffix.c_str(),
            humanize_audio_codec(source_info.audio_codec).c_str(),
            audio_bitrate_suffix.c_str());
        source_summary_label_ptr->set_text(buffer);
    }

    void ExportPage::update_estimated_file_size() {
        const int64_t selected_duration_ms = get_selected_duration_ms();
        if(selected_duration_ms <= 0) {
            estimated_file_size_ptr->set_text(TR("Estimated output file size unavailable."));
            return;
        }

        const bool reencode_video = reencode_video_checkbox_ptr->is_checked();
        const bool reencode_audio = reencode_audio_checkbox_ptr->is_checked();

        int64_t estimated_size_bytes = 0;
        if(!reencode_video && !reencode_audio && source_info.metadata.file_size > 0 && source_info.metadata.duration_seconds > 0.0) {
            estimated_size_bytes = (int64_t)std::llround((double)source_info.metadata.file_size * ((double)selected_duration_ms / (source_info.metadata.duration_seconds * 1000.0)));
        } else {
            int64_t total_bitrate_kbps = source_info.total_bitrate_kbps;
            if(total_bitrate_kbps <= 0) {
                total_bitrate_kbps = 0;
                if(source_info.has_video_bitrate)
                    total_bitrate_kbps += source_info.video_bitrate_kbps;
                if(source_info.has_audio_bitrate)
                    total_bitrate_kbps += source_info.audio_bitrate_kbps;
            }

            if(reencode_video) {
                const int64_t target_video_bitrate_kbps = sv_to_int<int64_t>(video_bitrate_entry_ptr->get_text());
                if(total_bitrate_kbps > 0 && source_info.has_video_bitrate)
                    total_bitrate_kbps = std::max<int64_t>(1, total_bitrate_kbps - source_info.video_bitrate_kbps + target_video_bitrate_kbps);
                else if(total_bitrate_kbps <= 0)
                    total_bitrate_kbps += target_video_bitrate_kbps;
            }

            if(reencode_audio) {
                const int64_t target_audio_bitrate_kbps = sv_to_int<int64_t>(audio_bitrate_entry_ptr->get_text());
                if(total_bitrate_kbps > 0 && source_info.has_audio_bitrate)
                    total_bitrate_kbps = std::max<int64_t>(1, total_bitrate_kbps - source_info.audio_bitrate_kbps + target_audio_bitrate_kbps);
                else if(total_bitrate_kbps <= 0)
                    total_bitrate_kbps += target_audio_bitrate_kbps;
            }

            total_bitrate_kbps = std::max<int64_t>(1, total_bitrate_kbps);
            estimated_size_bytes = (int64_t)std::llround(((double)selected_duration_ms / 1000.0) * ((double)total_bitrate_kbps * 1000.0 / 8.0));
        }

        char buffer[512];
        if(!reencode_video && !reencode_audio) {
            snprintf(buffer, sizeof(buffer),
                TR("Estimated trimmed file size without re-encoding: %s.\nThis is based on the selected chunks relative to the original file size."),
                format_file_size(estimated_size_bytes).c_str());
        } else {
            snprintf(buffer, sizeof(buffer),
                TR("Estimated output file size: %s.\nThis is approximate and based on the selected trim duration and target bitrates."),
                format_file_size(estimated_size_bytes).c_str());
        }
        estimated_file_size_ptr->set_text(buffer);
    }

    mgl::vec2f ExportPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        return (window_size * mgl::vec2f(0.4f, 0.48f)).floor();
    }

    mgl::vec2f ExportPage::get_inner_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_bottom = margin_bottom_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        const int margin_right = margin_right_scale * get_theme().window_height;
        return get_size() - mgl::vec2f(margin_left + margin_right, margin_top + margin_bottom + get_border_size());
    }

    void ExportPage::set_margins(float top, float bottom, float left, float right) {
        margin_top_scale = top;
        margin_bottom_scale = bottom;
        margin_left_scale = left;
        margin_right_scale = right;
    }

    void ExportPage::add_button(const std::string &text, const std::string &id, mgl::Color color) {
        auto button = std::make_unique<Button>(get_theme().title_font_desc.c_str(), text.c_str(),
            mgl::vec2f(get_theme().window_width / 10, get_theme().window_height / 15).floor(), color);
        button->set_border_scale(0.003f);
        button->on_click = [this, id]() {
            if(on_click)
                on_click(id);
        };
        buttons.push_back({ std::move(button), id });
    }

    float ExportPage::get_border_size() const {
        return 0.004f * get_theme().window_height;
    }

    float ExportPage::get_horizontal_spacing() const {
        return get_theme().window_width / 50;
    }

    mgl::vec2f ExportPage::get_content_position() {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = get_size();
        return mgl::vec2f(
            window_size.x * 0.5f - content_page_size.x * 0.5f,
            window_size.y * 0.5f - window_size.y * 0.7f * 0.5f
        ).floor();
    }
}
