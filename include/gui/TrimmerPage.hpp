#pragma once

#include "StaticPage.hpp"

#include <mglpp/graphics/Text.hpp>

namespace gsr {
    class VideoPlayer;
}

#include <memory>

namespace gsr {
    class PageStack;

    class TrimmerPage : public StaticPage {
    public:
        explicit TrimmerPage(PageStack *page_stack, std::string video_path);
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
        mgl::Text video_path_text;
        VideoPlayer *video_player_ptr = nullptr;
    };
}
