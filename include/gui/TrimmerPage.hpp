#pragma once

#include "StaticPage.hpp"

#include <mglpp/graphics/Text.hpp>
#include <mglpp/system/Clock.hpp>

namespace gsr {
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
        mgl::FloatRect get_seekbar_hitbox(mgl::vec2f content_page_position, mgl::vec2f content_page_size) const;
        void update_seekbar_drag(mgl::vec2f content_page_position, mgl::vec2f content_page_size, mgl::vec2f mouse_pos);
    private:
        PageStack *page_stack = nullptr;
        std::string video_path;
        mgl::vec2i size;
        mgl::Text video_path_text;
        VideoPlayer *video_player_ptr = nullptr;
        int64_t playback_position_ms = 0;
        int64_t playback_duration_ms = 0;
        int64_t seekbar_drag_position_ms = 0;
        bool playback_paused = true;
        bool dragging_seekbar = false;
        bool dragging_seekbar_resume_on_release = false;
        mgl::Clock seekbar_seek_clock;
    };
}
