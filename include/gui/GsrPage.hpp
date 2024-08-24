#pragma once

#include "Page.hpp"
#include "Button.hpp"

#include <mglpp/graphics/Text.hpp>

namespace gsr {
    class GsrPage : public Page {
    public:
        GsrPage();
        GsrPage(const GsrPage&) = delete;
        GsrPage& operator=(const GsrPage&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        mgl::vec2f get_inner_size() override;

        void set_margins(float top, float bottom, float left, float right);
        void set_on_back_button_click(std::function<void()> on_click_handler);
    private:
        void draw_page_label(mgl::Window &window, mgl::vec2f body_pos);
        void draw_children(mgl::Window &window, mgl::vec2f position);

        float get_border_size() const;
        float get_horizontal_spacing() const;
        mgl::vec2f get_content_position();
        mgl::vec2f get_content_position_with_margin();
    private:
        float margin_top_scale = 0.0f;
        float margin_bottom_scale = 0.0f;
        float margin_left_scale = 0.0f;
        float margin_right_scale = 0.0f;
        Button back_button;
        mgl::Text label_text;
    };
}