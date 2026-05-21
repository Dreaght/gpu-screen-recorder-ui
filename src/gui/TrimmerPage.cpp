#include "../../include/gui/TrimmerPage.hpp"
#include "../../include/Theme.hpp"
#include "../../include/gui/CustomRendererWidget.hpp"
#include "../../include/gui/ScrollablePage.hpp"
#include "../../include/gui/TimelineWidget.hpp"
#include "../../include/gui/VideoPlayer.hpp"
#include "../../include/gui/Utils.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>

#include <cmath>

namespace gsr {
    TrimmerPage::TrimmerPage(PageStack *page_stack, std::string video_path, const mgl::vec2i size) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        page_stack(page_stack),
        video_path(std::move(video_path)),
        size(size),
        video_path_text(this->video_path, get_theme().title_font_desc.c_str())
    {
        auto player = std::make_unique<VideoPlayer>(TrimmerPage::get_size(), this->video_path, VideoPlayer::PreviewSource::PROXY_FAST);
        player->set_position({0.0f, 0.0f});
        player->set_seekbar_enabled(false);
        player->set_seek_state_callback([this](int64_t position_ms, int64_t duration_ms, bool paused) {
            playback_position_ms = position_ms;
            playback_duration_ms = duration_ms;
            playback_paused = paused;
            if(timeline_ptr) {
                timeline_ptr->set_duration_ms(duration_ms);
                timeline_ptr->set_position_ms(position_ms);
                timeline_ptr->set_paused(paused);

                const std::string proxy_path = video_player_ptr ? video_player_ptr->get_proxy_video_path() : std::string();
                if(!proxy_path.empty())
                    timeline_ptr->set_source_video_path(proxy_path);
            }
        });
        video_player_ptr = player.get();
        Page::add_widget(std::move(player));

        auto timeline_scroll = std::make_unique<ScrollablePage>(TrimmerPage::get_size() * mgl::vec2f(1.0f, 0.14f), ScrollablePage::ScrollbarSide::BOTTOM);
        timeline_scroll->set_position({0.0f, 0.0f});
        timeline_scroll_ptr = timeline_scroll.get();

        auto left_padding = std::make_unique<CustomRendererWidget>(mgl::vec2f(1.0f, timeline_scroll_ptr->get_inner_size().y));
        left_padding->set_position({0.0f, 0.0f});
        timeline_left_padding_ptr = left_padding.get();
        timeline_scroll_ptr->add_widget(std::move(left_padding));

        auto timeline = std::make_unique<TimelineWidget>(timeline_scroll_ptr->get_inner_size());
        timeline->set_position({0.0f, 0.0f});
        timeline_ptr = timeline.get();
        timeline_scroll_ptr->add_widget(std::move(timeline));

        auto right_padding = std::make_unique<CustomRendererWidget>(mgl::vec2f(1.0f, timeline_scroll_ptr->get_inner_size().y));
        right_padding->set_position({0.0f, 0.0f});
        timeline_right_padding_ptr = right_padding.get();
        timeline_scroll_ptr->add_widget(std::move(right_padding));

        Page::add_widget(std::move(timeline_scroll));
    }

    bool TrimmerPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        const mgl::vec2f content_page_position = get_content_position();
        const mgl::vec2f content_page_size = get_size();
        if(video_player_ptr)
            video_player_ptr->set_size(content_page_size);
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

    void TrimmerPage::draw(mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return;

        const mgl::vec2f content_page_position = get_content_position();
        const mgl::vec2f content_page_size = get_size();
        if(video_player_ptr)
            video_player_ptr->set_size(content_page_size);

        video_path_text.set_position((content_page_position + mgl::vec2f(
            content_page_size.x * 0.5f - video_path_text.get_bounds().size.x * 0.5f,
            - video_path_text.get_bounds().size.y * 1.5f
            )).floor());
        window.draw(video_path_text);

        if(timeline_scroll_ptr && timeline_ptr && timeline_left_padding_ptr && timeline_right_padding_ptr) {
            timeline_scroll_ptr->set_size(content_page_size * mgl::vec2f(1.0f, 0.14f));
            timeline_scroll_ptr->set_position({0.0f, content_page_size.y + get_theme().window_height / 70});

            const mgl::vec2f timeline_inner_size = timeline_scroll_ptr->get_inner_size();
            const float timeline_side_padding = timeline_inner_size.x * 0.5f;

            timeline_left_padding_ptr->set_size({timeline_side_padding, timeline_inner_size.y});
            timeline_left_padding_ptr->set_position({0.0f, 0.0f});

            timeline_ptr->set_duration_ms(playback_duration_ms);
            timeline_ptr->set_position_ms(playback_position_ms);
            timeline_ptr->set_paused(playback_paused);
            timeline_ptr->set_size({timeline_ptr->get_timeline_width(), timeline_inner_size.y});
            timeline_ptr->set_position({timeline_side_padding, 0.0f});

            timeline_right_padding_ptr->set_size({timeline_side_padding, timeline_inner_size.y});
            timeline_right_padding_ptr->set_position({timeline_side_padding + timeline_ptr->get_size().x, 0.0f});

            if(video_player_ptr) {
                const std::string &proxy_path = video_player_ptr->get_proxy_video_path();
                if(!proxy_path.empty())
                    timeline_ptr->set_source_video_path(proxy_path);
            }

            const float desired_scroll_x = timeline_ptr->position_ms_to_scroll(playback_position_ms);
            const float current_scroll_x = timeline_scroll_ptr->get_scroll().x;
            const bool timeline_scroll_changed = std::abs(current_scroll_x - last_timeline_scroll_x) > 0.5f;
            const bool scrollbar_is_being_dragged = timeline_scroll_ptr->is_moving_scrollbar_with_cursor();

            if(scrollbar_is_being_dragged && !timeline_scroll_dragging) {
                timeline_scroll_dragging = true;
                timeline_scroll_resume_on_release = !playback_paused;
                if(video_player_ptr)
                    video_player_ptr->begin_external_scrub();
                playback_paused = true;
                timeline_ptr->set_paused(true);
                timeline_scroll_settle_clock.restart();
            }

            if(timeline_scroll_changed) {
                last_timeline_scroll_x = current_scroll_x;
                const int64_t requested_position_ms = timeline_ptr->scroll_to_position_ms(current_scroll_x);
                if(requested_position_ms != playback_position_ms) {
                    if(video_player_ptr && !timeline_scroll_dragging) {
                        timeline_scroll_dragging = true;
                        timeline_scroll_resume_on_release = !playback_paused;
                        video_player_ptr->begin_external_scrub();
                        playback_paused = true;
                        timeline_ptr->set_paused(true);
                    }

                    playback_position_ms = requested_position_ms;
                    if(video_player_ptr) {
                        video_player_ptr->update_external_scrub(requested_position_ms);
                    }
                    timeline_scroll_settle_clock.restart();
                }
            } else if(!timeline_scroll_dragging && std::abs(current_scroll_x - desired_scroll_x) > 0.5f) {
                timeline_scroll_ptr->set_scroll({desired_scroll_x, 0.0f});
                last_timeline_scroll_x = desired_scroll_x;
            }

            if(!scrollbar_is_being_dragged && timeline_scroll_dragging && timeline_scroll_settle_clock.get_elapsed_time_seconds() >= 0.12) {
                timeline_scroll_dragging = false;
                if(video_player_ptr)
                    video_player_ptr->end_external_scrub(timeline_scroll_resume_on_release, false);
                timeline_scroll_resume_on_release = false;
            }
        }

        Widget *selected_widget = selected_child_widget;

        const mgl::Scissor prev_scissor = window.get_scissor();
        mgl::vec2f scissor_size = content_page_size;
        if(timeline_scroll_ptr)
            scissor_size.y += get_theme().window_height / 70 + timeline_scroll_ptr->get_size().y;
        window.set_scissor(scissor_get_sub_area(prev_scissor, {content_page_position.to_vec2i(), scissor_size.to_vec2i()}));

        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget)
                widget->draw(window, content_page_position);
        }

        if(selected_widget)
            selected_widget->draw(window, content_page_position);

        window.set_scissor(prev_scissor);

        if(timeline_scroll_ptr) {
            mgl::Rectangle timeline_pointer(content_page_size * mgl::vec2f(0.003f, 0.13f));
            timeline_pointer.set_position(content_page_position + mgl::vec2f(
                content_page_size.x / 2 - timeline_pointer.get_size().x / 2,
                content_page_size.y + get_theme().window_height / 70 - timeline_pointer.get_size().y / 2 + timeline_scroll_ptr->get_inner_size().y / 2
                ));
            timeline_pointer.set_color(get_color_theme().tint_color);
            window.draw(timeline_pointer);
        }
    }

    mgl::vec2f TrimmerPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size.to_vec2f();
    }

    mgl::vec2f TrimmerPage::get_content_position() {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = get_size();
        return mgl::vec2f(window_size * 0.5f - content_page_size * 0.5f).floor();
    }
}
