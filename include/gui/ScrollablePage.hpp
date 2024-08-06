#pragma once

#include "Page.hpp"

namespace gsr {
    class ScrollablePage : public Page {
    public:
        ScrollablePage(mgl::vec2f size);
        ScrollablePage(const ScrollablePage&) = delete;
        ScrollablePage& operator=(const ScrollablePage&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        void set_margins(float top, float bottom, float left, float right);
    private:
        float get_border_size() const;
    private:
        mgl::vec2f size;
        float margin_top_scale = 0.0f;
        float margin_bottom_scale = 0.0f;
        float margin_left_scale = 0.0f;
        float margin_right_scale = 0.0f;
    };
}