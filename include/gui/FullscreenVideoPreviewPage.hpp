#pragma once

#include "Page.hpp"
#include "TimelineWidget.hpp"

#include <functional>
#include <vector>

namespace gsr {
    class PageStack;
    class VideoPlayer;

    class FullscreenVideoPreviewPage : public Page {
    public:
        FullscreenVideoPreviewPage(PageStack *page_stack, VideoPlayer *video_player, int video_width, int video_height,
            std::function<void(bool)> on_active_changed,
            std::function<std::vector<TimelineWidget::TimelineChunk>()> get_chunks);

        void on_navigate_to_page() override;
        void on_navigate_away_from_page() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

    private:
        struct SeekbarLayout {
            mgl::FloatRect hitbox;
            mgl::vec2f track_pos;
            mgl::vec2f track_size;
        };

        void layout_player();
        SeekbarLayout get_seekbar_layout() const;
        int64_t get_total_enabled_duration_ms(const std::vector<TimelineWidget::TimelineChunk> &chunks) const;
        int64_t source_position_to_enabled_offset_ms(const std::vector<TimelineWidget::TimelineChunk> &chunks, int64_t position_ms) const;
        int64_t enabled_offset_to_source_position_ms(const std::vector<TimelineWidget::TimelineChunk> &chunks, int64_t enabled_offset_ms) const;
        bool handle_seekbar_event(mgl::Event &event);
        void begin_external_scrub();
        void update_external_scrub(float mouse_x, const SeekbarLayout &layout, const std::vector<TimelineWidget::TimelineChunk> &chunks, int64_t total_enabled_duration_ms);
        void end_external_scrub(bool exact_seek);
        void draw_external_seekbar(mgl::Window &window);

        PageStack *page_stack = nullptr;
        VideoPlayer *video_player = nullptr;
        int video_width = 1;
        int video_height = 1;
        mgl::vec2f previous_position;
        mgl::vec2f previous_size;
        bool previous_seekbar_enabled = false;
        bool previous_smart_controls_hide_enabled = false;
        std::function<void(bool)> on_active_changed;
        std::function<std::vector<TimelineWidget::TimelineChunk>()> get_chunks;
        bool external_scrub_active = false;
        bool resume_after_external_scrub = false;
    };
}
