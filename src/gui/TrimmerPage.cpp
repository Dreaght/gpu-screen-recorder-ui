#include "../../include/gui/TrimmerPage.hpp"
#include "../../include/Theme.hpp"
#include "../../include/Utils.hpp"
#include "../../include/gui/CustomRendererWidget.hpp"
#include "../../include/gui/ScrollablePage.hpp"
#include "../../include/gui/TimelineWidget.hpp"
#include "../../include/gui/VideoPlayer.hpp"
#include "../../include/gui/Utils.hpp"
#include "include/gui/List.hpp"
#include "include/gui/Label.hpp"

#include <mglpp/window/Window.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <sys/stat.h>
#include <utility>

#include "mglpp/graphics/Rectangle.hpp"

namespace gsr {
    TrimmerPage::TrimmerPage(const GsrInfo *gsr_info, PageStack *page_stack, VideoMetadata video_metadata) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        gsr_info(gsr_info),
        page_stack(page_stack),
        video_metadata(std::move(video_metadata))
    {
        auto content_page = std::make_unique<StaticPage>(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor());
        content_page_ptr = content_page.get();
        add_widget(std::move(content_page));

        add_widgets();
    }

    TrimmerPage::~TrimmerPage() {
        save_state();
    }

    std::unique_ptr<CustomRendererWidget> TrimmerPage::create_header(mgl::vec2f size) {
        auto header = std::make_unique<CustomRendererWidget>(size);
        header->draw_handler = [this, size](mgl::Window &window, mgl::vec2f pos, mgl::vec2f) {
            mgl::Rectangle background(size);
            background.set_position(pos);
            background.set_color(mgl::Color(0, 0, 0, 180));
            window.draw(background);

            mgl::Rectangle border(mgl::vec2f(size.x, size.y * 0.15f));
            border.set_position(pos);
            border.set_color(get_color_theme().tint_color);
            window.draw(border);

            mgl::Text video_path_text(video_metadata.filepath, get_theme().title_font_desc.c_str());
            video_path_text.set_position(pos + size / 2 - video_path_text.get_bounds().size / 2);
            video_path_text.set_color(mgl::Color(255, 255, 255, 255));
            window.draw(video_path_text);
        };
        return header;
    }

    std::unique_ptr<VideoPlayer> TrimmerPage::create_videoplayer(mgl::vec2f size) {
        auto player = std::make_unique<VideoPlayer>(gsr_info, size, video_metadata.filepath, VideoPlayer::PreviewSource::PROXY_FAST);

        player->set_seekbar_enabled(false);
        player->set_playback_state_callback([this](const VideoPlayer::PlaybackState &state) {
            playback_state = state;
        });

        video_player_ptr = player.get();
        return player;
    }

    std::unique_ptr<ScrollablePage> TrimmerPage::create_timeline(mgl::vec2f size) {
        auto timeline_scroll = std::make_unique<ScrollablePage>(size, ScrollablePage::ScrollbarSide::BOTTOM);
        timeline_scroll->set_scroll_input_callback([this](mgl::vec2f) {
            begin_timeline_scrub();
            timeline_scroll_settle_clock.restart();
        });
        timeline_scroll->set_scroll_changed_callback([this](mgl::vec2f scroll) {
            sync_timeline_scrub_position(scroll.x);
        });
        timeline_scroll_ptr = timeline_scroll.get();

        auto left_padding = std::make_unique<CustomRendererWidget>(mgl::vec2f(1.0f, timeline_scroll_ptr->get_inner_size().y));
        timeline_left_padding_ptr = left_padding.get();
        timeline_scroll_ptr->add_widget(std::move(left_padding));

        auto timeline = std::make_unique<TimelineWidget>(timeline_scroll_ptr->get_inner_size());
        timeline_ptr = timeline.get();
        timeline_scroll_ptr->add_widget(std::move(timeline));

        auto right_padding = std::make_unique<CustomRendererWidget>(mgl::vec2f(1.0f, timeline_scroll_ptr->get_inner_size().y));
        timeline_right_padding_ptr = right_padding.get();
        timeline_scroll_ptr->add_widget(std::move(right_padding));

        return timeline_scroll;
    }

    void TrimmerPage::add_widgets() {
        auto page_list = std::make_unique<List>(List::Orientation::VERTICAL, List::Alignment::CENTER);

        auto content_list = std::make_unique<List>(List::Orientation::VERTICAL, List::Alignment::CENTER);
        content_list->add_widget(create_header(mgl::vec2f(content_page_ptr->get_inner_size().x * 0.666f, content_page_ptr->get_inner_size().y * 0.033f)));
        content_list->add_widget(create_videoplayer(mgl::vec2f(content_page_ptr->get_inner_size().x, content_page_ptr->get_inner_size().x) * (
                                                    mgl::vec2f(1, video_metadata.height) / mgl::vec2f(1, video_metadata.width)) * 0.666f));
        content_list->add_widget(create_timeline(mgl::vec2f(content_page_ptr->get_inner_size().x * 0.666f, content_page_ptr->get_inner_size().y * 0.1f)));

        auto spacer_size = mgl::vec2f(content_page_ptr->get_inner_size().x, (content_page_ptr->get_inner_size().y - content_list->get_size().y) / 2);
        page_list->add_widget(std::make_unique<CustomRendererWidget>(spacer_size));
        page_list->add_widget(std::move(content_list));
        page_list->add_widget(std::make_unique<CustomRendererWidget>(spacer_size));

        content_page_ptr->add_widget(std::move(page_list));
    }

    bool TrimmerPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        const mgl::vec2f content_page_position = get_content_position();

        if(timeline_scroll_ptr && timeline_ptr && timeline_left_padding_ptr && timeline_right_padding_ptr) {
            const mgl::vec2f timeline_inner_size = timeline_scroll_ptr->get_inner_size();
            const float timeline_side_padding = timeline_inner_size.x * 0.5f;
            timeline_ptr->set_zoom_interaction_region({-timeline_side_padding + timeline_scroll_ptr->get_scroll().x, 0.0f}, timeline_inner_size);
        }

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

        return true;
    }

    void TrimmerPage::draw(mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return;

        const mgl::vec2f content_page_position = get_content_position();

        if(timeline_scroll_ptr && timeline_ptr && timeline_left_padding_ptr && timeline_right_padding_ptr) {
            const mgl::vec2f timeline_inner_size = timeline_scroll_ptr->get_inner_size();
            const float timeline_side_padding = timeline_inner_size.x * 0.5f;

            timeline_left_padding_ptr->set_size({timeline_side_padding, timeline_inner_size.y});
            timeline_left_padding_ptr->set_position({0.0f, 0.0f});

            if(!state_loaded && playback_state.duration_ms > 0) {
                load_state();
                state_loaded = true;
            }

            const int64_t visible_position_ms = (timeline_scrub_active && timeline_scrub_position_ms >= 0)
                ? timeline_scrub_position_ms
                : playback_state.position_ms;
            const bool visible_paused = timeline_scrub_active ? true : playback_state.paused;

            // TODO: Avoid modifying widget properties in `draw`, use callbacks instead!
            timeline_ptr->set_duration_ms(playback_state.duration_ms);
            timeline_ptr->set_position_ms(visible_position_ms);
            timeline_ptr->set_paused(visible_paused);
            timeline_ptr->set_size({timeline_ptr->get_timeline_width(), timeline_inner_size.y});
            timeline_ptr->set_position({timeline_side_padding, 0.0f});
            timeline_ptr->set_zoom_interaction_region({-timeline_side_padding + timeline_scroll_ptr->get_scroll().x, 0.0f}, timeline_inner_size);

            timeline_right_padding_ptr->set_size({timeline_side_padding, timeline_inner_size.y});
            timeline_right_padding_ptr->set_position({timeline_side_padding + timeline_ptr->get_size().x, 0.0f});

            if(video_player_ptr) {
                const std::string &proxy_path = video_player_ptr->get_proxy_video_path();
                if(!proxy_path.empty())
                    timeline_ptr->set_source_video_path(proxy_path);
            }

            const float desired_scroll_x = timeline_ptr->position_ms_to_scroll(visible_position_ms);
            const float current_scroll_x = timeline_scroll_ptr->get_scroll().x;
            const bool timeline_scroll_changed = std::abs(current_scroll_x - last_timeline_scroll_x) > 0.5f;
            const bool scrollbar_is_being_dragged = timeline_scroll_ptr->is_moving_scrollbar_with_cursor();
            const bool timeline_zoom_changed = timeline_ptr->take_zoom_changed();

            if(timeline_zoom_changed) {
                const float total_timeline_width = timeline_side_padding * 2.0f + timeline_ptr->get_size().x;
                const float max_scroll_x = std::max(0.0f, total_timeline_width - timeline_scroll_ptr->get_size().x);
                const float clamped_scroll_x = std::clamp(desired_scroll_x, 0.0f, max_scroll_x);
                timeline_scroll_ptr->set_scroll({clamped_scroll_x, 0.0f});
                timeline_scroll_ptr->reset_scrollbar_drag_anchor(window);
                last_timeline_scroll_x = clamped_scroll_x;
            }

            if(scrollbar_is_being_dragged && !timeline_scrub_active && !timeline_zoom_changed) {
                begin_timeline_scrub();
                timeline_scroll_settle_clock.restart();
            }

            if(timeline_scroll_changed && !timeline_zoom_changed) {
                last_timeline_scroll_x = current_scroll_x;

                const int64_t requested_position_ms = timeline_ptr->scroll_to_position_ms(current_scroll_x);

                const int64_t active_scrub_position_ms = timeline_scrub_active ? timeline_scrub_position_ms : playback_state.position_ms;
                if(requested_position_ms != active_scrub_position_ms) {
                    if(video_player_ptr && !timeline_scrub_active) {
                        timeline_scrub_active = true;
                        timeline_scrub_resume_on_release = !playback_state.paused;
                        timeline_scrub_position_ms = playback_state.position_ms;
                        video_player_ptr->begin_external_scrub();
                    }

                    timeline_scrub_position_ms = requested_position_ms;

                    if(video_player_ptr) {
                        video_player_ptr->update_external_scrub(requested_position_ms);
                    }

                    timeline_scroll_settle_clock.restart();
                }
            } else if(!timeline_scrub_active && std::abs(current_scroll_x - desired_scroll_x) > 0.5f) {
                timeline_scroll_ptr->set_scroll({desired_scroll_x, 0.0f});
                last_timeline_scroll_x = desired_scroll_x;
            }

            if(!scrollbar_is_being_dragged && timeline_scrub_active && timeline_scroll_settle_clock.get_elapsed_time_seconds() >= 0.12) {
                timeline_scrub_active = false;
                timeline_scrub_position_ms = -1;
                if(video_player_ptr)
                    video_player_ptr->end_external_scrub(timeline_scrub_resume_on_release, true);
                timeline_scrub_resume_on_release = false;
            }
        }

        draw_children(window, content_page_position);

        if(timeline_scroll_ptr && timeline_ptr) {
            mgl::Rectangle timeline_pointer(mgl::vec2f(timeline_scroll_ptr->get_size().x * 0.003f, timeline_ptr->get_inner_size().y));
            timeline_pointer.set_position(timeline_scroll_ptr->get_position() + mgl::vec2f(
                timeline_scroll_ptr->get_size().x / 2 - timeline_pointer.get_size().x / 2, 0));
            timeline_pointer.set_color(get_color_theme().tint_color);
            window.draw(timeline_pointer);
        }
    }

    void TrimmerPage::draw_children(mgl::Window &window, mgl::vec2f position) {
        Widget *selected_widget = selected_child_widget;

        const mgl::Scissor prev_scissor = window.get_scissor();
        window.set_scissor({position.to_vec2i(), content_page_ptr->get_inner_size().to_vec2i()});

        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget)
                widget->draw(window, position);
        }

        if(selected_widget)
            selected_widget->draw(window, position);

        window.set_scissor(prev_scissor);
    }

    void TrimmerPage::save_state() {
        if(video_metadata.filepath.empty() || playback_state.duration_ms <= 0)
            return;

        struct stat st;
        std::string key = video_metadata.filepath;
        if(stat(video_metadata.filepath.c_str(), &st) == 0)
            key += ":" + std::to_string((int64_t)st.st_mtim.tv_sec) + ":" + std::to_string((int64_t)st.st_size);

        const std::string trimmer_dir = get_state_dir() + "/trimmer";
        char dir_buffer[4096];
        snprintf(dir_buffer, sizeof(dir_buffer), "%s", trimmer_dir.c_str());
        create_directory_recursive(dir_buffer);

        const std::string path = trimmer_dir + "/" + std::to_string(std::hash<std::string>{}(key));

        const auto &chunks = timeline_ptr->get_chunks();

        std::string interval_line;
        std::string state_line;

        if(chunks.empty()) {
            interval_line = "0 " + std::to_string(playback_state.duration_ms);
            state_line = "1";
        } else {
            for(size_t i = 0; i < chunks.size(); ++i) {
                if(i > 0) {
                    interval_line += " ";
                    state_line += " ";
                }
                interval_line += std::to_string(chunks[i].start_ms) + " " + std::to_string(chunks[i].end_ms);
                state_line += chunks[i].enabled ? "1" : "0";
            }
        }

        file_overwrite(path.c_str(), interval_line + "\n" + state_line + "\n");
    }

    void TrimmerPage::load_state() {
        if(video_metadata.filepath.empty())
            return;

        struct stat st;
        std::string key = video_metadata.filepath;
        if(stat(video_metadata.filepath.c_str(), &st) == 0)
            key += ":" + std::to_string((int64_t)st.st_mtim.tv_sec) + ":" + std::to_string((int64_t)st.st_size);

        const std::string path = get_state_dir() + "/trimmer/" + std::to_string(std::hash<std::string>{}(key));

        std::string content;
        if(!file_get_content(path.c_str(), content))
            return;

        std::istringstream stream(content);
        std::string line;

        if(!std::getline(stream, line))
            return;

        std::vector<int64_t> intervals;
        {
            std::istringstream iss(line);
            int64_t val;
            while(iss >> val)
                intervals.push_back(val);
        }

        if(intervals.size() < 2 || intervals.size() % 2 != 0) {
            std::filesystem::remove(path.c_str());
            return;
        }

        if(!std::getline(stream, line)) {
            std::filesystem::remove(path.c_str());
            return;
        }

        std::vector<int> states;
        {
            std::istringstream iss(line);
            int val;
            while(iss >> val)
                states.push_back(val);
        }

        const size_t expected_chunks = intervals.size() / 2;
        if(states.size() != expected_chunks) {
            std::filesystem::remove(path.c_str());
            return;
        }

        if(intervals[0] != 0 || intervals.back() > playback_state.duration_ms) {
            std::filesystem::remove(path.c_str());
            return;
        }

        bool valid = true;
        for(size_t i = 0; i + 1 < intervals.size(); i += 2) {
            if(intervals[i] >= intervals[i + 1]) {
                valid = false;
                break;
            }
            if(i + 2 < intervals.size() && intervals[i + 1] != intervals[i + 2]) {
                valid = false;
                break;
            }
        }

        if(!valid) {
            std::filesystem::remove(path.c_str());
            return;
        }

        std::vector<TimelineWidget::TimelineChunk> chunk_list;
        chunk_list.reserve(expected_chunks);
        for(size_t i = 0; i < expected_chunks; ++i) {
            TimelineWidget::TimelineChunk chunk;
            chunk.start_ms = intervals[i * 2];
            chunk.end_ms = intervals[i * 2 + 1];
            chunk.enabled = states[i] != 0;
            chunk_list.push_back(chunk);
        }

        timeline_ptr->set_chunks(std::move(chunk_list));
    }

    mgl::vec2f TrimmerPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return content_page_ptr->get_size();
    }

    mgl::vec2f TrimmerPage::get_content_position() {
        const mgl::vec2f window_size = get_size();
        const mgl::vec2f content_page_size = content_page_ptr->get_size();

        return mgl::vec2f(window_size * 0.5f - content_page_size * 0.5f).floor();
    }

    void TrimmerPage::begin_timeline_scrub() {
        if(timeline_scrub_active)
            return;

        timeline_scrub_active = true;
        timeline_scrub_resume_on_release = !playback_state.paused;
        timeline_scrub_position_ms = playback_state.position_ms;
        if(video_player_ptr)
            video_player_ptr->begin_external_scrub();
    }

    void TrimmerPage::sync_timeline_scrub_position(float scroll_x) {
        if(!timeline_ptr)
            return;

        last_timeline_scroll_x = scroll_x;

        if(!timeline_scrub_active)
            return;

        const int64_t requested_position_ms = timeline_ptr->scroll_to_position_ms(scroll_x);
        if(requested_position_ms == timeline_scrub_position_ms)
            return;

        timeline_scrub_position_ms = requested_position_ms;
        if(video_player_ptr)
            video_player_ptr->update_external_scrub(requested_position_ms);

        timeline_scroll_settle_clock.restart();
    }
}
