#pragma once

#include "StaticPage.hpp"
#include "Button.hpp"

#include <mglpp/graphics/Text.hpp>

namespace gsr {
    class PageStack;

    class ExportPage : public StaticPage {
    public:
        explicit ExportPage(PageStack *page_stack);
        ExportPage(const ExportPage&) = delete;
        ExportPage& operator=(const ExportPage&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        mgl::vec2f get_inner_size() override;
    private:
        void draw_page_label(mgl::Window &window, mgl::vec2f body_pos);
        void draw_buttons(mgl::Window &window, mgl::vec2f body_pos, mgl::vec2f body_size);

        std::unique_ptr<Widget> create_video_section();
        std::unique_ptr<Widget> create_settings();
        void add_widgets();

        void set_margins(float top, float bottom, float left, float right);
        void add_button(const std::string &text, const std::string &id, mgl::Color color);
        float get_border_size() const;
        float get_horizontal_spacing() const;
        mgl::vec2f get_content_position();
    private:
        struct ButtonItem {
            std::unique_ptr<Button> button;
            std::string id;
        };

        float margin_top_scale = 0.0f;
        float margin_bottom_scale = 0.0f;
        float margin_left_scale = 0.0f;
        float margin_right_scale = 0.0f;

        PageStack *page_stack = nullptr;
        mgl::Text top_text;
        mgl::Text bottom_text;
        std::vector<ButtonItem> buttons;

        std::function<void(const std::string &id)> on_click;
    };
}
