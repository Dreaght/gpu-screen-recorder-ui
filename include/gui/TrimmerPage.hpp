#pragma once

#include "StaticPage.hpp"

#include <mglpp/graphics/Text.hpp>
#include <mglpp/system/Clock.hpp>

namespace gsr {
    class CustomRendererWidget;
    class ScrollablePage;
    class TimelineWidget;
    class VideoPlayer;
}

#include <memory>

namespace gsr {
    class PageStack;

    class TrimmerPage : public StaticPage {
    public:
        explicit TrimmerPage(PageStack *page_stack, std::string video_path, mgl::vec2i size);
        TrimmerPage(const TrimmerPage&) = delete;
        TrimmerPage& operator=(const TrimmerPage&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
    private:
        mgl::vec2f get_content_position();
    private:
        PageStack *page_stack = nullptr;
        std::string video_path;
        mgl::vec2i size;
        mgl::Text video_path_text;
        VideoPlayer *video_player_ptr = nullptr;
        ScrollablePage *timeline_scroll_ptr = nullptr;
        TimelineWidget *timeline_ptr = nullptr;
        CustomRendererWidget *timeline_left_padding_ptr = nullptr;
        CustomRendererWidget *timeline_right_padding_ptr = nullptr;
        int64_t playback_position_ms = 0;
        int64_t playback_duration_ms = 0;
        bool playback_paused = true;
        float last_timeline_scroll_x = -1.0f;
        bool timeline_scroll_dragging = false;
        bool timeline_scroll_resume_on_release = false;
        mgl::Clock timeline_scroll_settle_clock;
    };
}
