#pragma once

#include "StaticPage.hpp"
#include "VideoPlayer.hpp"
#include "../RecentVideos.hpp"

#include <mglpp/system/Clock.hpp>

namespace gsr {
    struct GsrInfo;
    class Label;
    class CustomRendererWidget;
    class ContainerButton;
    class ScrollablePage;
    class TimelineWidget;
}

namespace gsr {
    class PageStack;

    class TrimmerPage : public StaticPage {
    public:
        explicit TrimmerPage(const GsrInfo *gsr_info, PageStack *page_stack, VideoMetadata video_metadata);
        TrimmerPage(const TrimmerPage&) = delete;
        TrimmerPage& operator=(const TrimmerPage&) = delete;
        ~TrimmerPage() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
    private:
        std::unique_ptr<CustomRendererWidget> create_header(mgl::vec2f size);
        std::unique_ptr<VideoPlayer> create_videoplayer(mgl::vec2f size);
        std::unique_ptr<ScrollablePage> create_timeline(mgl::vec2f size);
        std::unique_ptr<ContainerButton> create_back_button(mgl::vec2f size);
        void add_widgets();

        void draw_children(mgl::Window &window, mgl::vec2f position);

        mgl::vec2f get_content_position();
        void begin_timeline_scrub();
        void sync_timeline_scrub_position(float scroll_x);
        void save_state();
        void load_state();
    private:
        const GsrInfo *gsr_info = nullptr;
        StaticPage *content_page_ptr = nullptr;
        PageStack *page_stack = nullptr;
        VideoMetadata video_metadata;
        VideoPlayer *video_player_ptr = nullptr;
        ScrollablePage *timeline_scroll_ptr = nullptr;
        TimelineWidget *timeline_ptr = nullptr;
        CustomRendererWidget *timeline_left_padding_ptr = nullptr;
        CustomRendererWidget *timeline_right_padding_ptr = nullptr;
        VideoPlayer::PlaybackState playback_state;
        float last_timeline_scroll_x = -1.0f;
        bool timeline_scrub_active = false;
        bool timeline_scrub_resume_on_release = false;
        int64_t timeline_scrub_position_ms = -1;
        mgl::Clock timeline_scroll_settle_clock;
        bool state_loaded = false;
    };
}
