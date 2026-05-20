#include "../../include/gui/TrimmerPage.hpp"
#include "../../include/Theme.hpp"
#include "../../include/gui/VideoPlayer.hpp"
#include "../../include/gui/Utils.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/window/Window.hpp>

#include <algorithm>
#include <cmath>

namespace gsr {
    namespace {
        static const double seek_dispatch_interval_seconds = 1.0 / 30.0;
        static const float seekbar_horizontal_padding_ratio = 0.08f;
        static const float seekbar_vertical_offset_ratio = 0.06f;
        static const float seekbar_height_ratio = 0.02f;
        static const float seekbar_hitbox_height_ratio = 0.08f;
    }

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
            if(!dragging_seekbar)
                seekbar_drag_position_ms = position_ms;
        });
        video_player_ptr = player.get();
        Page::add_widget(std::move(player));
    }

    bool TrimmerPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        const mgl::vec2f content_page_position = get_content_position();
        const mgl::vec2f content_page_size = get_size();
        if(video_player_ptr)
            video_player_ptr->set_size(content_page_size);

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const mgl::vec2f mouse_pos((float)event.mouse_button.x, (float)event.mouse_button.y);
            if(get_seekbar_hitbox(content_page_position, content_page_size).contains(mouse_pos)) {
                dragging_seekbar = true;
                dragging_seekbar_resume_on_release = !playback_paused;
                seekbar_drag_position_ms = playback_position_ms;
                seekbar_seek_clock.restart();
                if(video_player_ptr)
                    video_player_ptr->begin_external_scrub();
                playback_paused = true;
                update_seekbar_drag(content_page_position, content_page_size, mouse_pos);
                return false;
            }
        }

        if(event.type == mgl::Event::MouseButtonReleased && event.mouse_button.button == mgl::Mouse::Left && dragging_seekbar) {
            dragging_seekbar = false;
            if(video_player_ptr)
                video_player_ptr->end_external_scrub(dragging_seekbar_resume_on_release);
            dragging_seekbar_resume_on_release = false;
            return false;
        }

        if(event.type == mgl::Event::MouseMoved && dragging_seekbar) {
            update_seekbar_drag(content_page_position, content_page_size, {(float)event.mouse_move.x, (float)event.mouse_move.y});
            return false;
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
    }

    void TrimmerPage::draw(mgl::Window &window, mgl::vec2f offset) {
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

        Widget *selected_widget = selected_child_widget;

        const mgl::Scissor prev_scissor = window.get_scissor();
        mgl::vec2f scissor_size = content_page_size;
        scissor_size.y += get_theme().window_height * 0.12f;
        window.set_scissor(scissor_get_sub_area(prev_scissor, {content_page_position.to_vec2i(), scissor_size.to_vec2i()}));

        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget)
                widget->draw(window, content_page_position);
        }

        if(selected_widget)
            selected_widget->draw(window, content_page_position);

        const mgl::FloatRect seekbar_hitbox = get_seekbar_hitbox(content_page_position, content_page_size);
        const float seekbar_height = std::max(3.0f, content_page_size.y * seekbar_height_ratio);
        const mgl::vec2f seekbar_pos = mgl::vec2f(
            seekbar_hitbox.position.x,
            seekbar_hitbox.position.y + seekbar_hitbox.size.y * 0.5f - seekbar_height * 0.5f
        ).floor();
        const mgl::vec2f seekbar_size(seekbar_hitbox.size.x, seekbar_height);
        const int64_t visible_position_ms = dragging_seekbar ? seekbar_drag_position_ms : playback_position_ms;
        const float progress = playback_duration_ms > 0
            ? std::clamp((float)((double)visible_position_ms / (double)playback_duration_ms), 0.0f, 1.0f)
            : 0.0f;

        mgl::Rectangle seekbar_track(seekbar_size);
        seekbar_track.set_position(seekbar_pos);
        seekbar_track.set_color(mgl::Color(255, 255, 255, 70));
        window.draw(seekbar_track);

        mgl::Rectangle seekbar_fill({seekbar_size.x * progress, seekbar_size.y});
        seekbar_fill.set_position(seekbar_pos);
        seekbar_fill.set_color(get_color_theme().tint_color);
        window.draw(seekbar_fill);

        mgl::Rectangle seekbar_pointer({std::max(3.0f, content_page_size.x * 0.003f), content_page_size.y * 0.10f});
        seekbar_pointer.set_position({
            content_page_position.x + content_page_size.x * 0.5f - seekbar_pointer.get_size().x * 0.5f,
            seekbar_pos.y + seekbar_size.y * 0.5f - seekbar_pointer.get_size().y * 0.5f
        });
        seekbar_pointer.set_color(get_color_theme().tint_color);
        window.draw(seekbar_pointer);

        window.set_scissor(prev_scissor);
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

    mgl::FloatRect TrimmerPage::get_seekbar_hitbox(mgl::vec2f content_page_position, mgl::vec2f content_page_size) const {
        const float padding_x = std::max(12.0f, content_page_size.x * seekbar_horizontal_padding_ratio);
        const float offset_y = std::max(16.0f, content_page_size.y * seekbar_vertical_offset_ratio);
        const float hitbox_height = std::max(18.0f, content_page_size.y * seekbar_hitbox_height_ratio);
        return {
            content_page_position + mgl::vec2f(padding_x, content_page_size.y + offset_y),
            mgl::vec2f(std::max(1.0f, content_page_size.x - padding_x * 2.0f), hitbox_height)
        };
    }

    void TrimmerPage::update_seekbar_drag(mgl::vec2f content_page_position, mgl::vec2f content_page_size, mgl::vec2f mouse_pos) {
        if(playback_duration_ms <= 0)
            return;

        const mgl::FloatRect seekbar_hitbox = get_seekbar_hitbox(content_page_position, content_page_size);
        const float relative_x = std::clamp((mouse_pos.x - seekbar_hitbox.position.x) / seekbar_hitbox.size.x, 0.0f, 1.0f);
        seekbar_drag_position_ms = std::min<int64_t>((int64_t)(relative_x * (double)playback_duration_ms), std::max<int64_t>(0, playback_duration_ms - 1));

        if(video_player_ptr && seekbar_seek_clock.get_elapsed_time_seconds() >= seek_dispatch_interval_seconds) {
            video_player_ptr->update_external_scrub(seekbar_drag_position_ms);
            seekbar_seek_clock.restart();
        }
    }
}
