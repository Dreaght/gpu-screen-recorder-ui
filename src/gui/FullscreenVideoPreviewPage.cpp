#include "../../include/gui/FullscreenVideoPreviewPage.hpp"
#include "../../include/Theme.hpp"
#include "../../include/gui/PageStack.hpp"
#include "../../include/gui/VideoPlayer.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/system/FloatRect.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/window/Window.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace gsr {
    namespace {
        static const float external_seekbar_proximity_padding_scale = 0.09f;
        static const float external_seekbar_proximity_min_padding = 28.0f;
    }

    FullscreenVideoPreviewPage::FullscreenVideoPreviewPage(PageStack *page_stack, VideoPlayer *video_player, int video_width, int video_height,
        std::function<void(bool)> on_active_changed,
        std::function<std::vector<TimelineWidget::TimelineChunk>()> get_chunks) :
        page_stack(page_stack),
        video_player(video_player),
        video_width(std::max(1, video_width)),
        video_height(std::max(1, video_height)),
        on_active_changed(std::move(on_active_changed)),
        get_chunks(std::move(get_chunks))
    {}

    void FullscreenVideoPreviewPage::on_navigate_to_page() {
        if(!video_player)
            return;

        previous_position = video_player->get_position();
        previous_size = video_player->get_size();
        previous_seekbar_enabled = video_player->is_seekbar_enabled();
        previous_smart_controls_hide_enabled = video_player->is_smart_controls_hide_enabled();

        on_active_changed(true);
        video_player->set_seekbar_enabled(false);
        video_player->set_smart_controls_hide_enabled(true);
        layout_player();
        video_player->request_redraw();
    }

    void FullscreenVideoPreviewPage::on_navigate_away_from_page() {
        if(!video_player)
            return;

        end_external_scrub(false);
        video_player->cancel_scrub(false, true);
        video_player->set_seekbar_enabled(previous_seekbar_enabled);
        video_player->set_smart_controls_hide_enabled(previous_smart_controls_hide_enabled);
        video_player->set_position(previous_position);
        video_player->set_size(previous_size);
        video_player->request_redraw();
        on_active_changed(false);
    }

    bool FullscreenVideoPreviewPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!video_player)
            return false;

        if(event.type == mgl::Event::KeyReleased && event.key.code == mgl::Keyboard::Key::F) {
            page_stack->pop();
            return false;
        }

        if(event.type == mgl::Event::KeyPressed && event.key.code == mgl::Keyboard::Key::Space) {
            if(external_scrub_active)
                return false;

            video_player->toggle_pause();
            return false;
        }

        layout_player();
        if(handle_seekbar_event(event))
            return false;

        return video_player->on_event(event, window, {0.0f, 0.0f});
    }

    void FullscreenVideoPreviewPage::draw(mgl::Window &window, mgl::vec2f) {
        if(!video_player)
            return;

        layout_player();
        video_player->draw(window, {0.0f, 0.0f});
        draw_external_seekbar(window);
    }

    mgl::vec2f FullscreenVideoPreviewPage::get_size() {
        return mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
    }

    void FullscreenVideoPreviewPage::layout_player() {
        if(!video_player)
            return;

        const mgl::vec2f page_size = get_size();
        const float seekbar_region_height = std::max(42.0f, page_size.y * 0.075f);
        const float vertical_gap = std::max(12.0f, page_size.y * 0.02f);
        // const float available_height = std::max(1.0f, page_size.y - seekbar_region_height - vertical_gap * 2.0f);
        // const float width_scale = page_size.x / (float)video_width;
        // const float height_scale = available_height / (float)video_height;
        // const float scale = std::min(width_scale, height_scale);
        const mgl::vec2f player_size = mgl::vec2f(video_width, video_height).floor();
        const float top = std::max(0.0f, (page_size.y - (player_size.y + vertical_gap + seekbar_region_height)) * 0.5f);
        const mgl::vec2f player_position = mgl::vec2f((page_size.x - player_size.x) * 0.5f, top).floor();
        video_player->set_size(player_size);
        video_player->set_position(player_position);
    }

    FullscreenVideoPreviewPage::SeekbarLayout FullscreenVideoPreviewPage::get_seekbar_layout() const {
        const mgl::vec2f page_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f player_pos = video_player->get_position();
        const mgl::vec2f player_size = video_player->get_size();
        const float padding_x = std::max(24.0f, page_size.x * 0.08f);
        const float gap_y = std::max(12.0f, page_size.y * 0.02f);
        const float hitbox_height = std::max(22.0f, page_size.y * 0.035f);
        const float track_height = std::max(4.0f, page_size.y * 0.012f);
        const float hitbox_y = std::min(page_size.y - hitbox_height - gap_y, player_pos.y + player_size.y + gap_y);
        const mgl::FloatRect hitbox(
            mgl::vec2f(padding_x, hitbox_y).floor(),
            mgl::vec2f(std::max(1.0f, page_size.x - padding_x * 2.0f), hitbox_height).floor()
        );
        const mgl::vec2f track_pos = mgl::vec2f(hitbox.position.x, hitbox.position.y + hitbox.size.y * 0.5f - track_height * 0.5f).floor();
        return { hitbox, track_pos, mgl::vec2f(hitbox.size.x, track_height).floor() };
    }

    int64_t FullscreenVideoPreviewPage::get_total_enabled_duration_ms(const std::vector<TimelineWidget::TimelineChunk> &chunks) const {
        int64_t total_enabled_ms = 0;
        for(const auto &chunk : chunks) {
            if(chunk.enabled)
                total_enabled_ms += std::max<int64_t>(0, chunk.end_ms - chunk.start_ms);
        }
        return total_enabled_ms;
    }

    int64_t FullscreenVideoPreviewPage::source_position_to_enabled_offset_ms(const std::vector<TimelineWidget::TimelineChunk> &chunks, int64_t position_ms) const {
        int64_t enabled_offset_ms = 0;
        for(const auto &chunk : chunks) {
            if(!chunk.enabled)
                continue;

            if(position_ms <= chunk.start_ms)
                break;

            enabled_offset_ms += std::min(position_ms, chunk.end_ms) - chunk.start_ms;
            if(position_ms < chunk.end_ms)
                break;
        }
        return enabled_offset_ms;
    }

    int64_t FullscreenVideoPreviewPage::enabled_offset_to_source_position_ms(const std::vector<TimelineWidget::TimelineChunk> &chunks, int64_t enabled_offset_ms) const {
        int64_t remaining_ms = std::max<int64_t>(0, enabled_offset_ms);
        for(const auto &chunk : chunks) {
            if(!chunk.enabled)
                continue;

            const int64_t chunk_duration_ms = std::max<int64_t>(0, chunk.end_ms - chunk.start_ms);
            if(remaining_ms < chunk_duration_ms)
                return chunk.start_ms + remaining_ms;

            remaining_ms -= chunk_duration_ms;
        }

        for(auto iter = chunks.rbegin(); iter != chunks.rend(); ++iter) {
            if(iter->enabled)
                return std::max<int64_t>(iter->start_ms, iter->end_ms - 1);
        }
        return 0;
    }

    bool FullscreenVideoPreviewPage::handle_seekbar_event(mgl::Event &event) {
        const auto chunks = get_chunks();
        const int64_t total_enabled_duration_ms = get_total_enabled_duration_ms(chunks);
        if(total_enabled_duration_ms <= 0)
            return false;

        const SeekbarLayout layout = get_seekbar_layout();

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const mgl::vec2f mouse_pos((float)event.mouse_button.x, (float)event.mouse_button.y);
            if(!layout.hitbox.contains(mouse_pos))
                return false;

            begin_external_scrub();
            update_external_scrub(mouse_pos.x, layout, chunks, total_enabled_duration_ms);
            return true;
        }

        if(event.type == mgl::Event::MouseMoved && external_scrub_active) {
            update_external_scrub((float)event.mouse_move.x, layout, chunks, total_enabled_duration_ms);
            return true;
        }

        if(event.type == mgl::Event::MouseButtonReleased && event.mouse_button.button == mgl::Mouse::Left && external_scrub_active) {
            end_external_scrub(true);
            return true;
        }

        return false;
    }

    void FullscreenVideoPreviewPage::begin_external_scrub() {
        if(external_scrub_active || !video_player)
            return;

        external_scrub_active = true;
        resume_after_external_scrub = !video_player->is_paused();
        video_player->begin_external_scrub();
    }

    void FullscreenVideoPreviewPage::update_external_scrub(float mouse_x, const SeekbarLayout &layout, const std::vector<TimelineWidget::TimelineChunk> &chunks, int64_t total_enabled_duration_ms) {
        if(!external_scrub_active || !video_player)
            return;

        const float relative_x = std::clamp((mouse_x - layout.hitbox.position.x) / std::max(1.0f, layout.hitbox.size.x), 0.0f, 1.0f);
        const int64_t enabled_offset_ms = std::min<int64_t>(
            total_enabled_duration_ms - 1,
            std::max<int64_t>(0, (int64_t)std::llround(relative_x * (double)total_enabled_duration_ms))
        );
        const int64_t source_position_ms = enabled_offset_to_source_position_ms(chunks, enabled_offset_ms);
        video_player->update_external_scrub(source_position_ms);
    }

    void FullscreenVideoPreviewPage::end_external_scrub(bool exact_seek) {
        if(!external_scrub_active || !video_player)
            return;

        external_scrub_active = false;
        video_player->end_external_scrub(resume_after_external_scrub, exact_seek);
        resume_after_external_scrub = false;
    }

    void FullscreenVideoPreviewPage::draw_external_seekbar(mgl::Window &window) {
        const auto chunks = get_chunks();
        const int64_t total_enabled_duration_ms = get_total_enabled_duration_ms(chunks);
        if(total_enabled_duration_ms <= 0)
            return;

        const SeekbarLayout layout = get_seekbar_layout();
        if(video_player->is_smart_controls_hide_enabled() && !external_scrub_active && !is_mouse_near_seekbar(window, layout))
            return;

        const auto playback_state = video_player->get_playback_state();
        const int64_t enabled_offset_ms = std::min<int64_t>(
            total_enabled_duration_ms,
            source_position_to_enabled_offset_ms(chunks, playback_state.position_ms)
        );
        const float progress = total_enabled_duration_ms > 0
            ? std::clamp((float)((double)enabled_offset_ms / (double)total_enabled_duration_ms), 0.0f, 1.0f)
            : 0.0f;

        mgl::Rectangle track(layout.track_size);
        track.set_position(layout.track_pos);
        track.set_color(mgl::Color(255, 255, 255, 70));
        window.draw(track);

        mgl::Rectangle fill({ layout.track_size.x * progress, layout.track_size.y });
        fill.set_position(layout.track_pos);
        fill.set_color(get_color_theme().tint_color);
        window.draw(fill);

        int64_t accumulated_ms = 0;
        for(const auto &chunk : chunks) {
            if(!chunk.enabled)
                continue;

            const int64_t chunk_duration_ms = std::max<int64_t>(0, chunk.end_ms - chunk.start_ms);
            if(chunk_duration_ms <= 0)
                continue;

            accumulated_ms += chunk_duration_ms;
            if(accumulated_ms >= total_enabled_duration_ms)
                continue;

            const float divider_progress = (float)((double)accumulated_ms / (double)total_enabled_duration_ms);
            const float divider_x = layout.track_pos.x + layout.track_size.x * divider_progress;
            mgl::Rectangle divider({ std::max(1.0f, get_size().x * 0.0015f), layout.track_size.y * 2.5f });
            divider.set_position(mgl::vec2f(divider_x, layout.track_pos.y + layout.track_size.y * 0.5f - divider.get_size().y * 0.5f).floor());
            divider.set_color(mgl::Color(0, 0, 0, 180));
            window.draw(divider);
        }
    }

    bool FullscreenVideoPreviewPage::is_mouse_near_seekbar(mgl::Window &window, const SeekbarLayout &layout) const {
        const mgl::vec2f page_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const float proximity_padding = std::max(
            external_seekbar_proximity_min_padding,
            std::min(page_size.x, page_size.y) * external_seekbar_proximity_padding_scale
        );
        const mgl::vec2f mouse_pos = window.get_mouse_position().to_vec2f();
        const mgl::FloatRect expanded_hitbox(
            layout.hitbox.position - mgl::vec2f(proximity_padding, proximity_padding),
            layout.hitbox.size + mgl::vec2f(proximity_padding * 2.0f, proximity_padding * 2.0f)
        );
        return expanded_hitbox.contains(mouse_pos);
    }
}
