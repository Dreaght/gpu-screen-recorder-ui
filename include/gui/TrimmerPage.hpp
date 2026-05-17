#pragma once

#include "StaticPage.hpp"

namespace gsr {
    class PageStack;

    class TrimmerPage : public StaticPage {
    public:
        explicit TrimmerPage(PageStack *page_stack);
        TrimmerPage(const TrimmerPage&) = delete;
        TrimmerPage& operator=(const TrimmerPage&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
    private:
        mgl::vec2f get_content_position();
    private:
        PageStack *page_stack = nullptr;
    };
}